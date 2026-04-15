#include "DoriaxWeb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten/emscripten.h>

#include "Engine.h"
#include "Input.h"
#include "System.h"
#include "Log.h"
#include "AudioSystem.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif

std::string DoriaxWeb::canvas;

int DoriaxWeb::syncWaitTime;
bool DoriaxWeb::enabledIDB;

int DoriaxWeb::screenWidth;
int DoriaxWeb::screenHeight;

int DoriaxWeb::sampleCount = 0;

extern "C" {
    EMSCRIPTEN_KEEPALIVE 
    int getScreenWidth() {
        return doriax::System::instance().getScreenWidth();
    }

    EMSCRIPTEN_KEEPALIVE 
    int getScreenHeight() {
        return doriax::System::instance().getScreenHeight();
    }

    EMSCRIPTEN_KEEPALIVE 
    void changeCanvasSize(int nWidth, int nHeight){
        DoriaxWeb::changeCanvasSize(nWidth, nHeight);
    }

    EMSCRIPTEN_KEEPALIVE 
    void syncfs_enable_callback(const char* err) {
	    if (!err || err[0]) {
		    doriax::Log::error("Failed to enable IndexedDB: %s", err);
            DoriaxWeb::setEnabledIDB(false);
	    }else{
            DoriaxWeb::setEnabledIDB(true);
        }
    }

    EMSCRIPTEN_KEEPALIVE 
    void syncfs_callback(const char* err) {
	    if (!err || err[0]) {
		    doriax::Log::error("Failed to save in iDB file system: %s", err);
	    }
    }

    EMSCRIPTEN_KEEPALIVE 
    void crazygamesad_started_callback() {
        doriax::Engine::systemPause();
    }

    EMSCRIPTEN_KEEPALIVE 
    void crazygamesad_finished_callback() {
        doriax::Engine::systemResume();
    }

    EMSCRIPTEN_KEEPALIVE 
    void crazygamesad_error_callback(const char* err) {
	    if (!err || err[0]) {
		    doriax::Log::error("Failed to load CrazyGames ad: %s", err);
	    }
        doriax::Engine::systemResume();
    }
}



DoriaxWeb::DoriaxWeb(){

}

void DoriaxWeb::setEnabledIDB(bool enabledIDB){
    DoriaxWeb::enabledIDB = enabledIDB;
}

int DoriaxWeb::getScreenWidth(){
    return screenWidth;
}

int DoriaxWeb::getScreenHeight(){
    return screenHeight;
}

int DoriaxWeb::getSampleCount(){
    return sampleCount;
}

int DoriaxWeb::init(int argc, char **argv){

    canvas = "#canvas";

    int sWidth = DEFAULT_WINDOW_WIDTH;
    int sHeight = DEFAULT_WINDOW_HEIGHT;
    if ((argv[1] != NULL && argv[1] != 0) && (argv[2] != NULL && argv[2] != 0)){
        sWidth = atoi(argv[1]);
        sHeight = atoi(argv[2]);
    }

    bool antiAlias = true;

    EMSCRIPTEN_RESULT ret = emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, true, key_callback);
    ret = emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, true, key_callback);
    ret = emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, true, key_callback);
    ret = emscripten_set_mousedown_callback(canvas.c_str(), 0, true, mouse_callback);
    ret = emscripten_set_mouseup_callback(canvas.c_str(), 0, true, mouse_callback);
    ret = emscripten_set_mousemove_callback(canvas.c_str(), 0, true, mouse_callback);
    ret = emscripten_set_mouseenter_callback(canvas.c_str(), 0, true, mouse_callback);
    ret = emscripten_set_mouseleave_callback(canvas.c_str(), 0, true, mouse_callback);
    ret = emscripten_set_wheel_callback(canvas.c_str(), 0, true, wheel_callback);
    ret = emscripten_set_touchstart_callback(canvas.c_str(), 0, true, touch_callback);
    ret = emscripten_set_touchmove_callback(canvas.c_str(), 0, true, touch_callback);
    ret = emscripten_set_touchend_callback(canvas.c_str(), 0, true, touch_callback);
    ret = emscripten_set_touchcancel_callback(canvas.c_str(), 0, true, touch_callback);
    ret = emscripten_set_webglcontextlost_callback(canvas.c_str(), 0, true, webgl_context_callback);
    ret = emscripten_set_webglcontextrestored_callback(canvas.c_str(), 0, true, webgl_context_callback);

    // necessary if canvas size is relative by window
    //ret = emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, resize_callback);

    // removed because emscripten_set_canvas_element_size is not working on this callback
    //ret = emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, 0, 1, fullscreenchange_callback);

    syncWaitTime = 0;
    enabledIDB = false;
    EM_ASM(
		FS.mkdir('/datafs');
		FS.mount(IDBFS, {}, '/datafs');
		FS.syncfs(true, function(err) {
			ccall('syncfs_enable_callback', null, ['string'], [err ? err.message : ""]);
		});
	);

    doriax::Engine::systemInit(argc, argv, new DoriaxWeb());

    sampleCount = (antiAlias) ? 4 : 1;

    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.alpha = true;
    attr.depth = true;
    attr.stencil = true;
    attr.antialias = false;
    attr.preserveDrawingBuffer = false;
    attr.failIfMajorPerformanceCaveat = false;
    attr.enableExtensionsByDefault = true;
    attr.premultipliedAlpha = true;
    attr.majorVersion = 2;
    attr.minorVersion = 0;
    attr.antialias = antiAlias;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(canvas.c_str(), &attr);
    
    emscripten_webgl_make_context_current(ctx);

    DoriaxWeb::screenWidth = sWidth;
    DoriaxWeb::screenHeight = sHeight;
    doriax::Engine::systemViewLoaded();
    changeCanvasSize(sWidth, sHeight);

    emscripten_request_animation_frame_loop(renderLoop, 0);

    return 0;
}

