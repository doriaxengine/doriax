// ---------------------------------------
// Based on samples: https://github.com/android/games-samples
// ---------------------------------------

#include "NativeEngine.h"

#include <game-activity/native_app_glue/android_native_app_glue.h>
#include <android/log.h>
#include <android/window.h>
#include <swappy/swappyGL.h>
#include <cstring>
#include <codecvt>
#include <locale>
#include <assert.h>

#include "DoriaxAndroid.h"
#include "Engine.h"
#include "Input.h"
#include "Log.h"

#define MAX_GL_ERRORS 200

static bool all_motion_filter(const GameActivityMotionEvent* event) {
  // Process all motion events
  return true;
}

static void _handle_cmd_proxy(struct android_app *app, int32_t cmd) {
    NativeEngine *engine = (NativeEngine *) app->userData;
    engine->handleCommand(cmd);
}

static void _log_opengl_error(GLenum err) {
    switch (err) {
        case GL_NO_ERROR:
            doriax::Log::error("*** OpenGL error: GL_NO_ERROR");
            break;
        case GL_INVALID_ENUM:
            doriax::Log::error("*** OpenGL error: GL_INVALID_ENUM");
            break;
        case GL_INVALID_VALUE:
            doriax::Log::error("*** OpenGL error: GL_INVALID_VALUE");
            break;
        case GL_INVALID_OPERATION:
            doriax::Log::error("*** OpenGL error: GL_INVALID_OPERATION");
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            doriax::Log::error("*** OpenGL error: GL_INVALID_FRAMEBUFFER_OPERATION");
            break;
        case GL_OUT_OF_MEMORY:
            doriax::Log::error("*** OpenGL error: GL_OUT_OF_MEMORY");
            break;
        default:
            doriax::Log::error("*** OpenGL error: error %d", err);
            break;
    }
}

static NativeEngine *_singleton = NULL;

// workaround for internal bug b/149866792
static NativeEngineSavedState appState = {false};

static std::wstring textInputBuffer;


NativeEngine::NativeEngine(struct android_app *app) {
    mApp = app;
    mHasFocus = mIsVisible = mHasWindow = false;
    mHasGLObjects = false;
    mEglDisplay = EGL_NO_DISPLAY;
    mEglSurface = EGL_NO_SURFACE;
    mEglContext = EGL_NO_CONTEXT;
    mEglConfig = 0;
    mSurfWidth = mSurfHeight = 0;
    mApiVersion = 0;
    mScreenDensity = AConfiguration_getDensity(app->config);
    mActiveAxisIds = 0;
    mJniEnv = NULL;
    memset(&mState, 0, sizeof(mState));
    mIsFirstFrame = true;

    mInternalDataPath = mApp->activity->internalDataPath;
    mExternalDataPath = mApp->activity->externalDataPath;

    app->motionEventFilter = all_motion_filter;

    // Flags to control how the IME behaves.
    // https://developer.android.com/reference/android/text/InputType
    constexpr int InputType_dot_TYPE_CLASS_TEXT = 1;
    constexpr int InputType_dot_TYPE_TEXT_FLAG_NO_SUGGESTIONS = 524288;
    // https://developer.android.com/reference/android/view/inputmethod/EditorInfo
    constexpr int IME_ACTION_NONE = 1;
    constexpr int IME_FLAG_NO_FULLSCREEN = 33554432;
    
    GameActivity_setImeEditorInfo(app->activity, InputType_dot_TYPE_CLASS_TEXT, IME_ACTION_NONE, IME_FLAG_NO_FULLSCREEN);

    if (app->savedState != NULL) {
        // we are starting with previously saved state -- restore it
        mState = *(struct NativeEngineSavedState *) app->savedState;
    }

    // only one instance of NativeEngine may exist!
    assert(_singleton == NULL);
    _singleton = this;

    // initialize Swappy to adjuest swap timing properly.
    SwappyGL_init(getJniEnv(), mApp->activity->javaGameActivity);
    SwappyGL_setSwapIntervalNS(SWAPPY_SWAP_60FPS);

    setupJNI();

    doriax::Engine::systemInit(0, nullptr, new DoriaxAndroid());
}

NativeEngine::~NativeEngine() {
    doriax::Engine::systemShutdown();

    SwappyGL_destroy();

    killContext();

    if (mJniEnv) {
    mApp->activity->vm->DetachCurrentThread();
    mJniEnv = NULL;
    }
    _singleton = NULL;
}

