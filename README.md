# stm_bridge

RPi(라즈베리파이)와 STM32(UART), 서버(TCP) 사이를 중계하는 C++17 데몬입니다.

- 서버 <-> RPi: TCP 바이너리 패킷
- RPi <-> STM32: UART 프레임

---

## 1) 이 프로젝트에서 필요한 파일
다른 개발자에게 코드 전달 시 아래 파일/폴더가 필요합니다.

```text
stm_bridge/
  CMakeLists.txt
  config.json
  stm_bridge.service
  third_party/
    json.hpp
  src/
    main.cpp
    bridge.h
    bridge.cpp
    server_client.h
    server_client.cpp
    uart_port.h
    uart_port.cpp
    stm32_proto.h
    stm32_proto.cpp
  tools/
    mock_tcp_server.py
  README.md
```

`build/` 폴더는 빌드 산출물이므로 전달 필수는 아닙니다.
`third_party/json.hpp`는 프로젝트에 포함된 JSON 파서(`nlohmann/json`)입니다.

### 주요 파일 역할
| 파일 | 역할 |
|---|---|
| `src/main.cpp` | 설정 로드 후 브리지 실행 진입점 |
| `src/bridge.cpp` | 서버 패킷 ↔ STM32 UART 중계 핵심 로직 |
| `src/server_client.cpp` | TCP 패킷 송수신(Type+Len+JSON) |
| `src/uart_port.cpp` | UART 포트 오픈/설정/타임아웃 읽기 |
| `src/stm32_proto.cpp` | STM32 프레임 빌드/파싱 + JSON payload 파싱 |
| `tools/mock_tcp_server.py` | 로컬 TCP 프로토콜 테스트용 mock 서버 |

---

## 2) 새 라즈베리파이에서 최초 1회 설정

### 2-1. 패키지 설치
```bash
sudo apt update
sudo apt install -y build-essential cmake python3
```

참고: JSON 라이브러리는 `third_party/json.hpp`로 포함되어 있어 별도 apt 설치가 필요 없습니다.

### 2-2. UART(serial0) 활성화 및 시리얼 콘솔 해제
`/dev/serial0`를 앱이 사용하려면 로그인 콘솔이 점유하면 안 됩니다.

1. 설정 실행
```bash
sudo raspi-config
```

2. 메뉴에서 아래 선택
- `Interface Options` -> `Serial Port`
- "Would you like a login shell to be accessible over serial?" -> `No`
- "Would you like the serial port hardware to be enabled?" -> `Yes`

3. 재부팅
```bash
sudo reboot
```

### 2-3. UART 장치 확인
재부팅 후:
```bash
ls -l /dev/serial0
```

정상 예시: `/dev/serial0 -> ttyAMA0` 또는 `/dev/serial0 -> ttyS0`

### 2-4. 사용자 권한 확인
```bash
groups
```

`dialout` 그룹이 없으면 추가:
```bash
sudo usermod -aG dialout $USER
```

다시 로그인(또는 재부팅) 후 반영됩니다.

### 2-5. 시리얼 getty 비활성 확인(선택)
```bash
sudo systemctl status serial-getty@ttyAMA0.service --no-pager
sudo systemctl status serial-getty@serial0.service --no-pager
```

`inactive (dead)`면 정상입니다.

---

## 3) 배선 체크 (RPi <-> STM32)
기본 UART(3.3V TTL) 기준:

- RPi TXD(pin 8) -> STM32 RX
- RPi RXD(pin 10) <- STM32 TX
- GND(pin 6 등) <-> STM32 GND

주의:
- TX/RX는 교차 연결
- GND 공통 필수
- 5V TTL 직접 연결 금지

---

## 4) 빌드
프로젝트 루트에서:

```bash
cd /home/<USER>/stm_bridge
cmake -S . -B build
cmake --build build -j
```

실행 파일:
- `build/stm_bridge`

---

## 5) 설정 파일(config.json)
`config.json`을 실환경에 맞게 수정합니다.

```json
{
  "serial_port": "/dev/serial0",
  "serial_baud": 115200,
  "server_host": "127.0.0.1",
  "server_port": 9000,
  "uart_timeout_ms": 1000,
  "reconnect_initial_ms": 500,
  "reconnect_max_ms": 10000,
  "log_file": "./stm_bridge.log"
}
```

### 꼭 바꿔야 하는 항목 (CUSTOM REQUIRED)
- `server_host`
- `server_port`

### 기존 STM32 환경이면 보통 그대로 써도 되는 항목 (CUSTOM OPTIONAL)
- `serial_port` (`/dev/serial0` 유지)
- `serial_baud` (`115200` 유지)

### 환경에 따라 조정하는 항목 (CUSTOM OPTIONAL)
- `uart_timeout_ms`
- `reconnect_initial_ms`
- `reconnect_max_ms`
- `log_file`

항목 설명:
- `serial_port`: STM32 연결 UART 장치(기본 `/dev/serial0`)
- `serial_baud`: STM32 펌웨어와 동일해야 함(기본 115200)
- `server_host`, `server_port`: TCP 서버 주소/포트
- `uart_timeout_ms`: STM 응답 대기 시간(ms)
- `reconnect_initial_ms`, `reconnect_max_ms`: 서버 재접속 백오프
- `log_file`: 로그 파일 경로

