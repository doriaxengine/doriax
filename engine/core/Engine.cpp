//
// (c) 2026 Eduardo Doria.
//

#include "Engine.h"
#include "Scene.h"
#include "Input.h"
#include "render/SystemRender.h"
#include "script/LuaBinding.h"
#include "subsystem/AudioSystem.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/UISystem.h"
#include "pool/TexturePool.h"
#include "pool/TextureDataPool.h"
#include "pool/ShaderPool.h"
#include "pool/FontPool.h"
#ifndef NO_THREAD_SUPPORT
    #include "thread/ThreadPoolManager.h"
#endif

#include "sokol_time.h"

thread_local static bool asyncThread = false;

using namespace doriax;

//-----Doriax user config-----
std::vector<Scene*> Engine::scenes;
std::unordered_set<Scene*> Engine::oneTimeScenes;

Scene* Engine::mainScene = NULL;

int Engine::canvasWidth;
int Engine::canvasHeight;

int Engine::preferredCanvasWidth;
int Engine::preferredCanvasHeight;

Rect Engine::viewRect;

Scaling Engine::scalingMode = Scaling::FITWIDTH;
TextureStrategy Engine::textureStrategy = TextureStrategy::RESIZE;

bool Engine::callMouseInTouchEvent = false;
bool Engine::callTouchInMouseEvent = false;
bool Engine::useDegrees = true;
bool Engine::allowEventsOutCanvas = false;
bool Engine::ignoreEventsHandledByUI = true;
bool Engine::fixedTimeSceneUpdate = true;

bool Engine::uiEventReceived = false;

uint64_t Engine::lastTime = 0;
float Engine::updateTimeCount = 0;

double Engine::deltatime = 0;
float Engine::framerate = 0;

float Engine::updateTime = 0.01667; //60Hz

std::atomic<bool> Engine::viewLoaded = false;
std::atomic<bool> Engine::paused = false;

CursorType Engine::mouseCursorType = CursorType::ARROW;
bool Engine::showCursor = true;

Semaphore Engine::drawSemaphore;

Framebuffer* Engine::framebuffer = nullptr;

//-----Doriax user events-----
FunctionSubscribe<void()> Engine::onViewLoaded;
FunctionSubscribe<void()> Engine::onViewChanged;
FunctionSubscribe<void()> Engine::onViewDestroyed;
FunctionSubscribe<void()> Engine::onDraw;
FunctionSubscribe<void()> Engine::onUpdate;
FunctionSubscribe<void()> Engine::onPostUpdate;
FunctionSubscribe<void()> Engine::onPause;
FunctionSubscribe<void()> Engine::onResume;
FunctionSubscribe<void()> Engine::onShutdown;
FunctionSubscribe<void(int,float,float)> Engine::onTouchStart;
FunctionSubscribe<void(int,float,float)> Engine::onTouchEnd;
FunctionSubscribe<void(int,float,float)> Engine::onTouchMove;
FunctionSubscribe<void()> Engine::onTouchCancel;
FunctionSubscribe<void(int,float,float,int)> Engine::onMouseDown;
FunctionSubscribe<void(int,float,float,int)> Engine::onMouseUp;
FunctionSubscribe<void(float,float,int)> Engine::onMouseScroll;
FunctionSubscribe<void(float,float,int)> Engine::onMouseMove;
FunctionSubscribe<void()> Engine::onMouseEnter;
FunctionSubscribe<void()> Engine::onMouseLeave;
FunctionSubscribe<void(int,bool,int)> Engine::onKeyDown;
FunctionSubscribe<void(int,bool,int)> Engine::onKeyUp;
FunctionSubscribe<void(wchar_t)> Engine::onCharInput;

FunctionSubscribe<void()>& Engine::getOnInit() {
    // This static variable is guaranteed to be created the first time this function is called
    static FunctionSubscribe<void()> onInit;
    return onInit;
}

