# SubCameraApp 통합 실험 및 프로파일링 가이드

이 문서는 `SubCameraApp_refactory_260306_queue` 모듈 하나로 카메라/AI 스트리밍과 STM32 기기 제어(EdgeBridge)를 **동시에 실행하고 검증**하는 방법을 안내합니다.

---

## 🚀 1. 빠른 실행 가이드

이 프로젝트는 라즈베리파이 4(RPi4)에서 네이티브로 빌드 및 실행하는 것을 권장합니다. 기존에는 AI 모듈과 브릿지 모듈을 따로 실행해야 했지만, 이제 단일 프로세스로 동작합니다.

### 1-1. 시스템 환경 준비 요소 점검 (RPi4)
미리 라즈베리파이에서 다음 설정이 완료되어 있는지 확인하세요.
- 카메라 연결 및 /dev/video0 활성화
- UART 핀 활성화 및 하드웨어 결선 (RPi TX/RX <-> STM32 RX/TX)
- 시리얼 콘솔 접근 비활성화 (`sudo raspi-config` -> Login Shell: No, Hardware Enable: Yes)
- `python3` 설치 (옵션: 목(mock) 서버를 사용한 로컬 테스트용)

### 1-2. 통합 설정 수정
**`config/edge_device_config.json`** 파일을 열어 서버 설정 및 UART 포트를 확인합니다.
```json
{
  "serial_port": "/dev/serial0",
  "serial_baud": 115200,
  "server_host": "192.168.0.58",  // 🚨 운영/테스트 서버 주소로 변경
  "server_port": 5000,            // 🚨 브리지용 포트 번호 확인
  ...
}
```

### 1-3. 통합 빌드 및 실행
```bash
# 빌드 디렉토리 생성 및 빌드 (약간 시간이 걸릴 수 있습니다)
mkdir -p build && cd build
cmake ..
make -j4  # RPi4의 4코어 전체를 사용하여 빌드

# 루트 디렉토리에서 실행
cd ..
./build/subcam_main
```

실행 시 `[Controller] EdgeBridge 시작 실패...` 가 뜨지 않고, 정상적으로 스레드가 초기화되었다는 로그가 나와야 합니다. (실패하더라도 기존 카메라 기능은 독립적으로 동작합니다.)

---

## 🧪 2. 서버 없이 로컬 프로파일링 (Mock Server Test)

실제 서버 없이 브릿지가 제대로 데이터를 쏘고 받는지, AI 시스템과 간섭이 없는지 랩 환경에서 검증할 수 있습니다.

**터미널 A (서버 흉내):**
```bash
# 원본 Edgedevice 프로젝트의 툴을 사용 (또는 새로 복사해둔 도구 사용)
python3 ../Edgedevice_RPI4/tools/mock_tcp_server.py --port 5000
```

**터미널 B (통합 앱 실행):**
앱의 `edge_device_config.json`의 host를 `127.0.0.1`, port를 `5000`으로 바꾼 후
```bash
./build/subcam_main
```

Mock 서버(터미널 A)에서 `w/a/s/d/status/recv` 명령어를 입력해 모터 제어 명령을 내리고 센서 응답값 수신이 잘 되는지 테스트합니다. 이 동안 터미널 B는 죽지 않고 AI 처리와 카메라 스트리밍이 병행되어야 합니다.

---

## 📊 3. 작동 프로파일링 방법

하나의 프로세스로 통합되면서 가장 중요해진 것은 **간섭(Interference)**과 **자원 경합(Resource Contention)**을 확인하는 것입니다. 다음 도구들로 앱의 성능 병목을 확인하십시오.

### 3-1. CPU 및 스레드 모니터링 (`htop`)
통합 앱은 이제 최소 7개 이상의 스레드를 굴립니다 (Main, Command TCP, UDP Beacon, SystemMonitor, Gstreamer, App StreamPipeline, **새로 추가된 Bridge Main**, **Bridge ReaderLoop**).

1. 라즈베리파이 터미널 창을 하나 더 열고 `htop`을 엽니다.
2. `F2(Setup) > Display options` 에서 `Show custom thread names`와 `Tree view`를 켭니다.
3. `subcam_main` 밑에 달린 스레드들을 관찰합니다.
   - **확인 1:** 특정 스레드 하나가 코어 1개(100%)를 독점하고 있지 않은가?
   - **확인 2:** AI 추론 코어가 브리지의 I/O 블로킹 때문에 멈칫거리지 않는가? (브릿지 스레드는 대부분 `idle` 상태여야 정상입니다.)

### 3-2. 메모리 릭 점검 (`valgrind`)
통합 과정에서 스레드 종료 시 자원 누수가 발생하지 않는지 프로파일링해야 합니다.

```bash
# valgrind 설치
sudo apt install valgrind

# leak check (성능이 크게 저하되므로 기능 자체 점검용으로만 사용)
valgrind --leak-check=full --show-leak-kinds=all ./build/subcam_main
```
실행 중 `Ctrl+C`로 앱을 종료했을 때 "LEAK SUMMARY" 부분에 누수(definitely lost)가 발생하는지 점검합니다. EdgeBridge 종료 시그널링과 스레드 회수(`stop_flag`)가 완벽하게 처리된다면 누수가 없어야 합니다.

### 3-3. 애플리케이션 자체 로깅 점검 (`stm_bridge.log`)
EdgeBridge 모듈은 동작을 `./stm_bridge.log` 라는 파일에 계속 씁니다. 앱이 돌아가는 중간중간 실시간으로 해당 로그를 감시하여 타임아웃 오류가 발생하지 않는지 확인하십시오.

```bash
tail -f stm_bridge.log
```
특히 `uart read timeout`이 잦다면, 통합된 AI 모듈 때문에 CPU 스레드 스케줄링에 밀려서 UART 버퍼를 제때 처리하지 못했을 가능성을 확인해야 합니다 (`nice` 값 조정 고려).

---

## 💡 요약 및 체크리스트
- [ ] 단일 프로세스 실행 여부 (이제 두 개를 켤 필요 없음)
- [ ] AI 성능 저하 미발생 여부 (htop 기준 코어 할당 확인)
- [ ] `stop()` 시 행(hang)에 걸리지 않고 빠르게 프로세스가 끝나는지 여부
  - `EdgeBridgeModule::stop()`에 의해 recv() 블로킹 락스(lock)가 해제되는지 확인.
- [ ] 모터 제어 딜레이 (서버 -> Bridge -> UART 응답 -> Edge -> 서버)가 100ms 이내에 동작하는가?
