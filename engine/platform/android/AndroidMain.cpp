#include <game-activity/native_app_glue/android_native_app_glue.h>

#include "NativeEngine.h"

extern "C" {
void android_main(struct android_app* state);
};

void android_main(struct android_app* app) {
    NativeEngine *engine = new NativeEngine(app);
    engine->gameLoop();
    delete engine;
}