/**
 * @file test_image_tuning.cpp
 * @brief 카메라/비디오 입력 및 STM32 외부 조도 센서를 융합한 자동 톤 매핑
 * 알고리즘 테스트 (녹화(Video Recording) 기능 추가)
 */

#include <ctime>
#include <iostream>
#include <memory>
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp>
#include <string>

#include "AdvancedEnhancers.h"
#include "LowLightEnhancer.h"

using namespace cv;
using namespace std;

#include <cmath>
#include <iostream>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

//////////////////////////////////////////////////////////////////////////////////
class RingingFreeRefocus {
public:
  // 링잉 없는 적응형 리포커싱 함수
  Mat process(const Mat &src, float base_strength = 1.8f) {
    Mat gray, guided, detail, result;

    // 1. 가이드 필터 기반 베이스 레이어 추출 (링잉 방지의 핵심)
    cvtColor(src, gray, COLOR_BGR2GRAY);
    // r=4, eps=0.1^2 정도가 에지 보존과 링잉 억제의 균형점
    ximgproc::guidedFilter(src, src, guided, 4, 0.01 * 255 * 255);

    // 2. 디테일 레이어 추출 (Original - Guided)
    detail = src - guided;

    // 3. 적응형 강도 계산 (Auto-Focus Metric 활용)
    // 화면의 로컬 대비가 높을수록 강도를 높이고, 낮을수록 노이즈 억제를 위해
    // 낮춤
    Scalar mu, sigma;
    meanStdDev(gray, mu, sigma);
    float adaptive_gain = base_strength * (sigma[0] / 50.0f);
    adaptive_gain = std::min(std::max(adaptive_gain, 0.5f), 3.0f);

    // 4. 로컬 클리핑을 통한 오버슈트 제어 (ASOC 로직)
    // 링잉을 원천 차단하기 위해 원본의 로컬 상/하한선을 넘지 못하게 함
    Mat sharp = src + detail * adaptive_gain;

    Mat min_val, max_val;
    int ksize = 3;
    erode(src, min_val, getStructuringElement(MORPH_RECT, Size(ksize, ksize)));
    dilate(src, max_val, getStructuringElement(MORPH_RECT, Size(ksize, ksize)));

    // result = clamp(sharp, min_val, max_val)
    result = max(min_val, min(sharp, max_val));

    return result;
  }

  // 최신 연구 기반 포커스 측정 (ELP 응용)
  double measureFocus(const Mat &frame) {
    Mat gray, lap;
    cvtColor(frame, gray, COLOR_BGR2GRAY);
    Laplacian(gray, lap, CV_64F);
    Scalar mu, stdev;
    meanStdDev(lap, mu, stdev);
    return stdev[0] * stdev[0]; // Variance of Laplacian
  }
};

class DigitalRefocusing {
public:
  // 1. 디포커스 맵 추정 (Gradient Ratio Method)
  Mat estimateDefocusMap(const Mat &gray, double sigma_r = 1.0) {
    Mat grad_orig, grad_reblur, reblurred;

    // 원본 기울기 계산 (Sobel)
    Mat sobel_x, sobel_y;
    Sobel(gray, sobel_x, CV_32F, 1, 0, 3);
    Sobel(gray, sobel_y, CV_32F, 0, 1, 3);
    magnitude(sobel_x, sobel_y, grad_orig);

    // 재-블러 처리 (Reference Blur)
    GaussianBlur(gray, reblurred, Size(0, 0), sigma_r);
    Sobel(reblurred, sobel_x, CV_32F, 1, 0, 3);
    Sobel(reblurred, sobel_y, CV_32F, 0, 1, 3);
    magnitude(sobel_x, sobel_y, grad_reblur);

    // 비율 R 계산 및 블러 반경 추정
    Mat ratio = grad_orig / (grad_reblur + 0.001);
    Mat defocus_map;
    // sigma = sigma_r / sqrt(R^2 - 1) 수식을 단순화한 가중치 맵
    exp(-ratio, defocus_map);

    // 노이즈 제거를 위한 고속 가이드 필터 효과 (Box Filter로 대체)
    blur(defocus_map, defocus_map, Size(15, 15));
    return defocus_map;
  }