NativeEngine *NativeEngine::getInstance() {
    //assert(_singleton != NULL);
    return _singleton;
}

GameActivity* NativeEngine::getActivity(){
    return mApp->activity;
}

jobject NativeEngine::getJavaGameActivity(){
    return mApp->activity->javaGameActivity;
}

JniData& NativeEngine::getJniData(){
    return mJniData;
}

JNIEnv* NativeEngine::getJniEnv() {
    if (!mJniEnv) {
        if (0 != mApp->activity->vm->AttachCurrentThread(&mJniEnv, NULL)) {
            doriax::Log::error("*** FATAL ERROR: Failed to attach thread to JNI.");
        }
        assert(mJniEnv != NULL);
    }

    return mJniEnv;
}

int NativeEngine::getSurfWidth(){
    return mSurfWidth;
}

int NativeEngine::getSurfHeight(){
    return mSurfHeight;
}

int NativeEngine::getScreenDensity(){
    return mScreenDensity;
}

void NativeEngine::showSoftInput(std::wstring text){
    textInputBuffer = text;

    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
    std::string strInputBuffer = convert.to_bytes(textInputBuffer);

    mGameTextInputState.text_UTF8 = strInputBuffer.data();
    mGameTextInputState.text_length = strInputBuffer.length();
    mGameTextInputState.selection.start = strInputBuffer.length();
    mGameTextInputState.selection.end = strInputBuffer.length();
    mGameTextInputState.composingRegion.start = -1;
    mGameTextInputState.composingRegion.end = -1;

    GameActivity_setTextInputState(mApp->activity, &mGameTextInputState);
    GameActivity_showSoftInput(NativeEngine::getInstance()->getActivity(), 0);
}

void NativeEngine::hideSoftInput(){
    GameActivity_hideSoftInput(NativeEngine::getInstance()->getActivity(), 0);
}

std::string NativeEngine::getInternalDataPath(){
    return mInternalDataPath;
}

std::string NativeEngine::getExternalDataPath(){
    return mExternalDataPath;
}

AAssetManager* NativeEngine::getAssetManager(){
    return mApp->activity->assetManager;
}

bool NativeEngine::initGLObjects() {
    if (!mHasGLObjects) {
        doriax::Engine::systemViewLoaded();
        mHasGLObjects = true;
    }
    return true;
}

void NativeEngine::killGLObjects() {
    if (mHasGLObjects) {
        doriax::Engine::systemViewDestroyed();
        mHasGLObjects = false;
    }
}

void NativeEngine::gameLoop() {
    mApp->userData = this;
    mApp->onAppCmd = _handle_cmd_proxy;
    //mApp->onInputEvent = _handle_input_proxy;

    GameActivity_setWindowFlags(mApp->activity,
                                AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_TURN_SCREEN_ON |
                                AWINDOW_FLAG_FULLSCREEN |
                                AWINDOW_FLAG_SHOW_WHEN_LOCKED,
                                0);
    updateSystemBarOffset();

    while (1) {
        int events;
        struct android_poll_source *source;

        // If not animating, block until we get an event; if animating, don't block.
        while ((ALooper_pollOnce(isAnimating() ? 0 : -1, NULL, &events, (void **) &source)) >= 0) {
            // process event
            if (source != NULL) {
                source->process(mApp, source);
            }

            // are we exiting?
            if (mApp->destroyRequested) {
                return;
            }
        }

        handleGameActivityInput();

        if (mApp->textInputState) {
            GameActivity_getTextInputState(mApp->activity, [](void *context, const GameTextInputState *state) {
                if (!context || !state) return;

                std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
                std::wstring utf16Text = convert.from_bytes(state->text_UTF8);

                while (textInputBuffer.length() > utf16Text.length()){
                    textInputBuffer.pop_back();
                    doriax::Engine::systemCharInput('\b');
                }

                int pos = 0;
                while (textInputBuffer[pos] == utf16Text[pos] and pos < textInputBuffer.length()){
                    pos++;
                }

                for (int i = pos; i < textInputBuffer.length(); i++){
                    doriax::Engine::systemCharInput('\b');
                }
                for (int i = pos; i < utf16Text.length(); i++){
                    doriax::Engine::systemCharInput(utf16Text[i]);
                }
                textInputBuffer = utf16Text;

            }, this);
            mApp->textInputState = 0;
        }

        if (isAnimating()) {
            doFrame();
        }
    }
}