void DoriaxWeb::changeCanvasSize(int width, int height){
    emscripten_set_canvas_element_size(canvas.c_str(), width, height);

    DoriaxWeb::screenWidth = width;
    DoriaxWeb::screenHeight = height;
    doriax::Engine::systemViewChanged();
}

bool DoriaxWeb::isFullscreen(){
    EmscriptenFullscreenChangeEvent fsce;
    EMSCRIPTEN_RESULT ret = emscripten_get_fullscreen_status(&fsce);

    return fsce.isFullscreen;
}

void DoriaxWeb::requestFullscreen(){
    EmscriptenFullscreenStrategy strategy;
    strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
    strategy.canvasResolutionScaleMode = EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
    strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
    strategy.canvasResizedCallback = canvas_resize;
    strategy.canvasResizedCallbackUserData = NULL;

    EMSCRIPTEN_RESULT ret = emscripten_request_fullscreen_strategy(canvas.c_str(), 1, &strategy);
}

void DoriaxWeb::exitFullscreen(){
    EMSCRIPTEN_RESULT ret = emscripten_exit_fullscreen();
}

void DoriaxWeb::setMouseCursor(doriax::CursorType type){
    std::string cursor;

    if (type == doriax::CursorType::ARROW){
        cursor = "default";
    }else if (type == doriax::CursorType::IBEAM){
        cursor = "text";
    }else if (type == doriax::CursorType::CROSSHAIR){
        cursor = "crosshair";
    }else if (type == doriax::CursorType::POINTING_HAND){
        cursor = "pointer";
    }else if (type == doriax::CursorType::RESIZE_EW){
        cursor = "ew-resize";
    }else if (type == doriax::CursorType::RESIZE_NS){
        cursor = "ns-resize";
    }else if (type == doriax::CursorType::RESIZE_NWSE){
        cursor = "nwse-resize";
    }else if (type == doriax::CursorType::RESIZE_NESW){
        cursor = "nesw-resize";
    }else if (type == doriax::CursorType::RESIZE_ALL){
        cursor = "all-scroll";
    }else if (type == doriax::CursorType::NOT_ALLOWED){
        cursor = "not-allowed";
    }else{
        cursor = "auto";
    }

    EM_ASM({Module.canvas.style.cursor = UTF8ToString($0);}, cursor.c_str());

}

void DoriaxWeb::setShowCursor(bool showCursor){
    if (!showCursor){
        EM_ASM({Module.canvas.style.cursor = UTF8ToString($0);}, "none");
    }else{
        setMouseCursor(doriax::Engine::getMouseCursor());
    }
}

std::string DoriaxWeb::getUserDataPath(){
    return "/datafs";
}

bool DoriaxWeb::syncFileSystem(){
    if (enabledIDB)
        syncWaitTime = 500;

    return true;
}

