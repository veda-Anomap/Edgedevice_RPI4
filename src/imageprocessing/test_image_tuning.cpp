#include <ctime>
#include <iostream>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp>
#include <string>
#include <vector>
#include <algorithm>

#include "AdvancedEnhancers.h"
#include "LowLightEnhancer.h"

using namespace cv;
using namespace std;

// ==========================================
// 1. 포커스 알고리즘 인터페이스 (DIP/LSP)
// ==========================================
class IFocusStrategy {
public:
  virtual ~IFocusStrategy() = default;
  virtual Mat apply(const Mat &src, float strength) = 0;
  virtual string getName() const = 0;
  virtual string getBrief() const = 0;
};

// ==========================================
// 2. 가이드 필터 기반 링잉 억제 (Guided Sharpening)
// 근거: IEEE Access 2025, Michela Lecca
// ==========================================
class GuidedSharpeningStrategy : public IFocusStrategy {
public:
  Mat apply(const Mat &src, float strength) override {
    Mat gray, guided, result;
    if (src.channels() == 3)
      cvtColor(src, gray, COLOR_BGR2GRAY);
    else
      gray = src;

    // r=4, eps=0.01로 설정하여 링잉(Halo) 억제 최적화
    ximgproc::guidedFilter(src, src, guided, 4, 0.01 * 255 * 255);
    
    // 8-bit 뺄셈 언더플로우 방지를 위해 32F 변환 후 계산
    Mat src32f, guided32f, detail32f;
    src.convertTo(src32f, CV_32F);
    guided.convertTo(guided32f, CV_32F);
    detail32f = src32f - guided32f;

    // ASOC 로직: 로컬 최소/최대값 클램핑으로 오버슈트 방지
    Mat sharp32f = src32f + detail32f * strength;
    Mat sharp;
    sharp32f.convertTo(sharp, CV_8U);
    
    Mat min_v, max_v;
    erode(src, min_v, getStructuringElement(MORPH_RECT, Size(3, 3)));
    dilate(src, max_v, getStructuringElement(MORPH_RECT, Size(3, 3)));

    // std::max/min 과 cv::max/min 충돌을 막기 위해 cv:: 명시 결함 수정
    result = cv::max(min_v, cv::min(sharp, max_v));
    return result;
  }
  string getName() const override { return "Guided (Ringing-Free)"; }
  string getBrief() const override { return "IEEE 2025: Local Clamping"; }
};

// ==========================================
// 3. 기울기 비율 기반 디포커스 복원 (Gradient Ratio)
// 근거: Chen et al., Fast Defocus Map Estimation
// ==========================================
class GradientRatioStrategy : public IFocusStrategy {
public:
  Mat apply(const Mat &src, float strength) override {
    Mat gray, g_orig, g_reblur, reblurred;
    cvtColor(src, gray, COLOR_BGR2GRAY);
    gray.convertTo(gray, CV_32F, 1.0 / 255.0);

    // 재-블러(Reference Blur)와 원본의 기울기 비율 계산
    Mat gx, gy;
    Sobel(gray, gx, CV_32F, 1, 0);
    Sobel(gray, gy, CV_32F, 0, 1);
    magnitude(gx, gy, g_orig);

    GaussianBlur(gray, reblurred, Size(5, 5), 1.0);
    Sobel(reblurred, gx, CV_32F, 1, 0);
    Sobel(reblurred, gy, CV_32F, 0, 1);
    magnitude(gx, gy, g_reblur);

    Mat focus_map;
    exp(-(g_orig / (g_reblur + 0.001)), focus_map);
    blur(focus_map, focus_map, Size(15, 15));

    // [개선 1] Fully Auto-Parameterization (완전자동 파라미터 제어)
    // 픽셀 전체의 흐림 지도(focus_map) 평균을 구하여 절대적 블러 수치 파악
    Scalar mean_blur = mean(focus_map);
    
    // 수동 트랙바(strength)를 무시하고 블러가 심할수록 스스로 샤프닝 강도를 높이는 자율주행 공식
    float auto_strength = (float)mean_blur[0] * 8.0f; 

    Mat laplacian, result;
    Laplacian(src, laplacian, CV_16S, 3);

    src.copyTo(result);
    for (int y = 0; y < src.rows; y++) {
      for (int x = 0; x < src.cols; x++) {
        float weight = focus_map.at<float>(y, x);
        for (int c = 0; c < 3; c++) {
          result.at<Vec3b>(y, x)[c] = saturate_cast<uchar>(
              src.at<Vec3b>(y, x)[c] -
              (laplacian.at<Vec3s>(y, x)[c] * weight * auto_strength));
        }
      }
    }
    return result;
  }
  string getName() const override { return "Gradient Ratio (Auto)"; }
  string getBrief() const override { return "Fully Auto Strength via Blur Map"; }
};

