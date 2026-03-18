import os
import numpy as np
import torch
import torch.nn as nn
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers

# --- [수정 핵심 1: 환경 변수 설정 오류 해결] ---
# 환경 변수 객체를 보존하면서 값만 설정해야 합니다.
os.environ = "0"
os.environ = "1" 

# 1. PyTorch 모델 정의 (도시 숲 화재 감지용 설계도)
class PT_ForestFireDetector(nn.Module):
    def __init__(self):
        super().__init__()
        # 24채널 경량 구조 (MobileNetV2 스타일)
        self.conv_rgb = nn.Conv2d(3, 24, 3, padding=1)
        self.conv_thermal = nn.Conv2d(1, 24, 3, padding=1)
        self.fc = nn.Linear(24, 1)

    def forward(self, rgb_seq, thermal_seq):
        # 8프레임 시퀀스 중 마지막 프레임의 특징을 추출하여 결합
        x_r = torch.relu(self.conv_rgb(rgb_seq[:, -1]))
        x_t = torch.relu(self.conv_thermal(thermal_seq[:, -1]))
        # 전역 평균 풀링 (Global Average Pooling)
        fused = (x_r + x_t).mean(dim=[1, 2]) 
        return torch.sigmoid(self.fc(fused))

# PyTorch 가중치 임시 저장 (훈련이 완료된 상태라고 가정)
pt_model = PT_ForestFireDetector().eval()
torch.save(pt_model.state_dict(), "pt_weights.pth")
print("1. PyTorch 가중치 준비 완료.")

# 2. Keras 브릿지 모델 생성 (라즈베리 파이 배포용 뼈대)
def build_keras_model():
    # Keras 형식: (Batch, Sequence, Height, Width, Channel)
    input_rgb = layers.Input(shape=(8, 112, 112, 3), name="input_rgb")
    input_thermal = layers.Input(shape=(8, 112, 112, 1), name="input_thermal")

    # 마지막 프레임 추출 [3]
    x_r_frame = input_rgb[:, -1, :, :, :]
    x_t_frame = input_thermal[:, -1, :, :, :]

    # 특징 추출 레이어 (PyTorch와 동일한 구조)
    k_conv_rgb = layers.Conv2D(24, 3, padding='same', activation='relu', name="k_conv_rgb")(x_r_frame)
    k_conv_thermal = layers.Conv2D(24, 3, padding='same', activation='relu', name="k_conv_thermal")(x_t_frame)
    
    # 융합 및 분류
    fused = layers.Add()([k_conv_rgb, k_conv_thermal])
    gap = layers.GlobalAveragePooling2D()(fused)
    output = layers.Dense(1, activation='sigmoid', name="k_fc")(gap)
    
    return keras.Model(inputs=[input_rgb, input_thermal], outputs=output)

k_model = build_keras_model()
print("2. Keras 브릿지 모델 생성 완료.")

# 3. 가중치 이식 (PyTorch 지능을 Keras 뼈대에 주입)
def transfer_weights(pt_path, k_model):
    sd = torch.load(pt_path, map_location='cpu')
    
    # Conv2D 가중치 전치: (Out, In, H, W) -> (H, W, In, Out)
    k_model.get_layer("k_conv_rgb").set_weights([
        sd['conv_rgb.weight'].numpy().transpose(2, 3, 1, 0),
        sd['conv_rgb.bias'].numpy()
    ])
    k_model.get_layer("k_conv_thermal").set_weights([
        sd['conv_thermal.weight'].numpy().transpose(2, 3, 1, 0),
        sd['conv_thermal.bias'].numpy()
    ])
    # Dense 가중치 전치: (Out, In) -> (In, Out)
    k_model.get_layer("k_fc").set_weights([
        sd['fc.weight'].numpy().transpose(),
        sd['fc.bias'].numpy()
    ])

transfer_weights("pt_weights.pth", k_model)
print("3. 가중치 이식 성공 (PyTorch -> Keras).")

# 4. TFLite INT8 양자화 및 변환
def representative_data_gen():
    for _ in range(100):
        # 실제 데이터 범위를 모사한 0~1 사이의 가상 데이터
        rgb = np.random.rand(1, 8, 112, 112, 3).astype(np.float32)
        thermal = np.random.rand(1, 8, 112, 112, 1).astype(np.float32)
        yield [rgb, thermal]

# --- [수정 핵심 2: 누락된 양자화 설정값 삽입] ---
converter = tf.lite.TFLiteConverter.from_keras_model(k_model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_data_gen # 보정 데이터셋 연결
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

print("4. TFLite INT8 양자화 진행 중...")
tflite_model = converter.convert()
with open("fire_model_int8.tflite", "wb") as f:
    f.write(tflite_model)

print("✅ 최종 성공: fire_model_int8.tflite 파일이 생성되었습니다.")