  // 2. 적응형 리포커싱 합성
  Mat applyRefocus(const Mat &src, const Mat &focus_map,
                   float strength = 2.5f) {
    Mat laplacian, sharpened, result;

    // 고역 통과 필터 (에지 성분 추출)
    Laplacian(src, laplacian, CV_16S, 3);
    convertScaleAbs(laplacian, sharpened);

    // 결과 합성: result = 원본 + (포커스맵 * 에지성분 * 강도)
    // focus_map이 높은 곳(포커스 영역)은 더 선명하게, 낮은 곳(배경)은 그대로 둠
    src.copyTo(result);
    for (int y = 0; y < src.rows; y++) {
      for (int x = 0; x < src.cols; x++) {
        float m = focus_map.at<float>(y, x);
        for (int c = 0; c < 3; c++) {
          int val = src.at<Vec3b>(y, x)[c] +
                    (sharpened.at<Vec3b>(y, x)[c] * m * strength);
          result.at<Vec3b>(y, x)[c] = saturate_cast<uchar>(val);
        }
      }
    }
    return result;
  }
};
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// GUI 트랙바 핸들링 전역 변수 설정
int g_mode =
    1; // 0: 수동 고정 파라미터 (이전 방식), 1: 융합 자동 톤매핑 알고리즘 구동
int g_stm32_lux = 150; // 실시간 STM32 UART에서 날라온다고 가정하는 하드웨어
                       // 외부 조도 센서 (0 ~ 255)
int g_manual_gamma = 50;   // 수동 모드일때 사용하는 톤매핑 감마 베이스
int g_sharp_strength = 20; // [신규] 기본값 2.0 (트랙바 값 20 / 10.0f)

void onTrackbarChanged(int, void *) {}

