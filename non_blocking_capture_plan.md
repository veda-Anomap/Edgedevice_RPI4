# Implementation Plan - Non-Blocking Capture Refactoring

This plan addresses the persistent `Assertion !this->empty()` crash in `libcamerasrc` during normal operation by decoupling the real-time camera capture from the heavy downstream processing (AI, Enhancer, Networking, Display).

## Problem Analysis
- **[cameraLoop](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp#274-383) Overload**: Currently, one thread handles Capture, Image Enhancement, Buffer Management, Network Stream Transmitting, and GUI Display.
- **Backpressure**: If `network_writer_.write()` or `cv::imshow()` blocks for even 50ms (common on RPi 4), the GStreamer `appsink` buffer fills up.
- **Libcamera Sensitivity**: `libcamerasrc` is extremely sensitive to buffer starvation. When the application doesn't call [read()](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/GStreamerCamera.cpp#18-21) fast enough, internal request tracking desyncs, leading to the `Assertion`.

## Proposed Architecture
We will split the current [cameraLoop](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp#274-383) into two distinct threads:

1. **Capture Thread ([cameraLoop](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp#274-383))**: 
   - Tasks: `camera_.read()`, `clone()`, [release()](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/GStreamerCamera.cpp#22-27), [push](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/buffer/CircularFrameBuffer.cpp#10-21) to `processing_queue_`.
   - Goal: Maximize FPS and ensure GStreamer buffers are released instantly.
   
2. **Processing Thread (`processingLoop`)**:
   - Tasks: Image Enhancement, `EventRecorder::feedFrame`, AI push, Network [write()](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/edge_device/uart_port.cpp#83-102), `imshow()`.
   - Goal: Perform all heavy-lifting away from the capture clock.

## Proposed Changes

### [Component] Stream Pipeline Layer

#### [MODIFY] [StreamPipeline.h](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.h)
- Add `std::thread processing_thread_`.
- Add `ThreadSafeQueue<cv::Mat> processing_queue_`.
- Add `void processingLoop()`.

#### [MODIFY] [StreamPipeline.cpp](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp)
- **[startStreaming](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp#32-133)**: Initialize `processing_queue_(2)` and start `processing_thread_`.
- **[stopStreaming](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp#134-171)**: Join `processing_thread_`.
- **[cameraLoop](file:///c:/Users/1-05/Downloads/SubCameraApp_refactory_260311/SubCameraApp_refactory_260311/src/stream/StreamPipeline.cpp#274-383)**: Refactor to contain ONLY the capture and push logic.
- **`processingLoop`**: Move all processing/network/display logic here.

## Verification Plan

### Manual Verification
- **Stability Test**: Run the app for 1 hour with AI and Network enabled. Monitor for "Assertion" crashes.
- **Shutdown Test**: Rapidly start/stop streaming via network commands to ensure thread safety.
- **Latency Check**: Verify that the network stream remains responsive under high CPU load.
