#ifndef NativeEngine_H_
#define NativeEngine_H_

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <jni.h>
#include <android/asset_manager.h>
#include <game-activity/GameActivity.h>
#include <string>

struct NativeEngineSavedState {
    bool mHasFocus;
};

struct JniData{
    jobject gameActivityObjRef;
    jclass gameActivityClsRef;

    jmethodID getUserSettingsRef;
    jmethodID getAdMobWrapperRef;

    jobject userSettingsObjRef;
    jclass userSettingsClsRef;

    jobject adMobWrapperObjRef;
    jclass adMobWrapperClsRef;

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

    jmethodID initializeAdMob;
    jmethodID setMaxAdContentRating;
    jmethodID loadInterstitialAd;
    jmethodID isInterstitialAdLoaded;
    jmethodID showInterstitialAd;
};

class NativeEngine {
private:
    struct android_app *mApp;
    bool mHasFocus, mIsVisible, mHasWindow;
    bool mHasGLObjects;

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

    GameTextInputState mGameTextInputState;

    int mSystemBarOffset;

    bool initDisplay();
    bool initSurface();
    bool initContext();

    void killContext();
    void killSurface();
    void killDisplay();

    bool initGLObjects();
    void killGLObjects();

    int getDoriaxKey(int32_t key);
    int getDoriaxModifiers(int32_t mods);
    bool handleEglError(EGLint error);

    bool prepareToRender();

    void doFrame();

    bool isAnimating();

    void updateSystemBarOffset();

    void handleGameActivityInput();

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