int main(int argc, char **argv) {
  if (argc < 2) {
    cout << "[사용법] ./test_image_tuning <비디오_파일_경로 | cam>" << endl;
    cout << "예시 (파일 시뮬레이션): ./test_image_tuning test.mp4" << endl;
    cout << "예시 (실제 V2 카메라): ./test_image_tuning cam" << endl;
    return -1;
  }

  string input_source = argv[1];
  VideoCapture cap;
  bool is_live_cam = false;

  // 1. 카메라 입력 or 동영상 파일 분기
  if (input_source == "cam" || input_source == "0") {
    is_live_cam = true;
    // RPi4 v2 카메라 GStreamer 파이프라인 (libcamerasrc 최적화)
    string capture_pipeline =
        "libcamerasrc ! video/x-raw, width=640, height=480, framerate=30/1 ! "
        "videoconvert ! "
        "videoflip method=rotate-180 ! " // 180도 회전 추가
        "video/x-raw, format=BGR ! appsink drop=true sync=false";

    cap.open(capture_pipeline, CAP_GSTREAMER);
    if (!cap.isOpened()) {
      cout << "[경고] libcamerasrc 파이프라인을 열 수 없습니다. 기본 "
              "V4L2(/dev/video0)로 폴백합니다."
           << endl;
      cap.open(0, CAP_V4L2);
    }
    cout << "[알림] 라즈베리 파이 카메라(Live) 모드로 시작합니다." << endl;
  } else {
    cap.open(input_source);
    cout << "[알림] 비디오 파일 반복 재생 모드로 시작합니다: " << input_source
         << endl;
  }

  if (!cap.isOpened()) {
    cerr << "[에러] 미디어 스트림을 열 수 없습니다!" << endl;
    return -1;
  }

  namedWindow("Hybrid Tuning (Manual vs Auto)", WINDOW_NORMAL);
  resizeWindow("Hybrid Tuning (Manual vs Auto)", 1280, 560);

  // 트랙바 부착
  createTrackbar("Mode(0:Manual, 1:Auto Dynamic)",
                 "Hybrid Tuning (Manual vs Auto)", &g_mode, 1,
                 onTrackbarChanged);
  createTrackbar("STM32 Hardware Lux Sensor", "Hybrid Tuning (Manual vs Auto)",
                 &g_stm32_lux, 255, onTrackbarChanged);
  createTrackbar("Manual Tone Gamma", "Hybrid Tuning (Manual vs Auto)",
                 &g_manual_gamma, 100, onTrackbarChanged);
  createTrackbar("Sharp Strength (x0.1)", "Hybrid Tuning (Manual vs Auto)",
                 &g_sharp_strength, 50, onTrackbarChanged);

  LowLightEnhancer lowLightEnhancer;
  DigitalRefocusing refocusing; // [추가] 리포커싱(샤프닝) 객체 생성
  Mat frame, enhanced, sharpened_final, display_frame;
  RingingFreeRefocus rfr; // 링잉 현상 해결

  // 녹화(Recording) 관련 변수
  bool is_recording = false;
  VideoWriter recorder;

  cout << "\n======================================================\n";
  cout << "[단축키 안내]\n";
  cout << " 'R' 키 : 현재 튜닝 결과 화면 녹화(비디오 저장) 시작/종료\n";
  cout << " 'S' 키 : 현재 적용된 튜닝 파라미터 콘솔에 출력\n";
  cout << " 'Q' 키 : 프로그램 종료\n";
  cout << "======================================================\n\n";

  while (true) {
    cap >> frame;
    if (frame.empty()) {
      if (is_live_cam) {
        cerr << "[경고] 카메라 프레임을 정상적으로 가져오지 못했습니다. (연결 "
                "끊김)"
             << endl;
        break;
      } else {
        // 비디오 파일이면 무한 루프 반복
        cap.set(CAP_PROP_POS_FRAMES, 0);
        continue;
      }
    }

    // 2. 카메라 영상 소프트웨어 밝기 (ISP 통과 후 데이터)
    Scalar m = mean(frame);
    float cam_lux = (float)(m[0] + m[1] + m[2]) / 3.0f;

    // 3. 통합 적응형 알고리즘 분기
    float current_gamma = 0.5f;

    if (g_mode == 0) {
      current_gamma = max(0.01f, g_manual_gamma * 0.01f);
      ToneMappingEnhancer manualTone(16, 0.01f, current_gamma);
      manualTone.enhance(frame, enhanced);
      putText(enhanced, "[Manual] Fixed ToneMapping", Point(15, 30),
              FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 0, 255), 2);
    } else {
      // 극한 저조도 판단 트리거 (STM32 센서가 30 이하 & 카메라 영상 밝기 50
      // 이하)
      if (g_stm32_lux < 30 && cam_lux < 50.0f) {
        lowLightEnhancer.enhance(frame, enhanced);
        putText(enhanced, "[Auto] Extreme Low Light Filter (LUT+CLAHE)",
                Point(15, 30), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 0, 255), 2);
      } else {
        if (cam_lux >= 180.0f) {
          current_gamma = 0.95f;
        } else if (cam_lux <= 50.0f) {
          current_gamma = 0.25f;
        } else {
          float ratio = (cam_lux - 50.0f) / 130.0f;
          current_gamma = 0.25f + ratio * 0.70f;
        }

        int adaptive_radius = (cam_lux < 100.0f) ? 24 : 16;
        ToneMappingEnhancer dynamicTone(adaptive_radius, 0.01f, current_gamma);
        dynamicTone.enhance(frame, enhanced);
        putText(enhanced, "[Auto] Dynamic Unified ToneMapping", Point(15, 30),
                FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 0, 255), 2);
      }
    }

    // 2. [신규] 적응형 샤프닝(Sharpening) 적용
    // 개선된 영상(enhanced)의 그레이스케일을 따서 포커스 맵 생성
    Mat gray;
    cvtColor(enhanced, gray, COLOR_BGR2GRAY);
    gray.convertTo(gray, CV_32F, 1.0 / 255.0); // 0~1 사이 값으로 정규화

    // 포커스 맵 추정 및 샤프닝 적용 (기존 클래스 메서드 호출)
    // Mat focus_map = refocusing.estimateDefocusMap(gray, 1.0);
    // sharpened_final = refocusing.applyRefocus(enhanced, focus_map, 2.5f); //
    // 강도 2.5 적용

    // 2. [개선] 링잉 제거 기술 적용 (트랙바 값 연동)
    float current_sharp = (float)g_sharp_strength / 10.0f;
    sharpened_final = rfr.process(enhanced, current_sharp);

    // 선명도 측정 및 출력 (현재 강도도 화면에 표시하면 좋습니다)
    double score = rfr.measureFocus(frame);
    string txt = "Focus Score: " + to_string((int)score) +
                 " | Sharp: " + to_string(current_sharp).substr(0, 3);
    putText(display_frame, txt, Point(20, 40), FONT_HERSHEY_SIMPLEX, 0.8,
            Scalar(0, 255, 0), 2);

    // 3. 3단 가로 합치기 (원본 640 | 톤매핑 640 | 샤프닝 640)
    Mat f_res, e_res, s_res;
    resize(frame, f_res, Size(640, 480));
    resize(enhanced, e_res, Size(640, 480));
    resize(sharpened_final, s_res, Size(640, 480));

    // 세 영상을 가로로 연결 (총 가로 1920)
    vector<Mat> views = {f_res, e_res, s_res};
    hconcat(views, display_frame);

    // 텍스트 라벨링
    putText(display_frame, "1. ORIGINAL", Point(20, 460), FONT_HERSHEY_SIMPLEX,
            0.8, Scalar(255, 255, 255), 2);
    putText(display_frame, "2. ENHANCED (Tone)", Point(660, 460),
            FONT_HERSHEY_SIMPLEX, 0.8, Scalar(255, 255, 255), 2);
    putText(display_frame, "3. SHARPENED (Adaptive)", Point(1300, 460),
            FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 0), 2);

    // 윈도우 크기 조정 (화면이 너무 크면 조절하세요)
    imshow("Hybrid Tuning (Origin | Tone | Sharp)", display_frame);

    // ... (녹화 로직 수정: 녹화 사이즈를 1920x480으로 맞춰야 함)
    if (is_recording && recorder.isOpened()) {
      recorder.write(display_frame);
    }

    // 5. 단축키 입력 이벤트 루프
    char key = (char)waitKey(30);
    if (key == 27 || key == 'q' || key == 'Q') {
      break;
    }
    // [신규] 'R' 키: 녹화 시작 / 종료 토글
    else if (key == 'r' || key == 'R') {
      if (is_recording) {
        is_recording = false;
        recorder.release();
        cout << "\n[알림] 영상 녹화(저장)가 완료되었습니다.\n";
      } else {
        string filename = "tuning_record_" + to_string(time(nullptr)) + ".mp4";
        // RPi 및 윈도우에서 보편적으로 재생 잘 되는 mp4v 코덱 사용
        recorder.open(filename, VideoWriter::fourcc('m', 'p', '4', 'v'), 30.0,
                      Size(1920, 480));

        if (recorder.isOpened()) {
          is_recording = true;
          cout << "\n[알림] 현재 화면 비디오 녹화를 시작합니다 ... -> "
               << filename << "\n";
        } else {
          cerr << "\n[에러] 녹화 파일 스트림을 열지 못했습니다!\n";
        }
      }
    } else if (key == 's' || key == 'S') {
      cout << "\n========================================\n";
      cout << "       [Captured Auto Dynamic States]   \n";
      cout << "========================================\n";
      cout << "Current Mode: "
           << (g_mode == 0 ? "Manual" : "Auto Dynamic Fusion") << "\n";
      cout << "Camera Target Lux: " << cam_lux << "\n";
      cout << "Hardware STM32 Lux: " << g_stm32_lux << "\n";
      if (g_mode == 1 && g_stm32_lux < 30 && cam_lux < 50.0f) {
        cout << "Triggered Algorithm: LowLightEnhancer (Extreme Mode)\n";
      } else {
        cout << "Triggered Algorithm: Unified ToneMappingEnhancer\n";
        cout << "Dynamic Gamma Base: " << current_gamma << "\n";
      }
      cout << "========================================\n\n";
    }
  }

  destroyAllWindows();
  if (recorder.isOpened()) {
    recorder.release();
  }
  cap.release();
  return 0;
}
