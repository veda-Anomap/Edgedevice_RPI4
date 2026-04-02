# Edge Device Layer (Hardware Interface)

## 📁 폴더 개요
본 폴더는 센서 및 모터 제어를 담당하는 STM32 마이크로컨트롤러와의 하위 레벨 통신을 전담합니다. UART 통신 프로토콜을 통해 하드웨어 명령을 내리고 상태를 수신하는 "하드웨어 브릿지" 역할을 수행합니다.

## 🛠 문제 해결 및 최적화
불안정한 시리얼 통신 환경에서도 데이터 무결성을 유지하기 위해 다음과 같은 기법이 적용되었습니다.

1.  **Read-twice, Resend-once 전략**:
    - UART 수신 시 한 번의 에러로 데이터가 유실되지 않도록 재시도 로직을 구현하여 통신의 신뢰성을 높였습니다.
2.  **데이터 무결성 검증 (Checksum)**:
    - `stm32_proto` 클래스에서 패킷 단위의 체크섬을 확인하여 오류가 있는 패킷은 상위 레이어로 전달되기 전 차단합니다.
3.  **DIP 기반 네트워크 연동**:
    - 하드웨어 모듈이 네트워크 패브릭에 직접 의존하지 않도록 `INetworkSender` 인터페이스를 주입받아 센서 데이터를 전송합니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: `SubCamController`로부터의 JSON 형식 제어 명령.
- **Output**: 
  - 물리적 장치: UART를 통한 바이너리 명령 전달.
  - `SubCamController`: 센서 데이터 및 실행 결과 보고.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **SRP (Single Responsibility)**: `UartPort`는 저수준 입출력을, `Stm32Proto`는 프로토콜 해석을, `EdgeBridgeModule`은 상위 비즈니스 로직을 담당하여 역할 분담이 매우 명확합니다.

### ⚠️ 코드 클린업 대상
- **Error Propagation**: UART 오픈 실패나 체크섬 에러 시 단순히 로그를 남기는 것 외에, 상위 `Controller`로 에러 상태를 명확히 전파하여 UI 등에 경고를 띄울 수 있는 구조가 보강되면 더 좋을 것입니다.
