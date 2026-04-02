.SUFFIXES : .cpp .o

# ====================================
# 소스 파일 (컴포넌트별 명시)
# ====================================
SRC = src/main.cpp \
      src/controller/SubCamController.cpp \
      src/network/BeaconService.cpp \
      src/network/CommandServer.cpp \
      src/network/NetworkFacade.cpp \
      src/ai/PoseEstimator.cpp \
      src/ai/PersonTracker.cpp \
      src/ai/FallDetector.cpp \
      src/stream/GStreamerCamera.cpp \
      src/stream/StreamPipeline.cpp \
      src/rendering/FrameRenderer.cpp \
      src/util/FrameSaver.cpp \
      src/imageprocessing/ImagePreprocessor.cpp \
      src/imageprocessing/LowLightEnhancer.cpp \
      src/imageprocessing/AdvancedEnhancers.cpp \
      src/buffer/CircularFrameBuffer.cpp \
      src/buffer/EventRecorder.cpp \
      src/transmitter/ChunkedStreamTransmitter.cpp \
      src/system/SystemResourceMonitor.cpp

OBJ = $(SRC:.cpp=.o)
CXX = aarch64-linux-gnu-g++

SYSROOT = $(HOME)/rpi_root
CFLAGS += --sysroot=$(SYSROOT) \
          -std=c++17 -O3 -ffast-math \
          -I$(SYSROOT)/usr/include/gstreamer-1.0 \
          -I$(SYSROOT)/usr/include/glib-2.0 \
          -I$(SYSROOT)/usr/lib/aarch64-linux-gnu/glib-2.0/include \
          -I$(SYSROOT)/usr/lib/aarch64-linux-gnu/gstreamer-1.0/include \
          -Isrc \
          -Iconfig

LDFLAGS += --sysroot=$(SYSROOT) \
           -L$(SYSROOT)/usr/lib/aarch64-linux-gnu \
           -L$(SYSROOT)/lib/aarch64-linux-gnu \
           -Wl,-rpath-link=$(SYSROOT)/usr/lib/aarch64-linux-gnu \
           -Wl,-rpath-link=$(SYSROOT)/lib/aarch64-linux-gnu \
           -lgstrtspserver-1.0 -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0 \
           -lpthread -lstdc++fs

.PHONY: clean all

all: subcam_main

subcam_main: $(OBJ)
	$(CXX) -o $@ $(OBJ) $(LDFLAGS)

.cpp.o:
	$(CXX) $(CFLAGS) -c $< -o $@

clean:
	find src -name "*.o" -delete
	rm -f subcam_main

dep:
	gccmakedep $(SRC)