void NativeEngine::setupJNI(){
    JNIEnv* env = getJniEnv();

    mJniData.gameActivityObjRef = NativeEngine::getInstance()->getJavaGameActivity();
    mJniData.gameActivityClsRef = env->GetObjectClass(mJniData.gameActivityObjRef);

    mJniData.getUserSettingsRef = env->GetMethodID(mJniData.gameActivityClsRef, "getUserSettings", "()Lorg/doriaxengine/doriax/UserSettings;");
    mJniData.getAdMobWrapperRef = env->GetMethodID(mJniData.gameActivityClsRef, "getAdMobWrapper", "()Lorg/doriaxengine/doriax/AdMobWrapper;");

    mJniData.userSettingsObjRef = env->CallObjectMethod(mJniData.gameActivityObjRef, mJniData.getUserSettingsRef);
    mJniData.userSettingsClsRef = env->GetObjectClass(mJniData.userSettingsObjRef);

    mJniData.adMobWrapperObjRef = env->CallObjectMethod(mJniData.gameActivityObjRef, mJniData.getAdMobWrapperRef);
    mJniData.adMobWrapperClsRef = env->GetObjectClass(mJniData.adMobWrapperObjRef);

    mJniData.getBoolForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "getBoolForKey", "(Ljava/lang/String;Z)Z");
    mJniData.getIntegerForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "getIntegerForKey", "(Ljava/lang/String;I)I");
    mJniData.getLongForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "getLongForKey", "(Ljava/lang/String;J)J");
    mJniData.getFloatForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "getFloatForKey", "(Ljava/lang/String;F)F");
    mJniData.getDoubleForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "getDoubleForKey", "(Ljava/lang/String;D)D");
    mJniData.getStringForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "getStringForKey", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    mJniData.setBoolForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "setBoolForKey", "(Ljava/lang/String;Z)V");
    mJniData.setIntegerForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "setIntegerForKey", "(Ljava/lang/String;I)V");
    mJniData.setLongForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "setLongForKey", "(Ljava/lang/String;J)V");
    mJniData.setFloatForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "setFloatForKey", "(Ljava/lang/String;F)V");
    mJniData.setDoubleForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "setDoubleForKey", "(Ljava/lang/String;D)V");
    mJniData.setStringForKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "setStringForKey", "(Ljava/lang/String;Ljava/lang/String;)V");

    mJniData.removeKeyRef = env->GetMethodID(mJniData.userSettingsClsRef, "removeKey", "(Ljava/lang/String;)V");

    mJniData.initializeAdMob = env->GetMethodID(mJniData.adMobWrapperClsRef, "initialize","(ZZ)V");
    mJniData.setMaxAdContentRating = env->GetMethodID(mJniData.adMobWrapperClsRef, "setMaxAdContentRating","(I)V");
    mJniData.loadInterstitialAd = env->GetMethodID(mJniData.adMobWrapperClsRef, "loadInterstitialAd","(Ljava/lang/String;)V");
    mJniData.isInterstitialAdLoaded = env->GetMethodID(mJniData.adMobWrapperClsRef, "isInterstitialAdLoaded","()Z");
    mJniData.showInterstitialAd = env->GetMethodID(mJniData.adMobWrapperClsRef, "showInterstitialAd","()V");
}

bool NativeEngine::initDisplay() {
    if (mEglDisplay != EGL_NO_DISPLAY) {
        return true;
    }

    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (EGL_FALSE == eglInitialize(mEglDisplay, 0, 0)) {
        doriax::Log::error("NativeEngine: failed to init display, error %d", eglGetError());
        return false;
    }
    return true;
}

bool NativeEngine::initSurface() {
    assert(mEglDisplay != EGL_NO_DISPLAY);

    if (mEglSurface != EGL_NO_SURFACE) {
        return true;
    }

    EGLint numConfigs;

    const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, // request OpenGL ES 3.0
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    // Pick the first EGLConfig that matches.
    eglChooseConfig(mEglDisplay, attribs, &mEglConfig, 1, &numConfigs);

    if (!numConfigs){
        // fall back to 16bit depth buffer
        const EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, // request OpenGL ES 3.0
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_BLUE_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_RED_SIZE, 8,
                EGL_DEPTH_SIZE, 16,
                EGL_NONE
        };

        eglChooseConfig(mEglDisplay, attribs, &mEglConfig, 1, &numConfigs);
    }

    if (!numConfigs) {
        doriax::Log::error("Unable to retrieve EGL config");
        return false;
    }

    // create EGL surface
    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglConfig, mApp->window, NULL);
    if (mEglSurface == EGL_NO_SURFACE) {
        doriax::Log::error("Failed to create EGL surface, EGL error %d", eglGetError());
        return false;
    }

    return true;
}