EM_BOOL DoriaxWeb::renderLoop(double time, void* userdata){
    doriax::Engine::systemDraw();

    if (syncWaitTime > 0) {
		syncWaitTime -= (int)(doriax::Engine::getDeltatime()*1000);

        if (syncWaitTime <= 0){
            EM_ASM(
	            FS.syncfs(function(err) {
                    ccall('syncfs_callback', null, ['string'], [err ? err.message : ""]);
		        });
	        );
        }
    }

    return EM_TRUE;
}

EM_BOOL DoriaxWeb::resize_callback(int event_type, const EmscriptenUiEvent* ui_event, void* user_data){
    int w, h;
    emscripten_get_canvas_element_size(canvas.c_str(), &w, &h);
    changeCanvasSize(w, h);

    return 0;
}

EM_BOOL DoriaxWeb::canvas_resize(int eventType, const void *reserved, void *userData){
    int w, h;
    emscripten_get_canvas_element_size(canvas.c_str(), &w, &h);

    DoriaxWeb::screenWidth = w;
    DoriaxWeb::screenHeight = h;
    doriax::Engine::systemViewChanged();

    return 0;
}

wchar_t DoriaxWeb::toCodepoint(const std::string &u){
    int l = u.length();
    if (l<1) return -1; unsigned char u0 = u[0]; if (u0>=0   && u0<=127) return u0;
    if (l<2) return -1; unsigned char u1 = u[1]; if (u0>=192 && u0<=223) return (u0-192)*64 + (u1-128);
    //if (u[0]==0xed && (u[1] & 0xa0) == 0xa0) return -1; //code points, 0xd800 to 0xdfff
    if (l<3) return -1; unsigned char u2 = u[2]; if (u0>=224 && u0<=239) return (u0-224)*4096 + (u1-128)*64 + (u2-128);
    if (l<4) return -1; unsigned char u3 = u[3]; if (u0>=240 && u0<=247) return (u0-240)*262144 + (u1-128)*4096 + (u2-128)*64 + (u3-128);
    return -1;
}

std::string DoriaxWeb::toUTF8(wchar_t cp) {
    char ch[5] = {0x00};
    if(cp <= 0x7F) { 
        ch[0] = cp; 
    }
    else if(cp <= 0x7FF) { 
        ch[0] = (cp >> 6) + 192; 
        ch[1] = (cp & 63) + 128; 
    }
    else if(0xd800 <= cp && cp <= 0xdfff) {}
    else if(cp <= 0xFFFF) { 
        ch[0] = (cp >> 12) + 224; 
        ch[1]= ((cp >> 6) & 63) + 128; 
        ch[2]= (cp & 63) + 128; 
    }
    else if(cp <= 0x10FFFF) { 
        ch[0] = (cp >> 18) + 240; 
        ch[1] = ((cp >> 12) & 63) + 128; 
        ch[2] = ((cp >> 6) & 63) + 128; 
        ch[3]= (cp & 63) + 128; 
    }
    return std::string(ch);
}

EM_BOOL DoriaxWeb::key_callback(int eventType, const EmscriptenKeyboardEvent *e, void *userData){
    std::string keyStorage;
    const char* key = e->key;
    if ((!key) || (!(*key))) {
        keyStorage = toUTF8(e->keyCode);
        key = keyStorage.c_str();
    }
    if (!(*key)) {
        keyStorage = toUTF8(e->which);
        key = keyStorage.c_str();
    }

    int modifiers = 0;
    if (e->ctrlKey) modifiers |= S_MODIFIER_CONTROL;
    if (e->shiftKey) modifiers |= S_MODIFIER_SHIFT;
    if (e->altKey)  modifiers |= S_MODIFIER_ALT;
    if (e->metaKey) modifiers |= S_MODIFIER_SUPER;

    int code = doriax_input(e->code);
    if (code==0){
        code = doriax_legacy_input(e->which);
    }
    if (code==0){
        code = doriax_legacy_input(e->keyCode);
    }

    int skey=0;
    if ((!strcmp(key,"Tab"))||(*key=='\t')) skey=1;
    if ((!strcmp(key,"Backspace"))||(*key=='\b')) skey=2;
    if ((!strcmp(key,"Enter"))||(*key=='\r')) skey=4;
    if ((!strcmp(key,"Escape"))||(*key=='\e')) skey=8;

    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN){
        doriax::Engine::systemKeyDown(code, e->repeat, modifiers);
        if (skey==1) doriax::Engine::systemCharInput('\t');
        if (skey==2) doriax::Engine::systemCharInput('\b');
        if (skey==4) doriax::Engine::systemCharInput('\r');
        if (skey==8) doriax::Engine::systemCharInput('\e');

    }else if (eventType == EMSCRIPTEN_EVENT_KEYUP){
        doriax::Engine::systemKeyUp(code, e->repeat, modifiers);

    }else if (eventType == EMSCRIPTEN_EVENT_KEYPRESS){
        wchar_t cp = toCodepoint(std::string(e->key));
        if (cp == 0) cp = e->which;
        if (cp == 0) cp = e->keyCode;

        if (skey == 0 && cp != 0){
            doriax::Engine::systemCharInput(cp);
        }
    }

    return 0;
}