void Engine::setScene(Scene* scene){
    if (asyncThread)
        drawSemaphore.acquire();

    if (mainScene){
        auto it = std::find(scenes.begin(), scenes.end(), mainScene);

        if (it != scenes.end()) {
            scenes.erase(it);
        }
    }

    if (scene){
        //main scene is allways first scene
        scenes.insert(scenes.begin(), scene);
        includeScene(0, scene);
        mainScene = scene;
    }

    if (asyncThread)
        drawSemaphore.release();
}

Scene* Engine::getScene(){
    return Engine::scenes[0];
}

void Engine::addSceneLayer(Scene* scene){
    if (asyncThread)
        drawSemaphore.acquire();

    auto it = std::find(scenes.begin(), scenes.end(), scene);
    if (scene && (it == scenes.end())) {
        scenes.push_back(scene);
        includeScene(scenes.size()-1, scene);
    }

    if (asyncThread)
        drawSemaphore.release();
}

void Engine::executeSceneOnce(Scene* scene) {
    if (asyncThread)
        drawSemaphore.acquire();

    auto it = std::find(scenes.begin(), scenes.end(), scene);
    if (scene && (it == scenes.end())) {
        scenes.push_back(scene);
        includeScene(scenes.size()-1, scene);
    }

    oneTimeScenes.insert(scene);

    if (asyncThread)
        drawSemaphore.release();
}

void Engine::removeScene(Scene* scene){
    if (asyncThread)
        drawSemaphore.acquire();

    if (scene){
        auto it = std::find(scenes.begin(), scenes.end(), scene);

        if (it != scenes.end()) {
            scenes.erase(it);
        }

        if (mainScene == scene){
            mainScene = NULL;
        }
    }

    if (asyncThread)
        drawSemaphore.release();
}

void Engine::removeAllSceneLayers(bool removeOneTimeScenes){
    if (asyncThread)
        drawSemaphore.acquire();

    scenes.erase(
        std::remove_if(scenes.begin(), scenes.end(),
            [removeOneTimeScenes](Scene* scene) {
                // Keep main scene
                if (scene == mainScene)
                    return false;

                // If removeOneTimeScenes is false, keep scenes in oneTimeScenes
                if (!removeOneTimeScenes && oneTimeScenes.find(scene) != oneTimeScenes.end())
                    return false;

                // Remove all other scenes
                return true;
            }),
        scenes.end()
    );

    if (asyncThread)
        drawSemaphore.release();
}

void Engine::removeAllScenes(){
    if (asyncThread)
        drawSemaphore.acquire();

        scenes.clear();
        mainScene = NULL;

    if (asyncThread)
        drawSemaphore.release();
}

bool Engine::isSceneRunning(Scene* scene){
    if (!scene) {
        return false;
    }

    auto it = std::find(scenes.begin(), scenes.end(), scene);
    return it != scenes.end();
}

Scene* Engine::getMainScene(){
    return mainScene;
}

Scene* Engine::getLastScene(){
    return scenes.back();
}

void Engine::pauseGameEvents(bool pause) {
    // Pause ALL subscriptions for game events
    // Don't pause drawing and lifecycle events
    onUpdate.setEnabled(!pause);
    onPostUpdate.setEnabled(!pause);
    onTouchStart.setEnabled(!pause);
    onTouchEnd.setEnabled(!pause);
    onTouchMove.setEnabled(!pause);
    onTouchCancel.setEnabled(!pause);
    onMouseDown.setEnabled(!pause);
    onMouseUp.setEnabled(!pause);
    onMouseMove.setEnabled(!pause);
    onMouseScroll.setEnabled(!pause);
    onKeyDown.setEnabled(!pause);
    onKeyUp.setEnabled(!pause);
    onCharInput.setEnabled(!pause);
}

void Engine::includeScene(size_t index, Scene* scene){
    if (viewLoaded){
        scene->load();
        scene->updateCameraSize();
    }
}

int Engine::getCanvasWidth(){
    return Engine::canvasWidth;
}

int Engine::getCanvasHeight(){
    return Engine::canvasHeight;
}

void Engine::setCanvasSize(int canvasWidth, int canvasHeight){
    Engine::preferredCanvasWidth = canvasWidth;
    Engine::preferredCanvasHeight = canvasHeight;

    if (viewLoaded){
        calculateCanvas();
    }
}

