#include "FrameRenderer.h"
#include "../../config/AppConfig.h"

void FrameRenderer::drawDetections(cv::Mat& frame, const DetectionResult& result) {
    for (const auto& obj : result.objects) {
        // 바운딩 박스 (낙상: 빨간색, 정상: 초록색)
        cv::Scalar color = obj.is_falling ? cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
        cv::rectangle(frame, obj.box, color, 2);

        // 스켈레톤 드로잉
        for (const auto& bone : AppConfig::KPT_SKELETON) {
            if (bone.first <= (int)obj.skeleton.size() &&
                bone.second <= (int)obj.skeleton.size()) {
                cv::Point p1 = obj.skeleton[bone.first - 1];
                cv::Point p2 = obj.skeleton[bone.second - 1];
                if (p1.x > 0 && p1.y > 0 && p2.x > 0 && p2.y > 0) {
                    cv::line(frame, p1, p2, cv::Scalar(255, 255, 0), 2);
                }
            }
        }

        // 라벨 표시
        std::string label = "ID:" + std::to_string(obj.track_id)
                            + (obj.is_falling ? " FALL!" : "");
        cv::putText(frame, label, cv::Point(obj.box.x, obj.box.y - 10),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, color, 2);
    }

    // 인원 수 표시
    cv::putText(frame, "Persons: " + std::to_string(result.person_count),
                cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(255, 255, 255), 2);
}

void FrameRenderer::drawSingleDetection(cv::Mat& frame, const SingleDetection& det) {
    // 박스 그리기 (빨간색)
    cv::rectangle(frame, det.box, cv::Scalar(0, 0, 255), 3);

    // 스켈레톤 그리기 (노란색)
    for (const auto& bone : AppConfig::KPT_SKELETON) {
        if (bone.first <= (int)det.skeleton.size() &&
            bone.second <= (int)det.skeleton.size()) {
            cv::Point p1 = det.skeleton[bone.first - 1];
            cv::Point p2 = det.skeleton[bone.second - 1];
            if (p1.x > 0 && p1.y > 0 && p2.x > 0 && p2.y > 0) {
                cv::line(frame, p1, p2, cv::Scalar(255, 255, 0), 2);
            }
        }
    }
}
