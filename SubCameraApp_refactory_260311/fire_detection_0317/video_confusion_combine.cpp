#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <vector>
#include <chrono>
#include <numeric>

using namespace cv;
using namespace std;

// -----------------------------------------------------------------------------
// Shared Thread-Safe Video Streamer (Producer)
// -----------------------------------------------------------------------------
class VideoStream {
private:
    std::string url;
    std::string name;
    VideoCapture cap;
    Mat latest_frame;
    std::mutex mtx;
    std::atomic<bool> stopped;
    std::thread worker;

    void update_loop() {
        int retries = 0;
        while (!stopped) {
            if (!cap.isOpened()) {
                int wait_time = std::min(60, 1 << (retries + 1));
                std::cout << "[!] " << name << " connection lost. Retrying in " << wait_time << "s...\n";
                std::this_thread::sleep_for(std::chrono::seconds(wait_time));
                cap.open(url, CAP_GSTREAMER);
                retries++;
                continue;
            }

            Mat frame;
            if (cap.read(frame) && !frame.empty()) {
                std::lock_guard<std::mutex> lock(mtx);
                latest_frame = frame.clone();
                retries = 0;
            } else {
                cap.release();
            }
        }
    }

public:
    VideoStream(const std::string& stream_url, const std::string& stream_name) 
        : url(stream_url), name(stream_name), stopped(false) {
        // Enforce zero-latency GST pipeline
        std::string gst_pipeline = "rtspsrc location=" + url + " latency=0 tcp-timeout=5000000 ! rtph264depay ! h264parse ! v4l2slh264dec ! videoconvert ! appsink sync=false";
        cap.open(gst_pipeline, CAP_GSTREAMER);
    }

    void start() {
        worker = std::thread(&VideoStream::update_loop, this);
    }

    bool read(Mat& out_frame) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!latest_frame.empty()) {
            out_frame = latest_frame.clone();
            return true;
        }
        return false;
    }

    void stop() {
        stopped = true;
        if (worker.joinable()) {
            worker.join();
        }
        if (cap.isOpened()) cap.release();
    }
};

// -----------------------------------------------------------------------------
// Precision Aligner (Asynchronous ECC)
// -----------------------------------------------------------------------------
class PrecisionAligner {
private:
    Mat warp_matrix;
    std::mutex mtx;
    std::atomic<bool> is_processing;
    std::atomic<float> current_cc;
    Size target_size;

    Mat scale_matrix(const Mat& H, float sx, float sy) {
        Mat H_out = H.clone();
        H_out.at<float>(0, 2) *= sx;
        H_out.at<float>(1, 2) *= sy;
        return H_out;
    }

    void update_task(Mat rgb_gray, Mat thermal_gray) {
        is_processing = true;
        try {
            Mat H_current;
            {
                std::lock_guard<std::mutex> lock(mtx);
                H_current = warp_matrix.clone();
            }

            // Downsample and compute gradient magnitude to avoid local minima
            Size small_size(target_size.width / 2, target_size.height / 2);
            Mat r_small, t_small, r_blur, t_blur, grad_r, grad_t;
            
            resize(rgb_gray, r_small, small_size);
            resize(thermal_gray, t_small, small_size);
            
            GaussianBlur(r_small, r_blur, Size(5, 5), 0);
            GaussianBlur(t_small, t_blur, Size(5, 5), 0);
            
            // Scharr gradient
            Mat gx, gy;
            Scharr(r_blur, gx, CV_32F, 1, 0);
            Scharr(r_blur, gy, CV_32F, 0, 1);
            magnitude(gx, gy, grad_r);
            normalize(grad_r, grad_r, 0, 255, NORM_MINMAX, CV_8U);

            Scharr(t_blur, gx, CV_32F, 1, 0);
            Scharr(t_blur, gy, CV_32F, 0, 1);
            magnitude(gx, gy, grad_t);
            normalize(grad_t, grad_t, 0, 255, NORM_MINMAX, CV_8U);

            Mat H_level = scale_matrix(H_current, 0.5f, 0.5f);
            double cc = findTransformECC(grad_r, grad_t, H_level, MOTION_AFFINE,
                                         TermCriteria(TermCriteria::EPS | TermCriteria::COUNT, 30, 1e-4));
            
            Mat best_H = scale_matrix(H_level, 2.0f, 2.0f);
            
            std::lock_guard<std::mutex> lock(mtx);
            if (cc > 0.1) warp_matrix = best_H.clone();
            current_cc = static_cast<float>(cc);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mtx);
            current_cc = 0.0f;
        }
        is_processing = false;
    }

public:
    PrecisionAligner(Size tgt_size) : target_size(tgt_size), is_processing(false), current_cc(1.0f) {
        warp_matrix = Mat::eye(2, 3, CV_32F);
    }

    void trigger_update(const Mat& rgb_gray, const Mat& thermal_gray) {
        if (!is_processing) {
            std::thread(&PrecisionAligner::update_task, this, rgb_gray.clone(), thermal_gray.clone()).detach();
        }
    }

    Mat get_warp_matrix(float& cc) {
        std::lock_guard<std::mutex> lock(mtx);
        cc = current_cc;
        return warp_matrix.clone();
    }
    
    bool processing() const { return is_processing; }
};

// -----------------------------------------------------------------------------
// Fire Detector (Temporal + Dual-Modality)
// -----------------------------------------------------------------------------
class FireDetector {
private:
    std::deque<bool> history;
    size_t history_len;

public:
    FireDetector(size_t hist_len = 15) : history_len(hist_len) {}

