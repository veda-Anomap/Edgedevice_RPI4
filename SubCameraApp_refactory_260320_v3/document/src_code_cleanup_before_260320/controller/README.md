# Controller Layer (System Orchestration)

## 📁 폴더 개요
본 폴더는 전체 시스템의 "중앙 관제 센터" 역할을 수행합니다. 네트워크로부터의 원격 명령을 수신하여 하드웨어를 제어하거나, 영상 스트리밍 및 AI 분석의 생명주기(Life Cycle)를 통합 관리합니다.

## 🛠 문제 해결 및 최적화
시스템의 확장성과 테스트 용이성을 위해 다음과 같은 설계 패턴이 적용되었습니다.

1.  **의존성 주입 (Dependency Injection)**:
    - `SubCamController` 생성자에서 모든 인터페이스(`ICamera`, `IAiDetector`, `INetworkSender` 등)를 주입받는 방식을 사용합니다. 이를 통해 하드웨어 없이도 Mock 객체를 사용하여 로직을 테스트할 수 있는 기반을 마련했습니다.
2.  **비동기 시스템 모니터링**:
    - 프로그램의 메인 루프를 방해하지 않도록 별도의 `monitor_thread_`를 운용하여 5초 주기로 시스템 자원(CPU, 메모리, 온도)을 측정하고 서버로 보고합니다.
3.  **Graceful Shutdown**:
    - `is_running_` 아토믹 플러그를 사용하여 모든 스레드와 네트워크 소켓을 순차적으로 안전하게 종료하는 절차를 보장합니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: `NetworkFacade`로부터 유입되는 TCP 명령 문자열.
- **Output**: 
  - `StreamPipeline`: `startStreaming` / `stopStreaming` 제어.
  - `EdgeBridgeModule`: 모터 제어 및 센서 데이터 요청 전달.
  - `NetworkFacade`: 주기적인 시스템 상태 정보(`DeviceStatus`) 전송.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **DIP (Dependency Inversion)**: 구체 클래스가 아닌 인터페이스에 의존하여 설계되어 있어 매우 우수한 확장성을 보여줍니다.
- **SRP (Single Responsibility)**: 컨트롤러는 오케스트레이션에만 집중하나, `handleServerCommand` 내부에서 직접 JSON을 파싱하는 부분은 파싱 전담 클래스로 분리할 여지가 있습니다.

### ⚠️ 코드 클린업 대상
- **Deep Nesting & Complexity**: `SubCamController::handleServerCommand` 메서드 내부의 JSON 파싱 및 예외 처리 블록이 깊게 중첩되어 있습니다. 장치 명령(`motor`, `cmd` 등) 유형에 따라 별도의 핸들러 함수(`handleMotorCommand`)로 추출하면 가독성이 크게 향상될 것입니다.
- **Long Constructor**: 컨트롤러 생성자에서 모든 컴포넌트를 초기화하는 코드가 매우 깁니다. `SubCamFactory`와 같은 팩토리 패턴 도입을 고려해볼 수 있습니다.