int Engine::getPreferredCanvasWidth(){
    return Engine::preferredCanvasWidth;
}

int Engine::getPreferredCanvasHeight(){
    return Engine::preferredCanvasHeight;
}

Rect Engine::getViewRect(){
    return viewRect;
}

void Engine::setScalingMode(Scaling scalingMode){
    Engine::scalingMode = scalingMode;

    if (viewLoaded){
        calculateCanvas();
    }
}

Scaling Engine::getScalingMode(){
    return scalingMode;
}

void Engine::setTextureStrategy(TextureStrategy textureStrategy){
    Engine::textureStrategy = textureStrategy;
}

TextureStrategy Engine::getTextureStrategy(){
    return textureStrategy;
}

void Engine::setCallMouseInTouchEvent(bool callMouseInTouchEvent){
    Engine::callMouseInTouchEvent = callMouseInTouchEvent;
}

bool Engine::isCallMouseInTouchEvent(){
    return Engine::callMouseInTouchEvent;
}

void Engine::setCallTouchInMouseEvent(bool callTouchInMouseEvent){
    Engine::callTouchInMouseEvent = callTouchInMouseEvent;
}

bool Engine::isCallTouchInMouseEvent(){
    return Engine::callTouchInMouseEvent;
}

void Engine::setUseDegrees(bool useDegrees){
    Engine::useDegrees = useDegrees;
}

bool Engine::isUseDegrees(){
    return Engine::useDegrees;
}

void Engine::setAllowEventsOutCanvas(bool allowEventsOutCanvas){
    Engine::allowEventsOutCanvas = allowEventsOutCanvas;
}

bool Engine::isAllowEventsOutCanvas(){
    return allowEventsOutCanvas;
}

void Engine::setIgnoreEventsHandledByUI(bool ignoreEventsHandledByUI){
    Engine::ignoreEventsHandledByUI = ignoreEventsHandledByUI;
}

bool Engine::isIgnoreEventsHandledByUI(){
    return ignoreEventsHandledByUI;
}

bool Engine::isUIEventReceived(){
    return uiEventReceived;
}

void Engine::setFixedTimeSceneUpdate(bool fixedTimeSceneUpdate) {
    Engine::fixedTimeSceneUpdate = fixedTimeSceneUpdate;
}

bool Engine::isFixedTimeSceneUpdate() {
    return fixedTimeSceneUpdate;
}

void Engine::setUpdateTimeMS(unsigned int updateTimeMS){
    Engine::updateTime = updateTimeMS / 1000.0f;
}

void Engine::setUpdateTime(float updateTime){
    Engine::updateTime = updateTime;
}

float Engine::getUpdateTime(){
    return Engine::updateTime;
}

float Engine::getSceneUpdateTime(){
    if (isFixedTimeSceneUpdate()){
        return getUpdateTime();
    }else{
        return getDeltatime();
    }
}

void Engine::setMouseCursor(CursorType type){
    if (viewLoaded){
        System::instance().setMouseCursor(type);
    }
    mouseCursorType = type;
}

CursorType Engine::getMouseCursor(){
    return mouseCursorType;
}

void Engine::setShowCursor(bool showCursor){
    if (viewLoaded){
        System::instance().setShowCursor(showCursor);
    }
    Engine::showCursor = showCursor;
}

bool Engine::isShowCursor(){
    return showCursor;
}

Platform Engine::getPlatform(){
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    return Platform::Windows;
#elif __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_OS_SIMULATOR
    return Platform::iOS;
    #elif TARGET_OS_IPHONE
    return Platform::iOS;
    #elif TARGET_OS_MAC
    return Platform::MacOS;
    #endif
#elif __ANDROID__
    return Platform::Android;
#elif __EMSCRIPTEN__
    return Platform::Web;
#elif __linux__
    return Platform::Linux;
#endif
}

