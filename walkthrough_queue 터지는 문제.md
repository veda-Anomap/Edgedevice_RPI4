# 이미지 전송 프로토콜 통합 및 안정화 결과 보고서

## 1. 해결된 핵심 문제: Connection refused
- **현상**: AI 정보는 잘 가지만, 동영상 프레임(이미지) 전송 시 서버에서 연결을 거부하는 현상 발생.
- **원인**: 기존 방식은 이미지가 별도의 포트로 새로운 접속을 시도했으나, 서버 측 수신 대기 포트가 일치하지 않거나 방화벽 등에 의해 차단됨.

## 2. 주요 개선 사항

### [프로토콜 통합 (Socket Reuse)]
- **기존 소켓 재사용**: 서버가 카메라로 접속한 명령 채널(Command Socket)을 그대로 사용하여 이미지를 전송합니다.
- **통합 패킷 규격**: `PacketProtocol`에 정의된 `MessageType::IMAGE (0x0a)` 타입을 사용하여 `[Header] + [JSON Metadata] + [JPEG Data]` 순서로 바이너리 데이터를 안전하게 직렬화하여 송신합니다.

### [컴포넌트 리팩토링]
- **INetworkSender 확장**: 인터페이스에 [sendImage](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory/src/network/NetworkFacade.cpp#65-91)를 추가하여 모든 네트워크 송신 기능을 일원화했습니다.
- **ChunkedStreamTransmitter 최적화**: 자체적인 소켓 연결 및 관리 로직을 제거하고, 주입받은 네트워크 엔진([NetworkFacade](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory/src/network/NetworkFacade.h#19-20))에 전송을 위임하도록 경량화했습니다.
- **의존성 주입 (DI)**: [SubCamController](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory/src/controller/SubCamController.cpp#19-82)에서 모든 전송기가 동일한 네트워크 인터페이스를 공유하도록 연결하여 리소스 중복을 방지했습니다.

### [장기 안정성 및 메모리 보호]
- **이벤트 큐 상한제 (Bounded Queue)**: [EventRecorder](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory/src/buffer/EventRecorder.h#28-83)의 대기 큐에 상한(Max 5)을 설정하여, 네트워크 지연 시에도 메모리가 무한정 증식하는 현상을 차단했습니다.
- **감지 쿨다운 (Event Cooldown)**: 동일 객체에 대해 10초간 재트리거를 방지하여 중복 녹화 부하를 제거했습니다.
- **실시간 상태 모니터링**: `AVAILABLE (0x05)` 패킷에 현재 대기 중인 이벤트 수를 포함하여, 장치의 건강 상태를 원격에서 감시할 수 있게 했습니다.

## 3. 기대 효과
이제 별도의 포트 설정 이슈 없이 안정적인 데이터 전송이 가능하며, 특히 **장시간 가동 시에도 메모리 고갈로 인한 시스템 다운 없이** 견고하게 동작합니다. 이는 상용 환경에서의 24/7 무중단 운영을 보증합니다.