bool NativeEngine::initContext() {
    // need a display
    assert(mEglDisplay != EGL_NO_DISPLAY);

    EGLint attribList[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE}; // OpenGL ES 3.0

    if (mEglContext != EGL_NO_CONTEXT) {
        return true;
    }

    mEglContext = eglCreateContext(mEglDisplay, mEglConfig, NULL, attribList);
    if (mEglContext == EGL_NO_CONTEXT) {
        doriax::Log::error("Failed to create EGL context, EGL error %d", eglGetError());
        return false;
    }

    return true;
}

void NativeEngine::killContext() {
    killGLObjects();

    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    if (mEglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(mEglDisplay, mEglContext);
        mEglContext = EGL_NO_CONTEXT;
    }
}

void NativeEngine::killSurface() {
    eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (mEglSurface != EGL_NO_SURFACE) {
        eglDestroySurface(mEglDisplay, mEglSurface);
        mEglSurface = EGL_NO_SURFACE;
    }
}

void NativeEngine::killDisplay() {
    killContext();
    killSurface();

    if (mEglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(mEglDisplay);
        mEglDisplay = EGL_NO_DISPLAY;
    }
}

bool NativeEngine::handleEglError(EGLint error) {
    switch (error) {
        case EGL_SUCCESS:
            // nothing to do
            return true;
        case EGL_CONTEXT_LOST:
            doriax::Log::warn("NativeEngine: egl error: EGL_CONTEXT_LOST. Recreating context.");
            killContext();
            return true;
        case EGL_BAD_CONTEXT:
            doriax::Log::warn("NativeEngine: egl error: EGL_BAD_CONTEXT. Recreating context.");
            killContext();
            return true;
        case EGL_BAD_DISPLAY:
            doriax::Log::warn("NativeEngine: egl error: EGL_BAD_DISPLAY. Recreating display.");
            killDisplay();
            return true;
        case EGL_BAD_SURFACE:
            doriax::Log::warn("NativeEngine: egl error: EGL_BAD_SURFACE. Recreating display.");
            killSurface();
            return true;
        default:
            doriax::Log::warn("NativeEngine: unknown egl error: %d", error);
            return false;
    }
}

bool NativeEngine::prepareToRender() {
    if (mEglDisplay == EGL_NO_DISPLAY || mEglSurface == EGL_NO_SURFACE ||
        mEglContext == EGL_NO_CONTEXT) {

        // create display if needed
        if (!initDisplay()) {
            doriax::Log::error("NativeEngine: failed to create display.");
            return false;
        }

        // create surface if needed
        if (!initSurface()) {
            doriax::Log::error("NativeEngine: failed to create surface.");
            return false;
        }

        // create context if needed
        if (!initContext()) {
            doriax::Log::error("NativeEngine: failed to create context.");
            return false;
        }

        // bind them
        if (EGL_FALSE == eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext)) {
            doriax::Log::error("NativeEngine: eglMakeCurrent failed, EGL error %d", eglGetError());
            handleEglError(eglGetError());
        }

    }
    if (!mHasGLObjects) {
        if (!initGLObjects()) {
            doriax::Log::error("NativeEngine: unable to initialize OpenGL objects.");
            return false;
        }
    }

    doriax::Engine::systemViewChanged();

    // ready to render
    return true;
}