---

## 6) 실행

```bash
cd /home/<USER>/stm_bridge
./build/stm_bridge ./config.json
```

별도 터미널에서 로그 확인:
```bash
tail -f /home/<USER>/stm_bridge/stm_bridge.log
```

---

## 7) systemd 서비스 등록(운영용)
매번 `./build/stm_bridge ./config.json`를 직접 실행하지 않으려면 systemd로 등록하세요.

### 7-1. 실행 파일 먼저 준비
```bash
cd /home/<USER>/stm_bridge
cmake -S . -B build
cmake --build build -j
```

### 7-2. 서비스 파일 복사
```bash
cd /home/<USER>/stm_bridge
sudo cp stm_bridge.service /etc/systemd/system/stm_bridge.service
```

### 7-3. 서비스 파일 수정 (가장 중요)
```bash
sudo nano /etc/systemd/system/stm_bridge.service
```

최소 아래 3개는 반드시 본인 환경으로 바꿉니다:
- `User=<USER>`
- `WorkingDirectory=/home/<USER>/stm_bridge`
- `ExecStart=/home/<USER>/stm_bridge/build/stm_bridge /home/<USER>/stm_bridge/config.json`

권장 예시:
```ini
[Unit]
Description=STM32 Bridge Daemon (RPi)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=suseok
WorkingDirectory=/home/suseok/stm_bridge
ExecStart=/home/suseok/stm_bridge/build/stm_bridge /home/suseok/stm_bridge/config.json
Restart=always
RestartSec=2

[Install]
WantedBy=multi-user.target
```

### 7-4. 등록/자동시작 설정
```bash
sudo systemctl daemon-reload
sudo systemctl enable stm_bridge
sudo systemctl start stm_bridge
```

### 7-5. 상태 확인
```bash
sudo systemctl status stm_bridge --no-pager
```

정상 예시:
- `Active: active (running)`

비정상 예시:
- `Active: failed`
- `ExecStart=... No such file or directory` (경로 오타)
- `Permission denied` (권한/사용자 문제)

### 7-6. 로그 확인
```bash
journalctl -u stm_bridge -f
```

### 7-7. 서비스 설정 바꾼 뒤 재반영
```bash
sudo systemctl daemon-reload
sudo systemctl restart stm_bridge
sudo systemctl status stm_bridge --no-pager
```

### 7-8. 중지/비활성화/삭제
중지:
```bash
sudo systemctl stop stm_bridge
```

자동 시작 해제:
```bash
sudo systemctl disable stm_bridge
```

서비스 파일 삭제:
```bash
sudo rm /etc/systemd/system/stm_bridge.service
sudo systemctl daemon-reload
```

---

## 8) 서버 프로토콜 (RPi <-> Server)
패킷 형식:
- `Type`: 1 byte
- `BodyLength`: 4 bytes (big-endian)
- `Body`: JSON bytes (UTF-8)

메시지 타입:
- `0x00 ACK`
- `0x03 FAIL`
- `0x04 DEVICE`
- `0x05 AVAILABLE`

브리지 수신:
- `DEVICE(0x04)` + body `{"motor":"w"}` 또는 `{"cmd":"w"}`
- `AVAILABLE(0x05)` + empty body

브리지 송신:
- `ACK(0x00)` + body `{"ok":1,"mode":"...","cmd":"..."}`
- `AVAILABLE(0x05)` + body `{"tmp":..,"hum":..,"dir":"..","tilt":..}`
- `FAIL(0x03)` + body `{"reason":"..."}`

---

## 9) STM32 UART 프로토콜 (RPi <-> STM32)
프레임 형식:
- `CMD(1) + LEN(4, big-endian) + PAYLOAD(JSON)`

요청:
- 모터: `CMD=0x04`, payload `{"motor":"w|a|s|d|auto|manual|on|off"}`
- 상태 요청: `CMD=0x05`, `LEN=0`

응답:
- 모터 ACK: `CMD=0x04`, payload `{"ok":1/0,"mode":"...","cmd":"..."}`
- 상태: `CMD=0x05`, payload `{"tmp":..,"hum":..,"dir":"L|R|-","tilt":..}`

---

## 10) 로컬 테스트(서버 없이)
Mock 서버를 띄워 브리지 TCP 프로토콜을 검증할 수 있습니다.

터미널 A:
```bash
cd /home/<USER>/stm_bridge
python3 tools/mock_tcp_server.py --port 9000
```

터미널 B:
```bash
cd /home/<USER>/stm_bridge
./build/stm_bridge ./config.json
```

Mock 명령:
- `w/a/s/d/auto/manual/on/off`: 모터 요청
- `status`: 상태 요청
- `recv`: 브리지 응답 수신
- `quit`: 종료

---

## 11) 문제 해결

### `server connect fail: Connection refused`
- 서버가 `server_host:server_port`에서 listen 중인지 확인
- `config.json`의 host/port 재확인

### `uart read timeout/header fail`
- STM32가 응답 프레임을 보내는지 확인
- TX/RX/GND 배선 재확인
- baud/8N1 일치 확인
- `uart_timeout_ms` 증가(예: 1000 -> 3000)

### `invalid motor cmd`
- 허용값만 전송: `w,a,s,d,auto,manual,on,off`