EM_BOOL DoriaxWeb::mouse_callback(int eventType, const EmscriptenMouseEvent *e, void *userData) {

    int width = DoriaxWeb::screenWidth;
    int height = DoriaxWeb::screenHeight;

    if (e->targetX < 0) return 0;
    if (e->targetY < 0) return 0;
    if (e->targetX > width) return 0;
    if (e->targetY > height) return 0;

    int modifiers = 0;
    if (e->ctrlKey) modifiers |= S_MODIFIER_CONTROL;
    if (e->shiftKey) modifiers |= S_MODIFIER_SHIFT;
    if (e->altKey) modifiers |= S_MODIFIER_ALT;
    if (e->metaKey) modifiers |= S_MODIFIER_SUPER;

    doriax::Engine::systemMouseMove(e->targetX, e->targetY, modifiers);
    if (eventType == EMSCRIPTEN_EVENT_MOUSEDOWN && e->buttons != 0){
        doriax::Engine::systemMouseDown(doriax_mouse_button(e->button), e->targetX, e->targetY, modifiers);
    }
    if (eventType == EMSCRIPTEN_EVENT_MOUSEUP){
        doriax::Engine::systemMouseUp(doriax_mouse_button(e->button), e->targetX, e->targetY, modifiers);
    }
    if (eventType == EMSCRIPTEN_EVENT_MOUSEENTER){
        doriax::Engine::systemMouseEnter();
    }
    if (eventType == EMSCRIPTEN_EVENT_MOUSELEAVE){
        doriax::Engine::systemMouseLeave();
    }

    return 0;
}

EM_BOOL DoriaxWeb::wheel_callback(int eventType, const EmscriptenWheelEvent *e, void *userData) {
    float scale;
    switch (e->deltaMode) {
        case DOM_DELTA_PIXEL: scale = -0.04f; break;
        case DOM_DELTA_LINE:  scale = -1.33f; break;
        case DOM_DELTA_PAGE:  scale = -10.0f; break;
        default:              scale = -0.1f; break;
    }

    int modifiers = 0;
    if (e->mouse.ctrlKey) modifiers |= S_MODIFIER_CONTROL;
    if (e->mouse.shiftKey) modifiers |= S_MODIFIER_SHIFT;
    if (e->mouse.altKey) modifiers |= S_MODIFIER_ALT;
    if (e->mouse.metaKey) modifiers |= S_MODIFIER_SUPER;

    doriax::Engine::systemMouseScroll(scale * (float)e->deltaX, scale * (float)e->deltaY, modifiers);

  return 0;
}

EM_BOOL DoriaxWeb::touch_callback(int emsc_type, const EmscriptenTouchEvent* emsc_event, void* user_data) {
    bool retval = true;

    switch (emsc_type) {
        case EMSCRIPTEN_EVENT_TOUCHSTART:
            for (int i = 0; i < emsc_event->numTouches; i++) {
                const EmscriptenTouchPoint* src = &emsc_event->touches[i];
                doriax::Engine::systemTouchStart((int)src->identifier, src->targetX, src->targetY);
            }
            break;
        case EMSCRIPTEN_EVENT_TOUCHMOVE:
            for (int i = 0; i < emsc_event->numTouches; i++) {
                const EmscriptenTouchPoint* src = &emsc_event->touches[i];
                doriax::Engine::systemTouchMove((int)src->identifier, src->targetX, src->targetY);
            }
            break;
        case EMSCRIPTEN_EVENT_TOUCHEND:
            for (int i = 0; i < emsc_event->numTouches; i++) {
                const EmscriptenTouchPoint* src = &emsc_event->touches[i];
                doriax::Engine::systemTouchEnd((int)src->identifier, src->targetX, src->targetY);
            }
            break;
        case EMSCRIPTEN_EVENT_TOUCHCANCEL:
            doriax::Engine::systemTouchCancel();
            break;
        default:
            retval = false;
            break;
    }

  return retval;
}

