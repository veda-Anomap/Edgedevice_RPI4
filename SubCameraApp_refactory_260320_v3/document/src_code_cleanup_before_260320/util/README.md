# Utility Layer (Common Utils & Guards)

## 📁 폴더 개요
본 폴더는 시스템의 안정성과 견고성을 뒷받침하는 공통 유틸리티와 안전장치(Guards)를 포함합니다. 로깅, 성능 측정, 스레드 안전 큐 등을 제공합니다.

## 🛠 문제 해결 및 최적화
하드웨어 충돌과 같은 치명적인 런타임 위험을 감수하기 위해 DOCUMENT (`requestwrap_analysis.md`)에 기반한 강력한 도구들이 포함되었습니다.

1.  **ProcessGuard (Stability Safety Net)**:
    - **좀비 프로세스 정리**: 이전 실행 시 비정상 종료된 프로세스가 카메라 자원을 점유하는 것을 방지하기 위해 프로그램 시작 시 `/proc`을 스캔하여 구버전을 자동 정리합니다.
    - **장치 점유 선행 검사**: 카메라를 열기 전 `/dev/video0`의 파일 디스크립터 점유 여부를 Linux 커널 수준에서 확인하여 하드웨어 충돌에 의한 GStreamer 크래시를 원천 차단합니다.
2.  **Thread Safe Queue (Lock-free Design Target)**:
    - 다수의 스레드가 경쟁하는 환경에서 뮤텍스와 조건 변수를 활용하여 데드락(Deadlock) 없는 안전한 데이터 교환을 보장합니다.
3.  **성능 프로파일링 (PerformanceMonitor)**:
    - `PERF_AI`, `PERF_PRE`, `FPS` 등 주요 지표를 중앙 집중식으로 관리하고 리얼타임 통계를 제공하여 병목 지점을 쉽게 파악할 수 있게 돕습니다.

## 🔄 데이터 흐름 (Data Flow)
- **Cross-cutting Concern**: 전용 데이터 흐름보다는 모든 폴더에서 서비스 로직(로그 기록, 큐 삽입, 자원 확인)에 호출되는 보조 역할을 수행합니다.

---

## 🔍 코드 품질 분석 (Code Review)

### ✅ SOLID 원칙 점검
- **SRP (Single Responsibility)**: `Logger`는 로그만, `ProcessGuard`는 자원 보호만 담당하여 책임이 명확히 분리되어 있습니다.

### ⚠️ 코드 클린업 대상
- **Logger Categories**: 현재 로깅 카테고리가 문자열 TAG에 의존하고 있습니다. 이를 Enum 기반 카테고리로 관리하면 타입 안정성을 높이고 대규모 로그 필터링 시 성능상 이점이 있을 것입니다.
- **ProcessGuard Regex**: 현재 프로세스 검색 시 문자열 매칭 방식이 단순합니다. 정규표현식을 강화하거나 소유자(User) 확인 로직을 추가하여 정교함을 높일 수 있습니다.