void NativeEngine::doFrame() {
    // prepare to render (create context, surfaces, etc, if needed)
    if (!prepareToRender()) {
        // not ready
        doriax::Log::error("NativeEngine: preparation to render failed.");
        return;
    }

    // how big is the surface? We query every frame because it's cheap, and some
    // strange devices out there change the surface size without calling any callbacks...
    int width, height;
    eglQuerySurface(mEglDisplay, mEglSurface, EGL_WIDTH, &width);
    eglQuerySurface(mEglDisplay, mEglSurface, EGL_HEIGHT, &height);

    if (width != mSurfWidth || height != mSurfHeight) {
        mSurfWidth = width;
        mSurfHeight = height;

        doriax::Engine::systemViewChanged();

        glViewport(0, 0, mSurfWidth, mSurfHeight);
    }

    if (mIsFirstFrame) {
        mIsFirstFrame = false;
        // if this is the first frame
    }

    doriax::Engine::systemDraw();

    // swap buffers
    if (!SwappyGL_swap(mEglDisplay, mEglSurface)) {        // failed to swap buffers...
        doriax::Log::error("NativeEngine: SwappyGL_swap failed, EGL error %d", eglGetError());
        handleEglError(eglGetError());
    }

    // print out GL errors, if any
    GLenum e;
    static int errorsPrinted = 0;
    while ((e = glGetError()) != GL_NO_ERROR) {
        if (errorsPrinted < MAX_GL_ERRORS) {
            _log_opengl_error(e);
            ++errorsPrinted;
            if (errorsPrinted >= MAX_GL_ERRORS) {
                doriax::Log::error("*** NativeEngine: TOO MANY OPENGL ERRORS. NO LONGER PRINTING.");
            }
        }
    }
}

bool NativeEngine::isAnimating() {
    return mHasFocus && mIsVisible && mHasWindow;
}

void NativeEngine::updateSystemBarOffset() {
    ARect insets;
    // Log all the insets types
    GameActivity_getWindowInsets(mApp->activity, GAMECOMMON_INSETS_TYPE_SYSTEM_BARS, &insets);
    mSystemBarOffset = insets.top;
}

int NativeEngine::getDoriaxKey(int32_t key){
    if (key == AKEYCODE_SPACE)
        return S_KEY_SPACE;
    if (key == AKEYCODE_APOSTROPHE)
        return S_KEY_APOSTROPHE;
    if (key == AKEYCODE_COMMA)
        return S_KEY_COMMA;
    if (key == AKEYCODE_MINUS)
        return S_KEY_MINUS;
    if (key == AKEYCODE_PERIOD)
        return S_KEY_PERIOD;
    if (key == AKEYCODE_SLASH)
        return S_KEY_SLASH;

    if (key >= AKEYCODE_0 && key <= AKEYCODE_9)
        return key + S_KEY_0 - AKEYCODE_0;

    if (key == AKEYCODE_SEMICOLON)
        return S_KEY_SEMICOLON;
    if (key == AKEYCODE_EQUALS)
        return S_KEY_EQUAL;

    if (key >= AKEYCODE_A && key <= AKEYCODE_Z)
        return key + S_KEY_A - AKEYCODE_A;

    if (key == AKEYCODE_LEFT_BRACKET)
        return S_KEY_LEFT_BRACKET;
    if (key == AKEYCODE_BACKSLASH)
        return S_KEY_BACKSLASH;
    if (key == AKEYCODE_RIGHT_BRACKET)
        return S_KEY_RIGHT_BRACKET;
    if (key == AKEYCODE_GRAVE)
        return S_KEY_GRAVE_ACCENT;

    if (key == AKEYCODE_ESCAPE)
        return S_KEY_ESCAPE;
    if (key == AKEYCODE_ENTER)
        return S_KEY_ENTER;
    if (key == AKEYCODE_TAB)
        return S_KEY_TAB;
    if (key == AKEYCODE_DEL)
        return S_KEY_BACKSPACE;
    if (key == AKEYCODE_INSERT)
        return S_KEY_INSERT;
    if (key == AKEYCODE_FORWARD_DEL)
        return S_KEY_DELETE;
    if (key == AKEYCODE_DPAD_RIGHT)
        return S_KEY_RIGHT;
    if (key == AKEYCODE_DPAD_LEFT)
        return S_KEY_LEFT;
    if (key == AKEYCODE_DPAD_DOWN)
        return S_KEY_DOWN;
    if (key == AKEYCODE_DPAD_UP)
        return S_KEY_UP;
    if (key == AKEYCODE_PAGE_UP)
        return S_KEY_PAGE_UP;
    if (key == AKEYCODE_PAGE_DOWN)
        return S_KEY_PAGE_DOWN;
    if (key == AKEYCODE_MOVE_HOME)
        return S_KEY_HOME;
    if (key == AKEYCODE_MOVE_END)
        return S_KEY_END;
    if (key == AKEYCODE_CAPS_LOCK)
        return S_KEY_CAPS_LOCK;
    if (key == AKEYCODE_SCROLL_LOCK)
        return S_KEY_SCROLL_LOCK;
    if (key == AKEYCODE_NUM_LOCK)
        return S_KEY_NUM_LOCK;
    if (key == AKEYCODE_SYSRQ)
        return S_KEY_PRINT_SCREEN;
    if (key == AKEYCODE_BREAK)
        return S_KEY_PAUSE;

    if (key >= AKEYCODE_F1 && key <= AKEYCODE_F12)
        return key + S_KEY_F1 - AKEYCODE_F1;

    if (key >= AKEYCODE_NUMPAD_0 && key <= AKEYCODE_NUMPAD_9)
        return key + S_KEY_KP_0 - AKEYCODE_NUMPAD_0;
    if (key == AKEYCODE_NUMPAD_DOT)
        return S_KEY_KP_DECIMAL;
    if (key == AKEYCODE_NUMPAD_DIVIDE)
        return S_KEY_KP_DIVIDE;
    if (key == AKEYCODE_NUMPAD_MULTIPLY)
        return S_KEY_KP_MULTIPLY;
    if (key == AKEYCODE_NUMPAD_SUBTRACT)
        return S_KEY_KP_SUBTRACT;
    if (key == AKEYCODE_NUMPAD_ADD)
        return S_KEY_KP_ADD;
    if (key == AKEYCODE_NUMPAD_ENTER)
        return S_KEY_KP_ENTER;
    if (key == AKEYCODE_NUMPAD_EQUALS)
        return S_KEY_KP_EQUAL;

    if (key == AKEYCODE_SHIFT_LEFT)
        return S_KEY_LEFT_SHIFT;
    if (key == AKEYCODE_CTRL_LEFT)
        return S_KEY_LEFT_CONTROL;
    if (key == AKEYCODE_ALT_LEFT)
        return S_KEY_LEFT_ALT;
    if (key == AKEYCODE_META_LEFT)
        return S_KEY_LEFT_SUPER;
    if (key == AKEYCODE_SHIFT_RIGHT)
        return S_KEY_RIGHT_SHIFT;
    if (key == AKEYCODE_CTRL_RIGHT)
        return S_KEY_RIGHT_CONTROL;
    if (key == AKEYCODE_ALT_RIGHT)
        return S_KEY_RIGHT_ALT;
    if (key == AKEYCODE_META_RIGHT)
        return S_KEY_RIGHT_SUPER;
    if (key == AKEYCODE_MENU)
        return S_KEY_MENU;

    return 0;
}