// ==========================================
// 4. CVPR 2025 ELP 기반 AF 가이드 (Event-Driven)
// 근거: Bao et al., One-Step AF
// ==========================================
class DigitalELPStrategy : public IFocusStrategy {
private:
  Mat last_gray;

public:
  Mat apply(const Mat &src, float strength) override {
    Mat gray, laplacian, result = src.clone();
    cvtColor(src, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, gray, Size(3, 3), 0.5);
    gray.convertTo(gray, CV_32F);

    if (last_gray.empty()) {
      last_gray = gray.clone();
      return result;
    }

    Laplacian(gray, laplacian, CV_32F, 3);
    
    // [개선 2] 고정 초점 카메라를 위한 ELP 기반 "이벤트 모션 타겟 디블러링"
    // 모터가 없는 렌즈에서 시간차(diff)는 곧 '움직이는 물체(블러 잔상)'를 뜻함.
    // 이를 역이용해 움직이는 객체에만 이벤트 가중치를 주어 핀포인트 모션 샤프닝을 수행!
    Mat diff;
    absdiff(gray, last_gray, diff);
    
    Mat event_weight;
    // 노이즈(배경)는 죽이고 큰 움직임(이벤트)만 통과시킴
    threshold(diff, event_weight, 5.0, 1.0, THRESH_TOZERO);
    event_weight = event_weight / 20.0f; 
    blur(event_weight, event_weight, Size(9, 9));

    // 최적화를 위해 BGR 3채널에 단일 Grayscale 라플라시안 스칼라 연산 적용
    for (int y = 0; y < src.rows; y++) {
      for (int x = 0; x < src.cols; x++) {
        float w = std::min(1.0f, event_weight.at<float>(y, x));
        
        // 피사체가 움직이는 곳(w > 0)에만 극강의 ELP 샤프닝을 걸어 잔상(Motion Blur) 억제
        if (w > 0.05f) {
            for (int c = 0; c < 3; c++) {
              float orig = src.at<Vec3b>(y, x)[c];
              result.at<Vec3b>(y, x)[c] = saturate_cast<uchar>(orig - (laplacian.at<float>(y, x) * w * strength * 2.0f));
            }
        }
      }
    }

    last_gray = gray.clone();
    return result;
  }
  string getName() const override { return "ELP Motion Deblur"; }
  string getBrief() const override { return "CVPR 2025: Event-Guided Sharpening"; }
};

// ==========================================
// 5. 메인 시스템 및 GUI 제어
// ==========================================
void onTrackbar(int, void*) {}

