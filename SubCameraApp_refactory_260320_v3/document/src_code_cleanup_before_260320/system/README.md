# System Monitoring Layer

## 📁 폴더 개요
본 폴더는 엣지 장치의 하드웨어 상태와 건강을 감시하는 역할을 수행합니다. CPU 사용량, 가용 메모리, 시스템 온도 등을 주기적으로 읽어 상위 컨트롤러에 보고합니다.

## 🛠 문제 해결 및 최적화
리소스가 제한된 라즈베리 파이 4B의 안정성을 유지하기 위해 다음과 같은 기법이 적용되었습니다.

1.  **지수 이동 평균 (EMA) 필터링**:
    - 순간적인 CPU 튀는 현상(Spike)에 의한 잘못된 알람을 방지하기 위해, 측정된 지표에 이동 평균 필터를 적용하여 부드러운 상태 값을 제공합니다.
2.  **프로세스 파일 시스템 (/proc) 파싱**:
    - 별도의 외부 라이브러리 없이 Linux 표준 `/proc` 및 `/sys` 파일을 직접 파싱하여 런타임 오버헤드를 최소화했습니다.
3.  **상태 통합 보고 (DeviceStatus)**:
    - 개별 지표를 따로 보내지 않고 `DeviceStatus` 구조체로 통합하여 전송 패킷 수를 줄이고 네트워크 효율을 높였습니다.

## 🔄 데이터 흐름 (Data Flow)
- **Input**: Linux 시스템 파일 (`/proc/stat`, `/sys/class/thermal/...`).
- **Output**: 
  - `SubCamController`: 정제된 시스템 메타데이터(`DeviceStatus`) 전달.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **SRP (Single Responsibility)**: `SystemResourceMonitor`는 데이터 수집에만 집중하며, 이를 전송하는 역할은 `Controller`로 분리되어 있어 원칙을 잘 지키고 있습니다.

### ⚠️ 코드 클린업 대상
- **Platform Dependency**: 현재 코드는 Linux 전용 파일 시스템에 강하게 의존하고 있습니다. 타 플랫폼(Windows/macOS) 지원이 필요할 경우 `IResourceMonitor` 인터페이스를 강화하고 팩토리 클래스에서 구현체를 분기하는 구조가 필요합니다.
