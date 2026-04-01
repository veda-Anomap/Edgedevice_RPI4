#pragma once
#include <opencv2/opencv.hpp>

// =============================================
// ITestModule: 테스팅 앱 뷰/모듈 공통 인터페이스 (DIP, OCP)
// =============================================
class ITestModule {
public:
    virtual ~ITestModule() = default;

    // 새 프레임 또는 이미지가 로드될 때 호출
    virtual void update(const cv::Mat& frame) = 0;

    // 모듈의 결과를 canvas에 렌더링
    virtual void render(cv::Mat& canvas) = 0;

    // 키 입력 처리 (처리하면 true 반환)
    virtual bool onKey(char key) = 0;

    // 모듈 이름 (타이틀바, 로그 등에 사용)
    virtual std::string getName() const = 0;
};