EM_BOOL DoriaxWeb::webgl_context_callback(int emsc_type, const void* reserved, void* user_data) {
    switch (emsc_type) {
        case EMSCRIPTEN_EVENT_WEBGLCONTEXTLOST:     doriax::Engine::systemPause(); break;
        case EMSCRIPTEN_EVENT_WEBGLCONTEXTRESTORED: doriax::Engine::systemResume(); break;
        default:                                    break;
    }

    return 0;
}

int DoriaxWeb::doriax_mouse_button(int button){
    if (button == 0) return S_MOUSE_BUTTON_1;
    if (button == 2) return S_MOUSE_BUTTON_2;
    if (button == 1) return S_MOUSE_BUTTON_3;
    if (button == 3) return S_MOUSE_BUTTON_4;
    if (button == 4) return S_MOUSE_BUTTON_5;
    if (button == 5) return S_MOUSE_BUTTON_6;
    if (button == 6) return S_MOUSE_BUTTON_7;
    if (button == 7) return S_MOUSE_BUTTON_8;

    return -1;
}

int DoriaxWeb::doriax_input(const char code[32]){
    if (!strcmp(code,"Space")) return S_KEY_SPACE;
    if (!strcmp(code,"Quote")) return S_KEY_APOSTROPHE;  /* ' */
    if (!strcmp(code,"Comma")) return S_KEY_COMMA;  /* , */
    if (!strcmp(code,"Minus")) return S_KEY_MINUS;  /* - */
    if (!strcmp(code,"Period")) return S_KEY_PERIOD;  /* . */
    if (!strcmp(code,"Slash")) return S_KEY_SLASH;  /* / */
    if (!strcmp(code,"Digit0")) return S_KEY_0;
    if (!strcmp(code,"Digit1")) return S_KEY_1;
    if (!strcmp(code,"Digit2")) return S_KEY_2;
    if (!strcmp(code,"Digit3")) return S_KEY_3;
    if (!strcmp(code,"Digit4")) return S_KEY_4;
    if (!strcmp(code,"Digit5")) return S_KEY_5;
    if (!strcmp(code,"Digit6")) return S_KEY_6;
    if (!strcmp(code,"Digit7")) return S_KEY_7;
    if (!strcmp(code,"Digit8")) return S_KEY_8;
    if (!strcmp(code,"Digit9")) return S_KEY_9;
    if (!strcmp(code,"Semicolon")) return S_KEY_SEMICOLON; /* ; */
    if (!strcmp(code,"Equal")) return S_KEY_EQUAL;  /* = */
    if (!strcmp(code,"KeyA")) return S_KEY_A;
    if (!strcmp(code,"KeyB")) return S_KEY_B;
    if (!strcmp(code,"KeyC")) return S_KEY_C;
    if (!strcmp(code,"KeyD")) return S_KEY_D;
    if (!strcmp(code,"KeyE")) return S_KEY_E;
    if (!strcmp(code,"KeyF")) return S_KEY_F;
    if (!strcmp(code,"KeyG")) return S_KEY_G;
    if (!strcmp(code,"KeyH")) return S_KEY_H;
    if (!strcmp(code,"KeyI")) return S_KEY_I;
    if (!strcmp(code,"KeyJ")) return S_KEY_J;
    if (!strcmp(code,"KeyK")) return S_KEY_K;
    if (!strcmp(code,"KeyL")) return S_KEY_L;
    if (!strcmp(code,"KeyM")) return S_KEY_M;
    if (!strcmp(code,"KeyN")) return S_KEY_N;
    if (!strcmp(code,"KeyO")) return S_KEY_O;
    if (!strcmp(code,"KeyP")) return S_KEY_P;
    if (!strcmp(code,"KeyQ")) return S_KEY_Q;
    if (!strcmp(code,"KeyR")) return S_KEY_R;
    if (!strcmp(code,"KeyS")) return S_KEY_S;
    if (!strcmp(code,"KeyT")) return S_KEY_T;
    if (!strcmp(code,"KeyU")) return S_KEY_U;
    if (!strcmp(code,"KeyV")) return S_KEY_V;
    if (!strcmp(code,"KeyW")) return S_KEY_W;
    if (!strcmp(code,"KeyX")) return S_KEY_X;
    if (!strcmp(code,"KeyY")) return S_KEY_Y;
    if (!strcmp(code,"KeyZ")) return S_KEY_Z;
    if (!strcmp(code,"BracketLeft")) return S_KEY_LEFT_BRACKET; /* [ */
    if (!strcmp(code,"Backslash")) return S_KEY_BACKSLASH;  /* \ */
    if (!strcmp(code,"BracketLeft")) return S_KEY_RIGHT_BRACKET;  /* ] */
    if (!strcmp(code,"Backquote")) return S_KEY_GRAVE_ACCENT; /* ` */

    if (!strcmp(code,"Escape")) return S_KEY_ESCAPE;
    if (!strcmp(code,"Enter")) return S_KEY_ENTER;
    if (!strcmp(code,"Tab")) return S_KEY_TAB;
    if (!strcmp(code,"Backspace")) return S_KEY_BACKSPACE;
    if (!strcmp(code,"Insert")) return S_KEY_INSERT;
    if (!strcmp(code,"Delete")) return S_KEY_DELETE;
    if (!strcmp(code,"ArrowRight")) return S_KEY_RIGHT;
    if (!strcmp(code,"ArrowLeft")) return S_KEY_LEFT;
    if (!strcmp(code,"ArrowDown")) return S_KEY_DOWN;
    if (!strcmp(code,"ArrowUp")) return S_KEY_UP;
    if (!strcmp(code,"PageUp")) return S_KEY_PAGE_UP;
    if (!strcmp(code,"PageDown")) return S_KEY_PAGE_DOWN;
    if (!strcmp(code,"Home")) return S_KEY_HOME;
    if (!strcmp(code,"End")) return S_KEY_END;
    if (!strcmp(code,"CapsLock")) return S_KEY_CAPS_LOCK;
    if (!strcmp(code,"ScrollLock")) return S_KEY_SCROLL_LOCK;
    if (!strcmp(code,"NumLock")) return S_KEY_NUM_LOCK;
    if (!strcmp(code,"PrintScreen")) return S_KEY_PRINT_SCREEN;
    if (!strcmp(code,"Pause")) return S_KEY_PAUSE;
    if (!strcmp(code,"F1")) return S_KEY_F1;
    if (!strcmp(code,"F2")) return S_KEY_F2;
    if (!strcmp(code,"F3")) return S_KEY_F3;
    if (!strcmp(code,"F4")) return S_KEY_F4;
    if (!strcmp(code,"F5")) return S_KEY_F5;
    if (!strcmp(code,"F6")) return S_KEY_F6;
    if (!strcmp(code,"F7")) return S_KEY_F7;
    if (!strcmp(code,"F8")) return S_KEY_F8;
    if (!strcmp(code,"F9")) return S_KEY_F9;
    if (!strcmp(code,"F10")) return S_KEY_F10;
    if (!strcmp(code,"F11")) return S_KEY_F11;
    if (!strcmp(code,"F12")) return S_KEY_F12;
    if (!strcmp(code,"Numpad0")) return S_KEY_KP_0;
    if (!strcmp(code,"Numpad1")) return S_KEY_KP_1;
    if (!strcmp(code,"Numpad2")) return S_KEY_KP_2;
    if (!strcmp(code,"Numpad3")) return S_KEY_KP_3;
    if (!strcmp(code,"Numpad4")) return S_KEY_KP_4;
    if (!strcmp(code,"Numpad5")) return S_KEY_KP_5;
    if (!strcmp(code,"Numpad6")) return S_KEY_KP_6;
    if (!strcmp(code,"Numpad7")) return S_KEY_KP_7;
    if (!strcmp(code,"Numpad8")) return S_KEY_KP_8;
    if (!strcmp(code,"Numpad9")) return S_KEY_KP_9;
    if (!strcmp(code,"NumpadDecimal")) return S_KEY_KP_DECIMAL;
    if (!strcmp(code,"NumpadDivide")) return S_KEY_KP_DIVIDE;
    if (!strcmp(code,"NumpadMultiply")) return S_KEY_KP_MULTIPLY;
    if (!strcmp(code,"NumpadSubtract")) return S_KEY_KP_SUBTRACT;
    if (!strcmp(code,"NumpadAdd")) return S_KEY_KP_ADD;
    if (!strcmp(code,"NumpadEnter")) return S_KEY_KP_ENTER;
    if (!strcmp(code,"NumpadEqual")) return S_KEY_KP_EQUAL;
    if (!strcmp(code,"ShiftLeft")) return S_KEY_LEFT_SHIFT;
    if (!strcmp(code,"ControlLeft")) return S_KEY_LEFT_CONTROL;
    if (!strcmp(code,"AltLeft")) return S_KEY_LEFT_ALT;
    if (!strcmp(code,"OSLeft")) return S_KEY_LEFT_SUPER;
    if (!strcmp(code,"ShiftRight")) return S_KEY_RIGHT_SHIFT;
    if (!strcmp(code,"ControlRight")) return S_KEY_RIGHT_CONTROL;
    if (!strcmp(code,"AltRight")) return S_KEY_RIGHT_ALT;
    if (!strcmp(code,"OSRight")) return S_KEY_RIGHT_SUPER;
    if (!strcmp(code,"Menu")) return S_KEY_MENU;


    return 0;
}