GraphicBackend Engine::getGraphicBackend(){
#if defined(SOKOL_GLCORE)
    return GraphicBackend::GLCORE;
#elif defined(SOKOL_GLES3)
    return GraphicBackend::GLES3;
#elif defined(SOKOL_D3D11)
    return GraphicBackend::D3D11;
#elif defined(SOKOL_METAL)
    return GraphicBackend::METAL;
#elif defined(SOKOL_WGPU)
    return GraphicBackend::WGPU;
#elif defined(DORIAX_APPLE) //Xcode template
    return GraphicBackend::METAL;
#endif
}

bool Engine::isOpenGL(){
    GraphicBackend gbackend = getGraphicBackend();

    if (gbackend == GraphicBackend::GLCORE)
        return true;
    if (gbackend == GraphicBackend::GLES3)
        return true;

    return false;
}

float Engine::getFramerate(){
    return framerate;
}

float Engine::getDeltatime(){
    return deltatime;
}

void Engine::startAsyncThread(){
    #ifndef NO_THREAD_SUPPORT
        asyncThread = true;
    #else
        Log::warn("Threads are not available");
    #endif
}

void Engine::commitThreadQueue(){
    SystemRender::commitQueue();
}

void Engine::endAsyncThread(){
    commitThreadQueue();
    asyncThread = false;
}

bool Engine::isAsyncThread(){
    return asyncThread;
}

bool Engine::isViewLoaded(){
    return viewLoaded;
}

void Engine::setMaxResourceLoadingThreads(size_t maxThreads) {
    #ifndef NO_THREAD_SUPPORT
        ThreadPoolManager::shutdown();
        ThreadPoolManager::initialize(maxThreads);
    #endif
}

size_t Engine::getQueuedResourceCount() {
    #ifndef NO_THREAD_SUPPORT
        return ThreadPoolManager::getInstance().getQueueSize();
    #else
        return 0;
    #endif
}

void Engine::setFramebuffer(Framebuffer* framebuffer){
    Engine::framebuffer = framebuffer;
}

Framebuffer* Engine::getFramebuffer(){
    return Engine::framebuffer;
}

void Engine::removeSubscriptionsByTag(const std::string& substring) {
    onUpdate.removeByTagSubstring(substring);
    onPostUpdate.removeByTagSubstring(substring);

    onTouchStart.removeByTagSubstring(substring);
    onTouchEnd.removeByTagSubstring(substring);
    onTouchMove.removeByTagSubstring(substring);
    onTouchCancel.removeByTagSubstring(substring);

    onMouseDown.removeByTagSubstring(substring);
    onMouseUp.removeByTagSubstring(substring);
    onMouseScroll.removeByTagSubstring(substring);
    onMouseMove.removeByTagSubstring(substring);
    onMouseEnter.removeByTagSubstring(substring);
    onMouseLeave.removeByTagSubstring(substring);

    onKeyDown.removeByTagSubstring(substring);
    onKeyUp.removeByTagSubstring(substring);
    onCharInput.removeByTagSubstring(substring);

    onViewLoaded.removeByTagSubstring(substring);
    onViewChanged.removeByTagSubstring(substring);
    onViewDestroyed.removeByTagSubstring(substring);
    onDraw.removeByTagSubstring(substring);
    onPause.removeByTagSubstring(substring);
    onResume.removeByTagSubstring(substring);
    onShutdown.removeByTagSubstring(substring);
}

void Engine::clearAllSubscriptions(bool includeLifecycle) {
    // Gameplay/input/update events
    onUpdate.clear();
    onPostUpdate.clear();

    onTouchStart.clear();
    onTouchEnd.clear();
    onTouchMove.clear();
    onTouchCancel.clear();

    onMouseDown.clear();
    onMouseUp.clear();
    onMouseScroll.clear();
    onMouseMove.clear();
    onMouseEnter.clear();
    onMouseLeave.clear();

    onKeyDown.clear();
    onKeyUp.clear();
    onCharInput.clear();

    if (includeLifecycle) {
        onViewLoaded.clear();
        onViewChanged.clear();
        onViewDestroyed.clear();
        onDraw.clear();
        onPause.clear();
        onResume.clear();
        onShutdown.clear();
    }
}

