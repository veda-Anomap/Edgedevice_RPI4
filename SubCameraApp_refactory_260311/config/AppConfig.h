#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <string>
#include <utility>
#include <vector>

// ==============================================
// 중앙 집중식 설정 (매직 넘버 제거)
// 모든 상수/임계값을 한 곳에서 관리
// ==============================================

namespace AppConfig {

// --- 네트워크 설정 ---
constexpr int UDP_BEACON_PORT = 10001;
constexpr int TCP_LISTEN_PORT = 5000;
inline const std::string BEACON_MESSAGE = "SUB_PI_ALIVE";
constexpr int BEACON_INTERVAL_SEC = 1;

// --- 로컬 모니터링 (UI) 설정 ---
constexpr bool ENABLE_DISPLAY =
    false; // GUI 없는 환경(sudo, SSH)에서 실행 시 false 지정

// --- 카메라 / 영상 설정 ---
constexpr int FRAME_WIDTH = 640;
constexpr int FRAME_HEIGHT = 480;
constexpr int CAPTURE_WIDTH = 1920;
constexpr int CAPTURE_HEIGHT = 1080;
constexpr int FPS_TARGET = 30;
constexpr int BITRATE = 4000;

// --- AI 모델 설정 ---
constexpr int MODEL_INPUT_SIZE = 640;
constexpr int NUM_THREADS =
    3; // RPi 4 코어(4개) 중 1개는 GStreamer/OS용으로 비워둠
inline const std::string MODEL_PATH = "yolo26n-pose_int8.tflite";
constexpr float CONFIDENCE_THRESHOLD = 0.5f;
constexpr int MAX_DETECTIONS = 100;
constexpr int DETECTION_STRIDE = 57; // 모델 출력 stride (per detection)
constexpr int NUM_KEYPOINTS = 17;
constexpr int AI_INFERENCE_INTERVAL =
    5; // N프레임당 1번만 AI 연산 (CPU 부하 조절용, 기본 3)

// --- 낙상 감지 임계값 ---
constexpr float FALL_VELOCITY_THRESHOLD = 18.0f;
constexpr float SIDE_FALL_ASPECT_RATIO = 0.9f;
constexpr float SIDE_FALL_ANGLE = 45.0f;
constexpr float FRONTAL_FALL_COMPRESSION = 0.45f;
constexpr float FRONTAL_FALL_ANGLE = 50.0f;
constexpr float DYNAMIC_FALL_ANGLE = 55.0f;
constexpr float SITTING_ANGLE = 50.0f;
constexpr int VOTE_WINDOW_SIZE = 6;
constexpr int VOTE_THRESHOLD = 2;
constexpr int CY_HISTORY_SIZE = 10;

// --- 트래커 설정 ---
constexpr float TRACKER_MAX_DISTANCE = 50.0f;

// --- 스켈레톤 연결 쌍 (COCO 17 keypoints) ---
inline const std::vector<std::pair<int, int>> KPT_SKELETON = {
    {16, 14}, {14, 12}, {17, 15}, {15, 13}, {12, 13}, {6, 12}, {7, 13},
    {6, 7},   {6, 8},   {7, 9},   {8, 10},  {9, 11},  {2, 3},  {1, 2},
    {1, 3},   {2, 4},   {3, 5},   {4, 6},   {5, 7}};

// --- 파일 저장 설정 ---
inline const std::string CAPTURES_DIR = "captures";

// --- [Phase 2] 이벤트 버퍼 설정 ---
constexpr int PRE_EVENT_SEC = 2;  // 이벤트 전 녹화 시간 (초) → 2프레임
constexpr int POST_EVENT_SEC = 3; // 이벤트 프레임 + 후 2초 (초) → 3프레임
constexpr int EVENT_SAMPLING_FPS =
    1; // 초당 몇 장을 보낼 것인가 (1이면 1초에 1장)
// 버퍼 용량 = PRE_EVENT_SEC × FPS_TARGET (예: 2×30 = 60 프레임)
// 총 전송 프레임 = PRE(2) + POST(3) = 5프레임

// --- [Phase 2] 스트림 전송기 설정 ---
constexpr int TRANSMITTER_MAX_RETRIES = 3; // 최대 재시도 횟수
constexpr int JPEG_QUALITY = 85;           // JPEG 인코딩 품질 (0-100)
constexpr int EVENT_CLIP_PORT_OFFSET = 1;  // 이벤트 클립 전송 포트 오프셋

// --- [Stability] 자원 보호 설정 ---
constexpr int EVENT_RECORDER_MAX_PENDING = 5; // 대기 큐 최대 건수 (메모리 보호)
constexpr int EVENT_COOLDOWN_SEC = 10; // 동일 객체 낙상 재감지 쿨다운 (초)

} // namespace AppConfig

#endif // APP_CONFIG_H
