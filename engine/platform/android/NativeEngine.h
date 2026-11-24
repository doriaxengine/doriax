// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef NativeEngine_H_
#define NativeEngine_H_

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <android/asset_manager.h>
#include <game-activity/GameActivity.h>
#include <functional>
#include <string>

struct NativeEngineSavedState {
    bool mHasFocus;
};

struct JniData{
    jobject gameActivityObjRef;
    jclass gameActivityClsRef;

    jmethodID getUserSettingsRef;

    jobject userSettingsObjRef;
    jclass userSettingsClsRef;

    jmethodID getBoolForKeyRef;
    jmethodID getIntegerForKeyRef;
    jmethodID getLongForKeyRef;
    jmethodID getFloatForKeyRef;
    jmethodID getDoubleForKeyRef;
    jmethodID getStringForKeyRef;

    jmethodID setBoolForKeyRef;
    jmethodID setIntegerForKeyRef;
    jmethodID setLongForKeyRef;
    jmethodID setFloatForKeyRef;
    jmethodID setDoubleForKeyRef;
    jmethodID setStringForKeyRef;

    jmethodID removeKeyRef;

    jmethodID showTextInputRef;
    jmethodID hideTextInputRef;
};

class NativeEngine {
private:
    static const int MAX_GAMEPADS = 8;

    struct GamepadDevice {
        int32_t deviceId = -1;
        float axes[6] = {0.0f};
        int hatX = 0;
        int hatY = 0;
    };

    struct android_app *mApp;
    bool mHasFocus, mIsVisible, mHasWindow;
    bool mHasGLObjects;

    GamepadDevice mGamepads[MAX_GAMEPADS];

    EGLDisplay mEglDisplay;
    EGLSurface mEglSurface;
    EGLContext mEglContext;
    EGLConfig mEglConfig;

    int mSurfWidth, mSurfHeight;
    int mApiVersion;
    int mScreenDensity;
    uint64_t mActiveAxisIds;

    std::string mInternalDataPath;
    std::string mExternalDataPath;

    JNIEnv *mJniEnv;
    JniData mJniData;

    struct NativeEngineSavedState mState;

    bool mIsFirstFrame;

    int mSystemBarOffset;

    bool initDisplay();
    bool initSurface();
    bool initContext();

    void killContext();
    void killSurface();
    void killDisplay();

    bool initGLObjects();
    void killGLObjects();

    int getGamepadSlot(int32_t deviceId);
    void handleGamepadMotion(GameActivityMotionEvent* motionEvent);
    bool handleEglError(EGLint error);

    bool prepareToRender();

    void doFrame();

    bool isAnimating();

    void updateSystemBarOffset();

    void handleText(const std::wstring& oldText, const std::wstring& newText, int session);
    void releaseBack();
    void handleBack();
    void handleKey(GameActivityKeyEvent* keyEvent);
    void handleMotion(GameActivityMotionEvent* motionEvent);
    void handleGameActivityInput();

    void callActivity(const std::function<void(JNIEnv*, jobject)>& call);

public:
    // create an engine
    NativeEngine(struct android_app *app);

    ~NativeEngine();

    static NativeEngine *getInstance();
    JNIEnv* getJniEnv();
    GameActivity* getActivity();
    jobject getJavaGameActivity();
    JniData& getJniData();

    void handleCommand(int32_t cmd);

    void gameLoop();

    void setupJNI();

    int getSurfWidth();
    int getSurfHeight();
    int getScreenDensity();

    void showSoftInput(std::wstring text);
    void hideSoftInput();

    std::string getInternalDataPath();
    std::string getExternalDataPath();

    AAssetManager* getAssetManager();
};

#endif /* NativeEngine_H_ */
