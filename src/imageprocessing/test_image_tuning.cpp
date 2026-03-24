#include <ctime>
#include <iostream>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/ximgproc.hpp>
#include <string>
#include <vector>

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

    Mat laplacian, result;
    Laplacian(src, laplacian, CV_16S, 3);
    // convertScaleAbs는 음수(Laplacian 디테일의 윤곽선 한쪽)를 양수로 덮어버려 정상적인 샤프닝이 불가능하므로 제거

    src.copyTo(result);
    for (int y = 0; y < src.rows; y++) {
      for (int x = 0; x < src.cols; x++) {
        float weight = focus_map.at<float>(y, x);
        for (int c = 0; c < 3; c++) {
          // 샤프닝 공식 복구 (원본 - 라플라시안)
          result.at<Vec3b>(y, x)[c] = saturate_cast<uchar>(
              src.at<Vec3b>(y, x)[c] -
              (laplacian.at<Vec3s>(y, x)[c] * weight * strength));
        }
      }
    }
    return result;
  }
  string getName() const override { return "Gradient Ratio DFD"; }
  string getBrief() const override { return "Spatially-variant Blur Map"; }
};

// ==========================================
// 4. CVPR 2025 ELP 기반 AF 가이드 (Event-Driven)
// 근거: Bao et al., One-Step AF
// ==========================================
class DigitalELPStrategy : public IFocusStrategy {
private:
  Mat last_gray;
  double prev_elp = 0;

public:
  Mat apply(const Mat &src, float strength) override {
    Mat gray, laplacian, result = src.clone();
    cvtColor(src, gray, COLOR_BGR2GRAY);
    gray.convertTo(gray, CV_32F);

    if (last_gray.empty()) {
      last_gray = gray.clone();
      return result;
    }

    // 공간 Laplacian 정보와 시간적 Polarity(Event) 곱 산출
    Laplacian(gray, laplacian, CV_32F, 3);
    Mat diff = gray - last_gray;

    double elp_score = 0;
    Rect roi(src.cols / 2 - 100, src.rows / 2 - 100, 200,
             200); // RPi 최적화를 위한 ROI

    for (int y = roi.y; y < roi.y + roi.height; y++) {
      for (int x = roi.x; x < roi.x + roi.width; x++) {
        float d = diff.at<float>(y, x);
        if (std::abs(d) > 3.0f) { // 이벤트 생성 임계값
          float p = (d > 0) ? 1.0f : -1.0f;
          elp_score += (p * laplacian.at<float>(y, x));
        }
      }
    }

    // 부호 변이(Sign Mutation) 감지: Peak에서 Positive -> Negative
    string msg = "ELP Seeking...";
    Scalar color(0, 255, 255);
    if (prev_elp > 0 && elp_score <= 0) {
      msg = "IN FOCUS (PEAK)!";
      color = Scalar(0, 255, 0);
    } else if (elp_score < 0) {
      msg = "OUT (Passed)";
      color = Scalar(0, 0, 255);
    }

    rectangle(result, roi, color, 2);
    putText(result, msg, Point(10, 30), FONT_HERSHEY_SIMPLEX, 0.7, color, 2);

    last_gray = gray.clone();
    prev_elp = elp_score;
    return result;
  }
  string getName() const override { return "ELP AF Guide"; }
  string getBrief() const override { return "CVPR 2025: Sign Mutation"; }
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
      string capture_pipeline = "libcamerasrc ! video/x-raw, width=640, height=480, framerate=30/1 ! "
                                "videoconvert ! video/x-raw, format=BGR ! appsink drop=true sync=false";
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
  
  // 2. Trackbar Deprecated 경고 해결 (Value Pointer 대신 NULL + getTrackbarPos 사용)
  createTrackbar("Strategy(G|R|E)", "Focus Comparison System", NULL, 2, onTrackbar);
  setTrackbarPos("Strategy(G|R|E)", "Focus Comparison System", 0);
  
  createTrackbar("Strength(x0.1)", "Focus Comparison System", NULL, 50, onTrackbar);
  setTrackbarPos("Strength(x0.1)", "Focus Comparison System", 20);

  vector<unique_ptr<IFocusStrategy>> strategies;
  strategies.push_back(make_unique<GuidedSharpeningStrategy>());
  strategies.push_back(make_unique<GradientRatioStrategy>());
  strategies.push_back(make_unique<DigitalELPStrategy>());

  Mat frame, output, display;
  while (cap.read(frame)) {
    // 트랙바 안전 조회
    int g_strategy_idx = getTrackbarPos("Strategy(G|R|E)", "Focus Comparison System");
    int g_sharp_strength = getTrackbarPos("Strength(x0.1)", "Focus Comparison System");

    // [에러 수정 완료] cv::Scalar 전체 객체(m) 대신 각 채널의 배열 인덱스(m[0], m[1], m[2])를 접근해야 합니다.
    Scalar m = mean(frame);
    float cam_lux = (float)(m[0] + m[1] + m[2]) / 3.0f;

    // 선택된 전략 실행 (LSP 준수)
    float cur_strength = g_sharp_strength * 0.1f;
    output = strategies[g_strategy_idx]->apply(frame, cur_strength);

    // 결과 화면 구성 (원본 | 결과)
    Mat f_res, o_res;
    resize(frame, f_res, Size(640, 480));
    resize(output, o_res, Size(640, 480));
    hconcat(f_res, o_res, display);

    // 오버레이 정보 라벨링
    string info = strategies[g_strategy_idx]->getName() + " : " +
                  strategies[g_strategy_idx]->getBrief();
    putText(display, "ORIGINAL", Point(20, 460), FONT_HERSHEY_SIMPLEX, 0.7,
            Scalar(255, 255, 255), 2);
    putText(display, info, Point(660, 460), FONT_HERSHEY_SIMPLEX, 0.7,
            Scalar(0, 255, 0), 2);

    imshow("Focus Comparison System", display);
    if (waitKey(30) == 'q')
      break;
  }
  return 0;
}