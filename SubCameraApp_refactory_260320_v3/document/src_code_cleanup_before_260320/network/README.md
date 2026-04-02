# Network Communication Layer

## 📁 폴더 개요
본 폴더는 외부 서버와의 모든 네트워크 데이터 교환을 담당합니다. TCP 명령어 수신, UDP 영상 스트리밍 송출, 그리고 장치 발견을 위한 비콘 서비스를 통합 관리하는 통신 관문입니다.

## 🛠 문제 해결 및 최적화
네트워크의 불안정성과 다양한 서버 요구사항에 대응하기 위해 다음과 같은 설계가 적용되었습니다.

1.  **네트워크 퍼사드 (Network Facade)**:
    - `CommandServer`, `BeaconService` 등 복잡한 내부 네트워크 객체들을 `NetworkFacade` 하나로 캡슐화했습니다. 외부의 `Controller`는 복잡한 소켓 로직을 몰라도 단순한 인터페이스로 통신이 가능합니다.
2.  **비차단 명령 수신 (Non-blocking Listener)**:
    - 명령 수신 서버는 별도의 스레드에서 `accept`와 `recv`를 수행하며, 수신된 데이터는 콜백 함수를 통해 즉시 상위로 전달되어 시스템 전역의 응답성을 높였습니다.
3.  **장치 자동 발견 (UDP Beacon)**:
    - 서버가 클라이언트의 IP를 일일이 알 필요 없이, 장치가 스스로 자신의 존재를 알리는 비콘 서비스를 통해 네트워크 유연성을 확보했습니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: 외부 서버로부터의 TCP/UDP 패킷.
- **Output**: 
  - `controller`: 해석된 명령어 문자열 전달.
  - 외부 서버: AI 결과물, 이미지 데이터, 장치 상태 정보 전송.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **ISP (Interface Segregation) 위반**: `NetworkFacade`가 `ICommandReceiver`와 `INetworkSender` 두 가지 명백히 다른 역할(명령 수신 vs 데이터 송신)을 동시에 수행하고 있습니다. 향후 시스템이 커지면 이를 인터페이스 수준에서 더 분리하는 것을 고려해야 합니다.

### ⚠️ 코드 클린업 대상
- **Error Handling**: 네트워크 단절 시의 재연결 로직이 각 컴포넌트(`CommandServer`, `BeaconService`)에 산재되어 있습니다. 이를 `NetworkPolicy` 등 공통 정책 관리자로 통합하면 유지보수가 쉬워질 것입니다.