int DoriaxWeb::doriax_legacy_input(int code){
    if (code==8) return S_KEY_DELETE;
    if (code==9) return S_KEY_TAB;
    if (code==13) return S_KEY_ENTER;
    if (code==16) return S_KEY_LEFT_SHIFT;
    if (code==17) return S_KEY_LEFT_CONTROL;
    if (code==18) return S_KEY_LEFT_ALT;
    if (code==19) return S_KEY_PAUSE;
    if (code==20) return S_KEY_CAPS_LOCK;
    if (code==27) return S_KEY_ESCAPE;
    if (code==32) return S_KEY_SPACE;
    if (code==33) return S_KEY_PAGE_UP;
    if (code==34) return S_KEY_PAGE_DOWN;
    if (code==35) return S_KEY_END;
    if (code==36) return S_KEY_HOME;
    if (code==37) return S_KEY_LEFT;
    if (code==38) return S_KEY_UP;
    if (code==39) return S_KEY_RIGHT;
    if (code==40) return S_KEY_DOWN;
    if (code==45) return S_KEY_INSERT;
    if (code==46) return S_KEY_DELETE;
    if (code==48) return S_KEY_0;
    if (code==49) return S_KEY_1;
    if (code==50) return S_KEY_2;
    if (code==51) return S_KEY_3;
    if (code==52) return S_KEY_4;
    if (code==53) return S_KEY_5;
    if (code==54) return S_KEY_6;
    if (code==55) return S_KEY_7;
    if (code==56) return S_KEY_8;
    if (code==57) return S_KEY_9;

    if (code==65) return S_KEY_A;
    if (code==66) return S_KEY_B;
    if (code==67) return S_KEY_C;
    if (code==68) return S_KEY_D;
    if (code==69) return S_KEY_E;
    if (code==70) return S_KEY_F;
    if (code==71) return S_KEY_G;
    if (code==72) return S_KEY_H;
    if (code==73) return S_KEY_I;
    if (code==74) return S_KEY_J;
    if (code==75) return S_KEY_K;
    if (code==76) return S_KEY_L;
    if (code==77) return S_KEY_M;
    if (code==78) return S_KEY_N;
    if (code==79) return S_KEY_O;
    if (code==80) return S_KEY_P;
    if (code==81) return S_KEY_Q;
    if (code==82) return S_KEY_R;
    if (code==83) return S_KEY_S;
    if (code==84) return S_KEY_T;
    if (code==85) return S_KEY_U;
    if (code==86) return S_KEY_V;
    if (code==87) return S_KEY_W;
    if (code==88) return S_KEY_X;
    if (code==89) return S_KEY_Y;
    if (code==90) return S_KEY_Z;

    if (code==91) return S_KEY_LEFT_SUPER;
    if (code==92) return S_KEY_RIGHT_SUPER;
    if (code==93) return 0; //SELECT key
    if (code==96) return S_KEY_KP_0;
    if (code==97) return S_KEY_KP_1;
    if (code==98) return S_KEY_KP_2;
    if (code==99) return S_KEY_KP_3;
    if (code==100) return S_KEY_KP_4;
    if (code==101) return S_KEY_KP_5;
    if (code==102) return S_KEY_KP_6;
    if (code==103) return S_KEY_KP_7;
    if (code==104) return S_KEY_KP_8;
    if (code==105) return S_KEY_KP_9;
    if (code==106) return S_KEY_KP_MULTIPLY;
    if (code==107) return S_KEY_KP_ADD;
    if (code==109) return S_KEY_KP_SUBTRACT;
    if (code==110) return S_KEY_KP_DECIMAL;
    if (code==111) return S_KEY_KP_DIVIDE;
    if (code==112) return S_KEY_F1;
    if (code==113) return S_KEY_F2;
    if (code==114) return S_KEY_F3;
    if (code==115) return S_KEY_F4;
    if (code==116) return S_KEY_F5;
    if (code==117) return S_KEY_F6;
    if (code==118) return S_KEY_F7;
    if (code==119) return S_KEY_F8;
    if (code==120) return S_KEY_F9;
    if (code==121) return S_KEY_F10;
    if (code==122) return S_KEY_F11;
    if (code==123) return S_KEY_F12;
    if (code==144) return S_KEY_NUM_LOCK;
    if (code==145) return S_KEY_SCROLL_LOCK;
    if (code==186) return S_KEY_SEMICOLON;
    if (code==187) return S_KEY_EQUAL;
    if (code==188) return S_KEY_COMMA;
    if (code==189) return S_KEY_MINUS;
    if (code==190) return S_KEY_PERIOD;
    if (code==191) return S_KEY_SLASH;
    if (code==192) return S_KEY_GRAVE_ACCENT;
    if (code==219) return S_KEY_LEFT_BRACKET;
    if (code==220) return S_KEY_BACKSLASH;
    if (code==222) return S_KEY_APOSTROPHE;

    return 0;
}