int NativeEngine::getDoriaxModifiers(int32_t mods){
    int modifiers = 0;
    if (mods & AMETA_CTRL_ON) modifiers |= S_MODIFIER_CONTROL;
    if (mods & AMETA_SHIFT_ON) modifiers |= S_MODIFIER_SHIFT;
    if (mods & AMETA_ALT_ON)  modifiers |= S_MODIFIER_ALT;
    if (mods & AMETA_META_ON) modifiers |= S_MODIFIER_SUPER;
    if (mods & AMETA_CAPS_LOCK_ON) modifiers |= S_MODIFIER_CAPS_LOCK;
    if (mods & AMETA_NUM_LOCK_ON) modifiers |= S_MODIFIER_NUM_LOCK;

    return modifiers;
}

void NativeEngine::handleGameActivityInput(){
    android_input_buffer* inputBuffer = android_app_swap_input_buffers(mApp);

    if (inputBuffer == nullptr) return;

    if (inputBuffer->keyEventsCount != 0) {
        for (uint64_t i = 0; i < inputBuffer->keyEventsCount; ++i) {
            GameActivityKeyEvent* keyEvent = &inputBuffer->keyEvents[i];

            // metaState works with capslock, numlock and others
            int keyCode = getDoriaxKey(keyEvent->keyCode);
            int modifiers = getDoriaxModifiers(keyEvent->metaState);
            bool repeat = (keyEvent->repeatCount > 0)?true:false;

            if (keyEvent->action == AKEY_EVENT_ACTION_DOWN) {
                doriax::Engine::systemKeyDown(keyCode, repeat, modifiers);
            } else if (keyEvent->action == AKEY_EVENT_ACTION_UP) {
                doriax::Engine::systemKeyUp(keyCode, repeat, modifiers);
            }

            if (keyEvent->keyCode == AKEYCODE_BACK && 0 == keyEvent->action) {
                doriax::Engine::systemCharInput('\b');
            }

        }
        android_app_clear_key_events(inputBuffer);
    }

    if (inputBuffer->motionEventsCount != 0) {
        for (uint64_t i = 0; i < inputBuffer->motionEventsCount; ++i) {
            GameActivityMotionEvent* motionEvent = &inputBuffer->motionEvents[i];

            if (motionEvent->pointerCount > 0) {
                int action = motionEvent->action;
                int actionMasked = action & AMOTION_EVENT_ACTION_MASK;
                int ptrIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>  AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

                if (ptrIndex < motionEvent->pointerCount) {
                    int motionPointerId = motionEvent->pointers[ptrIndex].id;
                    bool motionIsOnScreen = motionEvent->source == AINPUT_SOURCE_TOUCHSCREEN;
                    float motionX = GameActivityPointerAxes_getX(&motionEvent->pointers[ptrIndex]);
                    float motionY = GameActivityPointerAxes_getY(&motionEvent->pointers[ptrIndex]);

                    if (actionMasked == AMOTION_EVENT_ACTION_DOWN || actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN) {
                        doriax::Engine::systemTouchStart(motionPointerId, motionX, motionY);
                    } else if (actionMasked == AMOTION_EVENT_ACTION_UP || actionMasked == AMOTION_EVENT_ACTION_POINTER_UP) {
                        doriax::Engine::systemTouchEnd(motionPointerId, motionX, motionY);
                    } else if (actionMasked == AMOTION_EVENT_ACTION_MOVE) {
                        doriax::Engine::systemTouchMove(motionPointerId, motionX, motionY);
                    } else if (actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
                        doriax::Engine::systemTouchCancel();
                    }
                }
            }

        }
        android_app_clear_motion_events(inputBuffer);
    }
}