    bool detect(const Mat& fused_rgb, const Mat& thermal_gray, std::vector<Rect>& fire_rects) {
        fire_rects.clear();
        
        // 1. Thermal Hotspot Mask
        Mat hot_mask;
        threshold(thermal_gray, hot_mask, 180, 255, THRESH_BINARY);
        
        // 2. RGB Colour Verification (HSV)
        Mat hsv, color_mask;
        cvtColor(fused_rgb, hsv, COLOR_BGR2HSV);
        inRange(hsv, Scalar(0, 50, 150), Scalar(35, 255, 255), color_mask);
        
        // Intersection
        Mat intersection;
        bitwise_and(hot_mask, color_mask, intersection);
        
        int current_area = countNonZero(intersection);
        bool valid_frame = (current_area > 30);
        
        history.push_back(valid_frame);
        if (history.size() > history_len) history.pop_front();
        
        int confirmed_count = std::accumulate(history.begin(), history.end(), 0);
        bool confirmed_fire = confirmed_count >= (history_len * 0.6);
        
        if (confirmed_fire) {
            std::vector<std::vector<Point>> contours;
            findContours(intersection, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
            for (const auto& cnt : contours) {
                if (contourArea(cnt) > 20) {
                    fire_rects.push_back(boundingRect(cnt));
                }
            }
        }
        
        return confirmed_fire;
    }
};

// -----------------------------------------------------------------------------
// Main Application (Renderer + Consumer Thread)
// -----------------------------------------------------------------------------
void enhance_thermal(const Mat& thermal_gray, Mat& out_enhanced) {
    Mat normalized;
    normalize(thermal_gray, normalized, 0, 255, NORM_MINMAX, CV_8U);
    Ptr<CLAHE> clahe = createCLAHE(3.0, Size(8, 8));
    clahe->apply(normalized, out_enhanced);
}

int main() {
    std::string user = "admin";
    std::string pass = "5hanwha!";
    std::string ip = "192.168.0.112";
    
    std::string rgb_url = "rtsp://" + user + ":" + pass + "@" + ip + "/0/profile2/media.smp";
    std::string th_url = "rtsp://" + user + ":" + pass + "@" + ip + "/1/profile2/media.smp";

    std::cout << "[*] Starting Fast C++ Streams (Combined Edition)...\n";
    VideoStream vs_rgb(rgb_url, "RGB");
    VideoStream vs_th(th_url, "Thermal");
    vs_rgb.start();
    vs_th.start();

    std::this_thread::sleep_for(std::chrono::seconds(2));

    Size target_size(640, 360);
    PrecisionAligner aligner(target_size);
    FireDetector detector(15);

    Mat frame_rgb, frame_th;
    uint64_t frame_count = 0;
    
    auto t_start = std::chrono::steady_clock::now();
    float fps = 15.0f;

    // Preallocate
    Mat r_img, t_raw, r_gray, t_gray, reg_t_gray;
    
    while (true) {
        if (!vs_rgb.read(frame_rgb) || !vs_th.read(frame_th)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        resize(frame_rgb, r_img, target_size);
        resize(frame_th, t_raw, target_size);
        
        cvtColor(r_img, r_gray, COLOR_BGR2GRAY);
        cvtColor(t_raw, t_gray, COLOR_BGR2GRAY);

        float cc;
        Mat current_warp = aligner.get_warp_matrix(cc);

        if (!aligner.processing()) {
            if (frame_count % 15 == 0 || cc < 0.3f) {
                aligner.trigger_update(r_gray, t_gray);
            }
        }

        warpAffine(t_gray, reg_t_gray, current_warp, target_size, INTER_LINEAR | WARP_INVERSE_MAP);

        std::vector<Rect> fire_rects;
        bool is_fire = detector.detect(r_img, reg_t_gray, fire_rects);

        Mat enhanced_t, reg_t_color, blend_mask;
        enhance_thermal(reg_t_gray, enhanced_t);
        applyColorMap(enhanced_t, reg_t_color, COLORMAP_INFERNO);
        
        threshold(reg_t_gray, blend_mask, 50, 255, THRESH_BINARY);
        
        Mat overlay, display = r_img.clone();
        addWeighted(r_img, 0.4, reg_t_color, 0.6, 0.0, overlay);
        overlay.copyTo(display, blend_mask);

        if (is_fire) {
            rectangle(display, Point(0, 0), Point(target_size.width, target_size.height), Scalar(0, 0, 255), 6);
            for (const auto& r : fire_rects) {
                rectangle(display, r, Scalar(0, 0, 255), 2);
                putText(display, "FIRE", Point(r.x, std::max(r.y - 5, 0)), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 255), 2);
            }
            putText(display, "CRITICAL FIRE ALARM", Point(target_size.width/2 - 120, 50), 
                    FONT_HERSHEY_SIMPLEX, 0.7, Scalar(0, 0, 255), 2);
        }

        std::string fps_text = "FPS: " + std::to_string(fps).substr(0,4) + " | CC: " + std::to_string(cc).substr(0,4);
        putText(display, fps_text, Point(10, 20), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255), 1);

        imshow("RPi 4B - C++ Fusion Fast", display);

        frame_count++;
        auto t_now = std::chrono::steady_clock::now();
        std::chrono::duration<float> elapsed = t_now - t_start;
        if (elapsed.count() > 1.0f) {
            fps = frame_count / elapsed.count();
            frame_count = 0;
            t_start = t_now;
        }

        if (waitKey(1) == 'q') break;
    }

    vs_rgb.stop();
    vs_th.stop();
    destroyAllWindows();

    return 0;
}
