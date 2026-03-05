# stm_bridge

Raspberry Pi에서 서버와 STM32(UART) 사이를 중계하는 C++17 데몬입니다.

## 기능
- 서버 <-> RPi <-> STM32 양방향 브리지
- UART 프레임: `CMD(1) + LEN(4, big-endian) + PAYLOAD(JSON)`
- 서버 프로토콜: TCP JSON Lines(`\n` 구분)
- UART 읽기 타임아웃
- 서버 재접속(지수 백오프)
- 잘못된 LEN/JSON 에러 처리 및 서버 에러 relay
- 로그 파일 출력

## 빌드
필수: `g++`, `cmake`

```bash
cd /home/suseok/stm_bridge
cmake -S . -B build
cmake --build build -j
```

실행 파일: `build/stm_bridge`

## 실행
```bash
cd /home/suseok/stm_bridge
./build/stm_bridge ./config.json
```

## 설정 파일
`config.json` 예시:

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

## 프로토콜
### STM32 UART
- 요청:
  - 상태 요청: `CMD=0x05`, `LEN=0`
  - 모터 명령: `CMD=0x04`, `PAYLOAD={"motor":"w|a|s|d|auto|manual|on|off|o|f"}`
- 응답:
  - 상태 JSON(`CMD=0x05`): `{"tmp":..,"hum":..,"dir":"L|R|-","tilt":..}`
  - ACK JSON(`CMD=0x04`): `{"ok":1/0,"mode":"...","cmd":"..."}`

### 서버(JSON Lines)
- 서버 -> 브리지:
  - `{"type":"motor","cmd":"w"}`
  - `{"type":"status_req"}`
- 브리지 -> 서버:
  - `{"type":"status","tmp":..,"hum":..,"dir":"..","tilt":..}`
  - `{"type":"motor_ack","ok":1,"mode":"..","cmd":".."}`
  - `{"type":"error","reason":"..."}`

## systemd 등록
`stm_bridge.service`를 `/etc/systemd/system/stm_bridge.service`로 복사 후:

```bash
sudo systemctl daemon-reload
sudo systemctl enable stm_bridge
sudo systemctl start stm_bridge
sudo systemctl status stm_bridge
```

`ExecStart`, `WorkingDirectory`, `User`는 배포 경로/사용자에 맞게 수정하세요.