void Engine::calculateCanvas(){
    Engine::canvasWidth = preferredCanvasWidth;
    Engine::canvasHeight = preferredCanvasHeight;

    int screenWidth = System::instance().getScreenWidth();
    int screenHeight = System::instance().getScreenHeight();

    //When canvas size is changed
    if (screenWidth != 0 && screenHeight != 0){
        if (scalingMode == Scaling::FITWIDTH){
            Engine::canvasWidth = preferredCanvasWidth;
            Engine::canvasHeight = screenHeight * preferredCanvasWidth / screenWidth;
        }
        if (scalingMode == Scaling::FITHEIGHT){
            Engine::canvasHeight = preferredCanvasHeight;
            Engine::canvasWidth = screenWidth * preferredCanvasHeight / screenHeight;
        }
        if (scalingMode == Scaling::NATIVE){
            Engine::canvasHeight = screenHeight;
            Engine::canvasWidth = screenWidth;
        }
    }
}

void Engine::systemInit(int argc, char* argv[], System* system){
    #ifndef NO_THREAD_SUPPORT
        size_t maxThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
        ThreadPoolManager::initialize(maxThreads);
    #endif

    System::setSystemInstance(system);

    Engine::setCanvasSize(1000,480);

    lastTime = 0;
    updateTimeCount = 0;
    viewLoaded = false;
    paused = false;

    drawSemaphore.release();

    stm_setup();
    
    std::vector<std::string> args(argv, argv + argc);
    System::instance().args = args;

    LuaBinding::createLuaState();

    #ifndef NO_LUA_INIT
    LuaBinding::init();
    #endif
    #ifndef NO_CPP_INIT
    getOnInit().call();
    #endif
}

void Engine::systemViewLoaded(){
    SystemRender::setup();

    if (mouseCursorType != CursorType::ARROW){
        System::instance().setMouseCursor(mouseCursorType);
    }
    if (!showCursor){
        System::instance().setShowCursor(showCursor);
    }

    asyncThread = false;

    viewLoaded = true;
    onViewLoaded.call();

    if (framebuffer){
        framebuffer->create();
    }
    
    for (int i = 0; i < scenes.size(); i++){
        scenes[i]->load();
    }
}

void Engine::systemViewChanged(){
    calculateCanvas();

    int screenWidth = System::instance().getScreenWidth();
    int screenHeight = System::instance().getScreenHeight();
    
    int viewX = 0;
    int viewY = 0;
    int viewWidth = screenWidth;
    int viewHeight = screenHeight;
    
    float screenAspect = (float)screenWidth / (float)screenHeight;
    float canvasAspect = (float)Engine::getPreferredCanvasWidth() / (float)Engine::getPreferredCanvasHeight();
    
    //When canvas size is not changed
    if (Engine::getScalingMode() == Scaling::LETTERBOX){
        if (screenAspect < canvasAspect){
            float aspect = (float)screenWidth / (float)Engine::getPreferredCanvasWidth();
            int newHeight = (int)((float)Engine::getPreferredCanvasHeight() * aspect);
            int dif = screenHeight - newHeight;
            viewY = (dif/2);
            viewHeight = screenHeight-(viewY*2); //diff could be odd, for this use view*2
        }else{
            float aspect = (float)screenHeight / (float)Engine::getPreferredCanvasHeight();
            int newWidth = (int)((float)Engine::getPreferredCanvasWidth() * aspect);
            int dif = screenWidth - newWidth;
            viewX = (dif/2);
            viewWidth = screenWidth-(viewX*2);
        }
    }
    
    if (Engine::getScalingMode() == Scaling::CROP){
        if (screenAspect > canvasAspect){
            float aspect = (float)screenWidth / (float)Engine::getPreferredCanvasWidth();
            int newHeight = (int)((float)Engine::getPreferredCanvasHeight() * aspect);
            int dif = screenHeight - newHeight;
            viewY = (dif/2);
            viewHeight = screenHeight-(viewY*2);
        }else{
            float aspect = (float)screenHeight / (float)Engine::getPreferredCanvasHeight();
            int newWidth = (int)((float)Engine::getPreferredCanvasWidth() * aspect);
            int dif = screenWidth - newWidth;
            viewX = (dif/2);
            viewWidth = screenWidth-(viewX*2);
        }
    }
    
    // S_SCALING_STRETCH do not need nothing
    
    viewRect.setRect(viewX, viewY, viewWidth, viewHeight);

    if (framebuffer){
        framebuffer->setWidth(screenWidth);
        framebuffer->setHeight(screenHeight);

        if (framebuffer->isCreated()){
            framebuffer->destroy();
            framebuffer->create();
        }
    }

    for (int i = 0; i < scenes.size(); i++){
        scenes[i]->updateCameraSize();
    }

    onViewChanged.call();
}

