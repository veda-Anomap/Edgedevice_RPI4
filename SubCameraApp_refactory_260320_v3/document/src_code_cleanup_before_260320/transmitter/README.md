# Stream Transmitter Layer

## 📁 폴더 개요
본 폴더는 실시간 영상 및 이벤트 녹화 결과물을 네트워크 환경에 최적화된 형태로 가공하여 서버로 전송하는 역할을 수행합니다.

## 🛠 문제 해결 및 최적화
대용량 이미지 데이터를 패킷 유실 없이 고속으로 전송하기 위해 다음과 같은 기법이 적용되었습니다.

1.  **청크 분할 전송 (Chunked Transmission)**:
    - 고해상도 JPEG 이미지는 단일 UDP 패킷 크기(MTU)를 초과할 수 있습니다. 이를 논리적 조각(Chunk)으로 나누어 전송하고, 서버에서 재조립할 수 있는 헤더를 추가하여 전송 안정성을 확보했습니다.
2.  **전송 재시도 로직**:
    - `IStreamTransmitter` 인터페이스 구현체는 전송 실패 시 정해진 횟수만큼 재시도하거나, 특정 상황에서 전송을 안전하게 취소할 수 있는 메커니즘을 포함합니다.
3.  **JPEG 품질 최적화**:
    - 대역폭과 화질 사이의 균형을 위해 `cv::imencode` 시 동적으로 품질 파라미터를 조정할 수 있도록 설계되었습니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: `StreamPipeline` 또는 `EventRecorder`로부터 전달된 정지 이미지(`cv::Mat`).
- **Output**: 
  - `network`: 가공된 바이너리 패킷 (Chunk 단위) 전달.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **OCP (Open/Closed)**: `IStreamTransmitter` 인터페이스를 통해 향후 HTTP, WebRTC 등 다른 전송 규약을 도입하더라도 `Controller`나 `Pipeline`의 수정 없이 전송기를 교체할 수 있습니다.

### ⚠️ 코드 클린업 대상
- **Memory Management**: 전송을 위해 바이너리로 변환된 벡터 데이터를 관리할 때, `std::vector`의 빈번한 할당/해제 대신 재사용 가능한 버퍼 풀 사용을 검토할 시기입니다.
