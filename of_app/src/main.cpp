#include "ofMain.h"
#include "ofApp.h"
#include <memory>

// The main file is intentionally plain: it mirrors the openFrameworks
// application template so newcomers can orient themselves fast. We pick a
// window size large enough for the HUD but small enough to live beside logs.
int main() {
    ofGLFWWindowSettings settings;
    settings.setSize(1280, 720);
    settings.setPosition(glm::ivec2(100, 60));
    settings.resizable = true;
    ofCreateWindow(settings);

    ofRunApp(std::make_shared<ofApp>());
    return 0;
}