void Engine::systemDraw(){
    const int MAX_UPDATES_PER_FRAME = 100;

    //Deltatime in seconds
    deltatime = stm_sec(stm_laptime(&lastTime));
    framerate = 1 / deltatime;

    drawSemaphore.acquire();

    SystemRender::executeQueue();

    // avoid increment updateTimeCount after resume
    if (!paused) {
        // clamp deltaTime to prevent large jumps
        //deltatime = std::min(deltatime, 0.25);  // bad for physics?
        updateTimeCount += deltatime;

        int updateLoops = 0;
        while (updateTimeCount >= updateTime && updateLoops < MAX_UPDATES_PER_FRAME) {
            Engine::onUpdate.call();

            if (isFixedTimeSceneUpdate()) {
                for (int i = 0; i < scenes.size(); i++) {
                    scenes[i]->update(updateTime);
                }
                Engine::onPostUpdate.call();
            }

            updateTimeCount -= updateTime;
            updateLoops++;
        }

        if (updateLoops >= MAX_UPDATES_PER_FRAME) {
            Log::warn("Dropping frames - More than %i updates per frame", MAX_UPDATES_PER_FRAME);
            //updateTimeCount = 0;  // bad for physics?
        }

        if (!isFixedTimeSceneUpdate()) {
            for (int i = 0; i < scenes.size(); i++) {
                scenes[i]->update(deltatime);
            }
            Engine::onPostUpdate.call();
        }
    }

    Engine::onDraw.call();

    for (int i = 0; i < scenes.size(); i++){
        scenes[i]->draw();
    }

    SystemRender::commit();

    if (!oneTimeScenes.empty()) {
        std::unordered_set<Scene*> loadedScenes;

        for (Scene* scene : oneTimeScenes) {
            if (scene == mainScene) {
                continue;
            }
            bool loaded = scene->getSystem<RenderSystem>()->isAllLoaded();
            auto sceneIt = std::find(scenes.begin(), scenes.end(), scene);
            if (loaded && sceneIt != scenes.end()) {
                scenes.erase(sceneIt);
                loadedScenes.insert(scene);
            }
        }

        // Only remove scenes that have been fully loaded
        for (Scene* scene : loadedScenes) {
            oneTimeScenes.erase(scene);
        }
    }

    drawSemaphore.release();
}

void Engine::systemViewDestroyed(){
    TextureDataPool::requestShutdown();

    drawSemaphore.acquire();
    
    viewLoaded = false;
    Engine::onViewDestroyed.call();

    //TODO: must destroy all scenes (even if not a layer)
    for (int i = 0; i < scenes.size(); i++){
        scenes[i]->destroy();
    }

    oneTimeScenes.clear();

    SystemRender::shutdown();

    TexturePool::clear();
    TextureDataPool::clear();
    ShaderPool::clear();
    FontPool::clear();

    drawSemaphore.release();

    #ifndef NO_THREAD_SUPPORT
        ThreadPoolManager::shutdown();
    #endif
}

void Engine::systemShutdown(){
    Engine::onShutdown.call();

    LuaBinding::cleanup();

    removeAllSceneLayers(true);
}

void Engine::systemPause(){
    AudioSystem::pauseAll();
    Engine::onPause.call();
    paused = true;
}