std::string DoriaxWeb::getStringForKey(const char *key, const std::string& defaultValue){
    char* value = (char*)EM_ASM_INT({
        var key = UTF8ToString($0);
        var val = localStorage.getItem(key);
        if (val) {
            var len = lengthBytesUTF8(val)+1;
            var buf = _malloc(len);
            stringToUTF8(val, buf, len);
            return buf;
        }
        return null;
    }, key);

    if (value){
        std::string strValue = value;
        free(value); //Each call to _malloc() must be paired with free() or heap memory will leak
        return strValue;
    }

    return defaultValue;
}

void DoriaxWeb::setStringForKey(const char* key, const std::string& value){
    EM_ASM_ARGS({
        var key = UTF8ToString($0);
        var value = UTF8ToString($1);
        localStorage.setItem(key, value);
    }, key, value.c_str());
}

void DoriaxWeb::removeKey(const char *key){
    EM_ASM_ARGS({
        var key = UTF8ToString($0);
        localStorage.removeItem(key);
    }, key);
}

void DoriaxWeb::initializeCrazyGamesSDK(){
    EM_ASM(
        function loadJS(FILE_URL, async = true) {
            let scriptEle = document.createElement("script");

            scriptEle.setAttribute("src", FILE_URL);
            scriptEle.setAttribute("type", "text/javascript");
            scriptEle.setAttribute("async", async);

            document.body.appendChild(scriptEle);

            // success event
            scriptEle.addEventListener("load", () => {
                //console.log("File loaded");
            });
            // error event
            scriptEle.addEventListener("error", (ev) => {
                console.log("Error on loading file", ev);
            });
        }

        loadJS("https://sdk.crazygames.com/crazygames-sdk-v2.js", false);
    );
}