int main(int argc, char **argv) {
  if (argc < 2) {
    cout << "Usage: ./test_image_tuning <cam|video_file>" << endl;
    return -1;
  }
  
  VideoCapture cap;
  string input_val = string(argv[1]);
  if (input_val == "cam") {
      // 1. 라즈베리 파이 GStreamer 메모리 할당 제한 에러(v4l2src0) 수정
      // V4L2 대신 최상위 libcamerasrc 엔진 사용으로 하드웨어 충돌 방지
      // 카메라 180도 회전(videoflip) 추가
      string capture_pipeline = "libcamerasrc ! video/x-raw, width=640, height=480, framerate=30/1 ! "
                                "videoconvert ! videoflip method=rotate-180 ! video/x-raw, format=BGR ! appsink drop=true sync=false";
      cap.open(capture_pipeline, CAP_GSTREAMER);
      
      // 만약 권한 거부 시 안전한 fallback
      if (!cap.isOpened()) {
          cout << "[WARNING] libcamerasrc failed. Falling back to default V4L2 /dev/video0\n";
          cap.open(0, CAP_V4L2);
      }
  } else {
      cap.open(input_val);
  }

  if (!cap.isOpened()) {
    cerr << "[ERROR] Cannot open camera or file." << endl;
    return -1;
  }

  namedWindow("Focus Comparison System", WINDOW_NORMAL);
  resizeWindow("Focus Comparison System", 1280, 560);
  
  // ==========================================
  // 통합 트랙바 렌더링
  // ==========================================
  createTrackbar("Tone Mode (0:M, 1:A)", "Focus Comparison System", NULL, 1, onTrackbar);
  setTrackbarPos("Tone Mode (0:M, 1:A)", "Focus Comparison System", 1);
  createTrackbar("STM32 Lux", "Focus Comparison System", NULL, 255, onTrackbar);
  setTrackbarPos("STM32 Lux", "Focus Comparison System", 150);
  createTrackbar("Tone Gamma", "Focus Comparison System", NULL, 100, onTrackbar);
  setTrackbarPos("Tone Gamma", "Focus Comparison System", 50);

  createTrackbar("Strategy(G|R|E)", "Focus Comparison System", NULL, 2, onTrackbar);
  setTrackbarPos("Strategy(G|R|E)", "Focus Comparison System", 0);
  createTrackbar("Sharp Strength", "Focus Comparison System", NULL, 50, onTrackbar);
  setTrackbarPos("Sharp Strength", "Focus Comparison System", 20);

  // 알고리즘 객체 생성
  vector<unique_ptr<IFocusStrategy>> strategies;
  strategies.push_back(make_unique<GuidedSharpeningStrategy>());
  strategies.push_back(make_unique<GradientRatioStrategy>());
  strategies.push_back(make_unique<DigitalELPStrategy>());
  
  LowLightEnhancer lowLightEnhancer;

  bool is_recording = false;
  VideoWriter recorder;

  cout << "\n======================================================\n";
  cout << "[단축키 안내]\n";
  cout << " 'R' 키 : 현재 화면 녹화(비디오 저장) 시작/종료\n";
  cout << " 'C' 또는 'P' 키 : 현재 화면 사진(이미지) 캡쳐 및 저장\n";
  cout << " 'Q' 키 : 프로그램 종료\n";
  cout << "======================================================\n\n";

  Mat frame, output, display;
  while (cap.read(frame)) {
    // 트랙바 안전 조회 (동적 파라미터)
    int t_mode = getTrackbarPos("Tone Mode (0:M, 1:A)", "Focus Comparison System");
    int t_lux = getTrackbarPos("STM32 Lux", "Focus Comparison System");
    int t_gamma = getTrackbarPos("Tone Gamma", "Focus Comparison System");
    int g_strategy_idx = getTrackbarPos("Strategy(G|R|E)", "Focus Comparison System");
    int g_sharp_strength = getTrackbarPos("Sharp Strength", "Focus Comparison System");

    // ==========================================
    // 1. 조도 기반 톤매핑(Toneup) 필터 파이프라인
    // ==========================================
    Mat enhanced;
    Scalar m = mean(frame);
    float cam_lux = (float)(m[0] + m[1] + m[2]) / 3.0f;
    float current_gamma = 0.5f;

    if (t_mode == 0) {
        current_gamma = std::max(0.01f, (float)t_gamma * 0.01f);
        ToneMappingEnhancer manualTone(16, 0.01f, current_gamma);
        manualTone.enhance(frame, enhanced);
    } else {
        if (t_lux < 30 && cam_lux < 50.0f) {
            lowLightEnhancer.enhance(frame, enhanced);
            current_gamma = -1.0f; // 극한 저조도 (LUT+CLAHE)
        } else {
            if (cam_lux >= 180.0f) current_gamma = 0.95f; 
            else if (cam_lux <= 50.0f) current_gamma = 0.25f; 
            else current_gamma = 0.25f + ((cam_lux - 50.0f) / 130.0f) * 0.70f;
            
            int adaptive_radius = (cam_lux < 100.0f) ? 24 : 16; 
            ToneMappingEnhancer dynamicTone(adaptive_radius, 0.01f, current_gamma);
            dynamicTone.enhance(frame, enhanced);
        }
    }

    // ==========================================
    // 2. 포커스(샤프닝) 전략 적용
    // (이전과 달리 원본이 아니라 밝아진 enhanced 프레임에 적용)
    // ==========================================
    float cur_strength = g_sharp_strength * 0.1f;
    output = strategies[g_strategy_idx]->apply(enhanced, cur_strength);

    // ==========================================
    // 3. 수치적 초점 평가 스코어 추출 (Variance of Laplacian)
    // ==========================================
    Mat gray, lap;
    cvtColor(output, gray, COLOR_BGR2GRAY);
    Laplacian(gray, lap, CV_32F);
    Scalar mean_L, stddev_L;
    meanStdDev(lap, mean_L, stddev_L);
    double focus_score = stddev_L[0] * stddev_L[0];

    // ==========================================
    // 4. GUI & 렌더링 구성
    // ==========================================
    Mat f_res, o_res;
    resize(frame, f_res, Size(640, 480));
    resize(output, o_res, Size(640, 480));
    hconcat(f_res, o_res, display);

    // 오버레이 정보 라벨링 (왼쪽: 톤매핑 정보, 오른쪽: 포커스 정보)
    string t_info = (t_mode == 0) ? format("[Manual Tone] Gamma: %.2f", current_gamma) 
                                  : format("[Auto Tone] Gamma: %.2f | Cam: %d, STM: %d", current_gamma, (int)cam_lux, t_lux);
    
    string f_info = strategies[g_strategy_idx]->getName();
    string score_info = format("Focus Score: %.1f", focus_score);

    putText(display, t_info, Point(20, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
    putText(display, f_info, Point(660, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 0), 2);
    
    // 포커스 점수 메트릭 추가
    putText(display, score_info, Point(660, 60), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 255, 255), 2);

    if (is_recording && recorder.isOpened()) {
        putText(display, "REC O", Point(1150, 40), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 0, 255), 3);
        recorder.write(display);
    }

    imshow("Focus Comparison System", display);
    
    // ==========================================
    // 5. 키보드 입력 및 녹화 로직 제어
    // ==========================================
    char key = (char)waitKey(30);
    if (key == 'q' || key == 'Q' || key == 27) {
      break;
    } else if (key == 'r' || key == 'R') {
      if (is_recording) {
        is_recording = false;
        recorder.release();
        cout << "\n[알림] 영상 녹화(저장)가 완료되었습니다.\n";
      } else {
        string filename = "tuning_record_" + to_string(time(nullptr)) + ".mp4";
        recorder.open(filename, VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0, Size(1280, 480));
        if (recorder.isOpened()) {
          is_recording = true;
          cout << "\n[알림] 화면 비디오 녹화를 시작합니다 -> " << filename << "\n";
        } else {
          cerr << "\n[에러] 녹화 파일 스트림을 열지 못했습니다!\n";
        }
      }
    } else if (key == 'c' || key == 'C' || key == 'p' || key == 'P') {
      string filename = "photo_capture_" + to_string(time(nullptr)) + ".jpg";
      imwrite(filename, display);
      cout << "\n[찰칵] 1280x480 화면 전체 사진 캡쳐 완료 -> " << filename << "\n";
    }
  }
  return 0;
}