void Engine::systemResume(){
    AudioSystem::resumeAll();
    Engine::onResume.call();
    paused = false;
}

bool Engine::transformCoordPos(float& x, float& y){

    x = (x - viewRect.getX()) / viewRect.getWidth();
    y = (y - viewRect.getY()) / viewRect.getHeight();
    
    x = (float)Engine::getCanvasWidth() * x;
    y = (float)Engine::getCanvasHeight() * y;
    
    if (allowEventsOutCanvas)
        return true;
    
    return ((x >= 0) && (x <= Engine::getCanvasWidth()) && (y >= 0) && (y <= Engine::getCanvasHeight()));
}

void Engine::systemTouchStart(int pointer, float x, float y){
    if (transformCoordPos(x, y)){
        //-----------------
        Input::addTouch(pointer, x, y);

        uiEventReceived = false;
        for (int i = 0; i < scenes.size(); i++){
            if (scenes[i]->canReceiveUIEvents())
                if (scenes[i]->getSystem<UISystem>()->eventOnPointerDown(x, y))
                    uiEventReceived = true;
        }

        if (!ignoreEventsHandledByUI || !uiEventReceived){
            Engine::onTouchStart.call(pointer, x, y);
            //-----------------
            if (Engine::isCallMouseInTouchEvent()){
                //-----------------
                Input::addMousePressed(S_MOUSE_BUTTON_1);
                Input::setMousePosition(x, y);
                Engine::onMouseDown.call(S_MOUSE_BUTTON_1, x, y, 0);
                //-----------------
            }
        }
        uiEventReceived = false;
    }
}

void Engine::systemTouchEnd(int pointer, float x, float y){
    if (transformCoordPos(x, y)){
        //-----------------
        Input::removeTouch(pointer);

        uiEventReceived = false;
        for (int i = 0; i < scenes.size(); i++){
            if (scenes[i]->canReceiveUIEvents())
                if (scenes[i]->getSystem<UISystem>()->eventOnPointerUp(x, y))
                    uiEventReceived = true;
        }

        if (!ignoreEventsHandledByUI || !uiEventReceived){
            Engine::onTouchEnd.call(pointer, x, y);
            //-----------------
            if (Engine::isCallMouseInTouchEvent()){
                //-----------------
                Input::releaseMousePressed(S_MOUSE_BUTTON_1);
                Input::setMousePosition(x, y);
                Engine::onMouseUp.call(S_MOUSE_BUTTON_1, x, y, 0);
                //-----------------
            }
        }
        uiEventReceived = false;
    }
}

void Engine::systemTouchMove(int pointer, float x, float y){
    if (transformCoordPos(x, y)){
        //-----------------
        Input::setTouchPosition(pointer, x, y);

        uiEventReceived = false;
        for (int i = 0; i < scenes.size(); i++){
            if (scenes[i]->canReceiveUIEvents())
                if (scenes[i]->getSystem<UISystem>()->eventOnPointerMove(x, y))
                    uiEventReceived = true;
        }

        if (!ignoreEventsHandledByUI || !uiEventReceived){
            Engine::onTouchMove.call(pointer, x, y);
            //-----------------
            if (Engine::isCallMouseInTouchEvent()){
                //-----------------
                Input::setMousePosition(x, y);
                Engine::onMouseMove.call(x, y, 0);
                //-----------------
            }
        }
        uiEventReceived = false;
    }
}

void Engine::systemTouchCancel(){
    //-----------------
    Input::clearTouches();
    Engine::onTouchCancel.call();
    //-----------------
}

