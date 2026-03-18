import requests
from requests.auth import HTTPDigestAuth
import time
import sys

# --- [사용자 설정 구간] ---
IP = "192.168.0.25"
USER = "admin"
PASS = "5hanwha!"

# HTTP 요청 주기 (초 단위). 예: 0.1 = 0.1초마다, 1.0 = 1초마다
CHECK_INTERVAL = 0.2 

# 모니터링할 이벤트 URL
EVENT_URL = f"http://{IP}/stw-cgi/eventstatus.cgi?msubmenu=eventstatus&action=check&Channel.1.EventType=TemperatureChangeDetection"

def start_monitoring():
    last_state = False
    print(f"[*] 모니터링 시작: {IP}")
    print(f"[*] 체크 주기: {CHECK_INTERVAL}초")
    print("[*] 종료하려면 Ctrl+C를 누르세요.\n")

    try:
        while True:
            try:
                # 1. 카메라에 이벤트 상태 요청
                response = requests.get(EVENT_URL, auth=HTTPDigestAuth(USER, PASS), timeout=1)
                
                if response.status_code == 200:
                    raw_text = response.text
                    # 전체 채널 또는 특정 리전 중 하나라도 True인지 확인
                    current_state = "True" in raw_text
                    
                    # 2. 상태가 변경되었을 때만 특별 알림 출력
                    if current_state and not last_state:
                        print(f"\n[{time.strftime('%Y-%m-%d %H:%M:%S')}] 🚨 [ALERT] 온도 감지 발생!")
                        print(f"--- 상세 응답 ---\n{raw_text.strip()}\n----------------")
                        
                    elif not current_state and last_state:
                        print(f"\n[{time.strftime('%Y-%m-%d %H:%M:%S')}] ✅ [NORMAL] 상황 종료 (정상)")

                    # 3. 실시간 동작 확인용 한 줄 출력 (폴링 진행 표시)
                    status_msg = "🔥 감지 중!!" if current_state else "Watching..."
                    sys.stdout.write(f"\r[{time.strftime('%H:%M:%S')}] {status_msg}  ")
                    sys.stdout.flush()

                    last_state = current_state
                else:
                    print(f"\n[!] HTTP 오류 발생: {response.status_code}")
                    
            except requests.exceptions.RequestException as e:
                print(f"\n[!] 연결 오류: {e}")
                time.sleep(2) # 오류 발생 시 잠시 대기 후 재시도

            # 4. 설정된 주기만큼 대기
            time.sleep(CHECK_INTERVAL)

    except KeyboardInterrupt:
        print("\n\n[*] 모니터링을 종료합니다.")

if __name__ == "__main__":
    start_monitoring()