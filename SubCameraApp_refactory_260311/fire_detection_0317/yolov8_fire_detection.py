import torch
import numpy as np
import tensorflow as tf
import onnx
from onnx_tf.backend import prepare

# PyTorch 모델 로드
model = ForestFireDetector().eval()
model.load_state_dict(torch.load("fire_model.pth", map_location='cpu'))

# ONNX로 수출 (멀티 입력 대응)
dummy_rgb = torch.randn(1, 8, 3, 112, 112)
dummy_thermal = torch.randn(1, 8, 1, 112, 112)
onnx_path = "fire_model.onnx"

torch.onnx.export(
    model, 
    (dummy_rgb, dummy_thermal), 
    onnx_path,
    input_names=['input_rgb', 'input_thermal'],
    output_names=['output'],
    opset_version=17 # 최신 Ops 지원
)

# ONNX -> TensorFlow SavedModel 변환
onnx_model = onnx.load(onnx_path)
tf_rep = prepare(onnx_model)
tf_model_path = "./tf_fire_model"
tf_rep.export_graph(tf_model_path)

# TFLite INT8 양자화 변환
converter = tf.lite.TFLiteConverter.from_saved_model(tf_model_path)
converter.optimizations = []

# 대표 데이터셋 생성 (양자화 보정용)
def representative_data_gen():
    for _ in range(100):
        # 실제 데이터셋과 유사한 범위를 주어야 정확도가 유지됩니다.
        rgb = np.random.rand(1, 8, 3, 112, 112).astype(np.float32)
        thermal = np.random.rand(1, 8, 1, 112, 112).astype(np.float32)
        yield [rgb, thermal]

converter.representative_dataset = representative_data_gen
converter.target_spec.supported_ops = [] 
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()
with open("fire_model_int8.tflite", "wb") as f:
    f.write(tflite_model)

print("✅ 최종 INT8 양자화 모델 생성 완료: fire_model_int8.tflite")