void DoriaxWeb::showCrazyGamesAd(const std::string& type){
    EM_ASM({
        var adtype = UTF8ToString($0);
        const callbacks = ({
            adFinished: () => ccall('crazygamesad_finished_callback', null),
            adError: (error) => ccall('crazygamesad_error_callback', null, ['string'], [error ? error.message : ""]),
            adStarted: () => ccall('crazygamesad_started_callback', null)
        });
        window.CrazyGames.SDK.ad.requestAd(adtype, callbacks);
    }, type.c_str());
}

void DoriaxWeb::happytimeCrazyGames(){
    EM_ASM(
        window.CrazyGames.SDK.game.happytime();
    );
}

void DoriaxWeb::gameplayStartCrazyGames(){
    EM_ASM(
        window.CrazyGames.SDK.game.gameplayStart();
    );
}

void DoriaxWeb::gameplayStopCrazyGames(){
    EM_ASM(
        window.CrazyGames.SDK.game.gameplayStop();
    );
}

void DoriaxWeb::loadingStartCrazyGames(){
    EM_ASM(
        window.CrazyGames.SDK.game.sdkGameLoadingStart();
    );
}

void DoriaxWeb::loadingStopCrazyGames(){
    EM_ASM(
        window.CrazyGames.SDK.game.sdkGameLoadingStop();
    );
}