void Engine::systemMouseDown(int button, float x, float y, int mods){
    if (transformCoordPos(x, y)){
        //-----------------
        Input::addMousePressed(button);
        Input::setMousePosition(x, y);
        if (mods != 0)
            Input::setModifiers(mods);

        uiEventReceived = false;
        for (int i = 0; i < scenes.size(); i++){
            if (scenes[i]->canReceiveUIEvents())
                if (button == S_MOUSE_BUTTON_1)
                    if (scenes[i]->getSystem<UISystem>()->eventOnPointerDown(x, y))
                        uiEventReceived = true;
        }

        if (!ignoreEventsHandledByUI || !uiEventReceived){
            Engine::onMouseDown.call(button, x, y, mods);
            //-----------------
            if (Engine::isCallTouchInMouseEvent()){
                //-----------------
                Input::addTouch(0, x, y);
                Engine::onTouchStart.call(0, x, y);
                //-----------------
            }
        }
        uiEventReceived = false;
    }
}
void Engine::systemMouseUp(int button, float x, float y, int mods){
    if (transformCoordPos(x, y)){
        //-----------------
        Input::releaseMousePressed(button);
        Input::setMousePosition(x, y);
        if (mods != 0)
            Input::setModifiers(mods);

        uiEventReceived = false;
        for (int i = 0; i < scenes.size(); i++){
            if (scenes[i]->canReceiveUIEvents())
                if (button == S_MOUSE_BUTTON_1)
                    if (scenes[i]->getSystem<UISystem>()->eventOnPointerUp(x, y))
                        uiEventReceived = true;
        }

        if (!ignoreEventsHandledByUI || !uiEventReceived){
            Engine::onMouseUp.call(button, x, y, mods);
            //-----------------
            if (Engine::isCallTouchInMouseEvent()){
                //-----------------
                Input::removeTouch(0);
                Engine::onTouchEnd.call(0, x, y);
                //-----------------
            }
        }
        uiEventReceived = false;
    }
}

void Engine::systemMouseMove(float x, float y, int mods){
    if (transformCoordPos(x, y)){
        //-----------------
        Input::setMousePosition(x, y);
        if (mods != 0)
            Input::setModifiers(mods);

        uiEventReceived = false;
        for (int i = 0; i < scenes.size(); i++){
            if (scenes[i]->canReceiveUIEvents())
                if (scenes[i]->getSystem<UISystem>()->eventOnPointerMove(x, y))
                    uiEventReceived = true;
        }

        if (!ignoreEventsHandledByUI || !uiEventReceived){
            Engine::onMouseMove.call(x, y, mods);
            //-----------------
            if (Engine::isCallTouchInMouseEvent()){
                //-----------------
                if (Input::isMousePressed(S_MOUSE_BUTTON_LEFT) || Input::isMousePressed(S_MOUSE_BUTTON_RIGHT)){
                    Input::setTouchPosition(0, x, y);
                    Engine::onTouchMove.call(0, x, y);
                }
                //-----------------
            }
        }
        uiEventReceived = false;
    }
}

void Engine::systemMouseScroll(float xoffset, float yoffset, int mods){
    //-----------------
    Input::setMouseScroll(xoffset, yoffset);
    if (mods != 0)
        Input::setModifiers(mods);
    Engine::onMouseScroll.call(xoffset, yoffset, mods);
    //-----------------
}

void Engine::systemMouseEnter(){
    //-----------------
    Input::addMouseEntered();
    Engine::onMouseEnter.call();
    //-----------------
}

void Engine::systemMouseLeave(){
    //-----------------
    Input::releaseMouseEntered();
    Engine::onMouseLeave.call();
    //-----------------
}

void Engine::systemKeyDown(int key, bool repeat, int mods){
    //-----------------
    Input::addKeyPressed(key);
    if (mods != 0)
        Input::setModifiers(mods);
    Engine::onKeyDown.call(key, repeat, mods);
    //-----------------
}

void Engine::systemKeyUp(int key, bool repeat, int mods){
    //-----------------
    Input::releaseKeyPressed(key);
    Input::setModifiers(mods); // Now it can be 0
    Engine::onKeyUp.call(key, repeat, mods);
    //-----------------
}

void Engine::systemCharInput(wchar_t codepoint){
    onCharInput.call(codepoint);

    for (int i = 0; i < scenes.size(); i++){
        if (scenes[i]->canReceiveUIEvents())
            scenes[i]->getSystem<UISystem>()->eventOnCharInput(codepoint);
    }
}
