#include <iostream>
#include <string>
#include "TesterApp.h"

using namespace std;

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "Usage: ./subcam_tester <cam | video_path | image_path>" << endl;
        cout << "Keys:" << endl;
        cout << "  [m] Switch Mode (Compare -> Tune -> AI)" << endl;
        cout << "  [1-9] Switch Algorithm in Tune Mode" << endl;
        cout << "  [q] Quit" << endl;
        return -1;
    }

    string source = argv[1];
    
    // [최적화] OpenCV 스레드 제한 (TFLite 충돌 방지)
    cv::setNumThreads(1);

    TesterApp app;
    if (!app.initialize(source)) {
        return -1;
    }

    cout << "[Tester] Starting Application..." << endl;
    app.run();

    cout << "[Tester] Terminated." << endl;
    return 0;
}
