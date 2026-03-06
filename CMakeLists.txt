cmake_minimum_required(VERSION 3.10)
project(SubCameraApp)

set(CMAKE_CXX_STANDARD 17)

# -------------------------------
# 패키지 탐색 (OpenCV)
# -------------------------------   
find_package(OpenCV REQUIRED)

# -------------------------------
# 경로 설정
# -------------------------------
set(TF_SRC "/home/suseok/tensorflow_src")
set(BAZEL_EXTERNAL "${TF_SRC}/bazel-tensorflow_src/external")

include_directories(
    ${OpenCV_INCLUDE_DIRS}
    ${TF_SRC}
    ${BAZEL_EXTERNAL}/flatbuffers/include
    ${BAZEL_EXTERNAL}/com_google_absl
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/config
)

# -------------------------------
# 소스 파일 설정 (컴포넌트별)
# -------------------------------
set(APP_SRC
    # 진입점
    src/main.cpp

    # 컨트롤러
    src/controller/SubCamController.cpp

    # 네트워크
    src/network/BeaconService.cpp
    src/network/CommandServer.cpp
    src/network/NetworkFacade.cpp

    # AI
    src/ai/PoseEstimator.cpp
    src/ai/PersonTracker.cpp
    src/ai/FallDetector.cpp

    # 스트림
    src/stream/GStreamerCamera.cpp
    src/stream/StreamPipeline.cpp

    # 렌더링 & 유틸리티
    src/rendering/FrameRenderer.cpp
    src/util/FrameSaver.cpp

    # [Phase 2] 이미지 전처리
    src/imageprocessing/ImagePreprocessor.cpp
    src/imageprocessing/LowLightEnhancer.cpp
    src/imageprocessing/AdvancedEnhancers.cpp

    # [Phase 2] 이벤트 버퍼
    src/buffer/CircularFrameBuffer.cpp
    src/buffer/EventRecorder.cpp

    # [Phase 2] 스트림 전송
    src/transmitter/ChunkedStreamTransmitter.cpp

    # 시스템 모니터링
    src/system/SystemResourceMonitor.cpp
)

# -------------------------------
# 실행 파일 생성
# -------------------------------
set(APP_NAME subcam_main)
add_executable(${APP_NAME} ${APP_SRC})

#target_compile_options(${APP_NAME} PRIVATE -O2 -pg -g)
#target_link_options(${APP_NAME} PRIVATE -pg -g)

target_compile_options(${APP_NAME} PRIVATE -O3 -ffast-math -mcpu=native)
target_link_options(${APP_NAME} PRIVATE -O3)

# -------------------------------
# 라이브러리 링크
# -------------------------------
target_link_libraries(${APP_NAME} PRIVATE
    ${OpenCV_LIBS}
    "${TF_SRC}/bazel-bin/tensorflow/lite/libtensorflowlite.so"
    pthread
    dl
    rt
    stdc++fs
)