void NativeEngine::handleCommand(int32_t cmd) {
    switch (cmd) {
        case APP_CMD_SAVE_STATE:
            // The system has asked us to save our current state.
            mState.mHasFocus = mHasFocus;
            mApp->savedState = malloc(sizeof(mState));
            *((NativeEngineSavedState *) mApp->savedState) = mState;
            mApp->savedStateSize = sizeof(mState);
            break;
        case APP_CMD_INIT_WINDOW:
            if (mApp->window != NULL) {
                mHasWindow = true;
                SwappyGL_setWindow(mApp->window);
                if (mApp->savedStateSize == sizeof(mState) && mApp->savedState != nullptr) {
                    mState = *((NativeEngineSavedState *) mApp->savedState);
                    mHasFocus = mState.mHasFocus;
                } else {
                    // Workaround APP_CMD_GAINED_FOCUS issue where the focus state is not
                    // passed down from NativeActivity when restarting Activity
                    mHasFocus = appState.mHasFocus;
                }
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is going away -- kill the surface
            killSurface();
            mHasWindow = false;
            break;
        case APP_CMD_GAINED_FOCUS:
            mHasFocus = true;
            mState.mHasFocus = appState.mHasFocus = mHasFocus;
            break;
        case APP_CMD_LOST_FOCUS:
            mHasFocus = false;
            mState.mHasFocus = appState.mHasFocus = mHasFocus;
            break;
        case APP_CMD_PAUSE:
            doriax::Engine::systemPause();
            break;
        case APP_CMD_RESUME:
            doriax::Engine::systemResume();
            break;
        case APP_CMD_STOP:
            mIsVisible = false;
            break;
        case APP_CMD_START:
            mIsVisible = true;
            break;
        case APP_CMD_WINDOW_RESIZED:
        case APP_CMD_CONFIG_CHANGED:
            // Window was resized or some other configuration changed.
            // Note: we don't handle this event because we check the surface dimensions
            // every frame, so that's how we know it was resized. If you are NOT doing that,
            // then you need to handle this event!
            break;
        case APP_CMD_LOW_MEMORY:
            // system told us we have low memory. So if we are not visible, let's
            // cooperate by deallocating all of our graphic resources.
            if (!mHasWindow) {
                killGLObjects();
            }
            break;
        case APP_CMD_WINDOW_INSETS_CHANGED:
            updateSystemBarOffset();
            break;
        default:
            break;
    }

}