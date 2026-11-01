// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef engine_h
#define engine_h

#if defined(__EMSCRIPTEN__)
#  if !defined(__EMSCRIPTEN_PTHREADS__)
#    define NO_THREAD_SUPPORT
#  endif
#endif

#ifndef MAX_LIGHTS
#define MAX_LIGHTS 6
#endif

// 2D lights (Light2DComponent). Also the row count of the 1D polar shadow atlas:
// each shadow-casting 2D light renders its occluders into one atlas row.
#ifndef MAX_LIGHTS_2D
#define MAX_LIGHTS_2D 16
#endif

#ifndef SHADOW_CUBE_FACES
#define SHADOW_CUBE_FACES 6
#endif

#ifndef MAX_SHADOWCASCADES
#define MAX_SHADOWCASCADES 4
#endif

// Projective shadow atlas (directional + spot), one 2D depth map per slot. Each slot costs
// 7 fragment uniform vectors, so keep the grid small (GLES 3.0 guarantees only 224).
#ifndef SHADOW_ATLAS_COLS
#define SHADOW_ATLAS_COLS 3
#endif

#ifndef SHADOW_ATLAS_ROWS
#define SHADOW_ATLAS_ROWS 3
#endif

#ifndef MAX_SHADOW_ATLAS_SLOTS
#define MAX_SHADOW_ATLAS_SLOTS (SHADOW_ATLAS_COLS * SHADOW_ATLAS_ROWS)
#endif

// Point shadow atlas (omnidirectional). Each shadow-casting point light consumes
// SHADOW_CUBE_FACES consecutive slots (one per cube face) and is sampled only in the
// fragment shader, so this atlas is bounded by texture size rather than varyings.
#ifndef SHADOW_POINT_ATLAS_COLS
#define SHADOW_POINT_ATLAS_COLS 6
#endif

#ifndef SHADOW_POINT_ATLAS_ROWS
#define SHADOW_POINT_ATLAS_ROWS 4
#endif

#ifndef MAX_POINT_SHADOW_ATLAS_SLOTS
#define MAX_POINT_SHADOW_ATLAS_SLOTS (SHADOW_POINT_ATLAS_COLS * SHADOW_POINT_ATLAS_ROWS)
#endif

#ifndef MAX_POINT_SHADOW_LIGHTS
#define MAX_POINT_SHADOW_LIGHTS (MAX_POINT_SHADOW_ATLAS_SLOTS / SHADOW_CUBE_FACES)
#endif

#ifndef MAX_SUBMESHES
#define MAX_SUBMESHES 4
#endif

// detail levels per submesh, level 0 being the source geometry
#ifndef MAX_MESH_LODS
#define MAX_MESH_LODS 4
#endif

#ifndef MAX_TILEMAP_TILESRECT
#define MAX_TILEMAP_TILESRECT 64
#endif

#ifndef MAX_TILEMAP_TILES
#define MAX_TILEMAP_TILES 512
#endif

#ifndef MAX_SPRITE_FRAMES
#define MAX_SPRITE_FRAMES 32
#endif

// main font of a text plus its fallbacks
#ifndef MAX_TEXT_FONTS
#define MAX_TEXT_FONTS 4
#endif

#ifndef MAX_BONES
#define MAX_BONES 96
#endif

#ifndef MAX_MORPHTARGETS
#define MAX_MORPHTARGETS 8
#endif

// number of hemisphere samples in the SSAO kernel; must match the
// SSAO_KERNEL_SIZE define injected for the ssao.frag shader (ShaderBuilder)
#ifndef SSAO_KERNEL_SIZE
#define SSAO_KERNEL_SIZE 32
#endif

#ifndef MAX_EXTERNAL_BUFFERS
#define MAX_EXTERNAL_BUFFERS 20
#endif

#ifndef MAX_BROADPHASELAYER_3D
#define MAX_BROADPHASELAYER_3D 6
#endif

#ifndef MAX_SHAPES_2D
#define MAX_SHAPES_2D 4
#endif

#ifndef MAX_SHAPE_POINTS_2D
#define MAX_SHAPE_POINTS_2D 8
#endif

#ifndef MAX_SHAPES_3D
#define MAX_SHAPES_3D 4
#endif

#ifndef MAX_SHAPE_VERTICES_3D
#define MAX_SHAPE_VERTICES_3D 64
#endif

#ifndef MAX_SHAPE_INDICES_3D
#define MAX_SHAPE_INDICES_3D 192
#endif

#include "Export.h"
#include "System.h"
#include "util/FunctionSubscribe.h"
#include "math/Rect.h"
#include "util/ThreadUtils.h"
#include "texture/Framebuffer.h"
#include <atomic>
#include <mutex>
#include <unordered_set>

#define DORIAX_INIT \
    void init(); \
    namespace { \
        struct InitRegistrar { \
            InitRegistrar() { \
                doriax::Engine::getOnInit().add("user_init", &init); \
            } \
        }; \
        static InitRegistrar _doriax_init_registrar; \
    }

namespace doriax {

    class DORIAX_API Scene;
    //class Rect;

    enum class Scaling{
        FITWIDTH,
        FITHEIGHT,
        LETTERBOX,
        CROP,
        STRETCH,
        NATIVE
    };

    // texture power of two strategy
    enum class TextureStrategy{
        FIT,
        RESIZE,
        NONE
    };


    enum class Platform{
        MacOS,
        iOS,
        Web,
        Android,
        Linux,
        Windows
    };

    enum class GraphicBackend{
        GLCORE,
        GLES3,
        D3D11,
        METAL,
        WGPU,
        VULKAN
    };

    enum class BodyType{
        STATIC,
        KINEMATIC,
        DYNAMIC
    };

    enum class ResourceLoadState {
        NotStarted,
        Loading,
        Finished,
        Failed
    };

    class DORIAX_API Engine {
    public:
        // draw calls, instances and triangles submitted by the last systemDraw
        struct FrameStats{
            uint32_t drawCalls = 0;
            uint32_t instances = 0;
            uint64_t triangles = 0;
        };

    private:
        //-----Doriax config-----
        static std::vector<Scene*> scenes;
        static std::unordered_set<Scene*> oneTimeScenes;

        static Scene* mainScene;
        
        static int canvasWidth;
        static int canvasHeight;
        
        static int preferredCanvasWidth;
        static int preferredCanvasHeight;
        
        static Rect viewRect;

        static Scaling scalingMode;
        static TextureStrategy textureStrategy;
        
        static bool callMouseInTouchEvent;
        static bool callTouchInMouseEvent;
        static bool useDegrees;

        static bool allowEventsOutCanvas;

        static bool ignoreEventsHandledByUI;

        static bool uiEventReceived;

        static uint64_t lastTime;
        static double updateTimeCount;
        static int fixedStepsLeft;
        
        static double deltatime;
        static double maxDeltatime;
        static float framerate;
        static FrameStats frameStats;
        static FrameStats currentFrameStats;
        
        static double updateTime;

        static std::atomic<bool> viewLoaded;
        static std::atomic<bool> paused;
        static std::atomic<bool> asyncLoading;
        static bool frameRunning;

        static CursorType mouseCursorType;
        static MouseMode mouseMode;

        static Semaphore drawSemaphore;

        // drawSemaphore only excludes async threads from systemDraw(). Main-thread code
        // outside systemDraw() (scene activation, view callbacks, input dispatch) never takes
        // it, so it can still touch scenes/oneTimeScenes while a worker thread is inside
        // executeSceneOnce(). This mutex is what actually guards those two containers.
        // Never block on drawSemaphore while holding it.
        static std::mutex sceneListMutex;

        static Framebuffer* framebuffer;
        // stacked scenes draw here so the frame keeps a single swapchain pass
        static Framebuffer compositeFramebuffer;

        static bool transformCoordPos(float& x, float& y);
        static void calculateCanvas();
        static void updateCameraSizes();
        static void includeScene(size_t index, Scene* scene);
        // Copy for iteration outside sceneListMutex: callbacks invoked while iterating are
        // free to add or remove scenes without invalidating the caller's traversal.
        static std::vector<Scene*> getScenesSnapshot();
        static bool ensureCompositeFramebuffer();
        static void beginCompositeFramebuffer();
        static void endCompositeFramebuffer();
        static void runFrame(bool draw);
        
    public:
        class AsyncThreadScope {
        public:
            AsyncThreadScope() { Engine::startAsyncThread(); }
            ~AsyncThreadScope() { Engine::endAsyncThread(); }

            AsyncThreadScope(const AsyncThreadScope&) = delete;
            AsyncThreadScope& operator=(const AsyncThreadScope&) = delete;
        };

        //Engine();
        //virtual ~Engine();
        
        //-----Doriax config-----
        static void setScene(Scene* scene);
        static Scene* getScene();
        static void addSceneLayer(Scene* scene);
        static void executeSceneOnce(Scene* scene);
        static void removeScene(Scene* scene);
        static void removeAllSceneLayers(bool removeOneTimeScenes);
        static void removeAllScenes();

        static bool isSceneRunning(Scene* scene);
        static bool hasScenesToExecuteOnce();
        static Scene* getMainScene();
        static Scene* getLastScene();

        static void pauseGameEvents(bool pause);
        
        static int getCanvasWidth();
        static int getCanvasHeight();
        static void setCanvasSize(int canvasWidth, int canvasHeight);
        
        static int getPreferredCanvasWidth();
        static int getPreferredCanvasHeight();
        
        static Rect getViewRect();
        
        static void setScalingMode(Scaling scalingMode);
        static Scaling getScalingMode();

        static void setTextureStrategy(TextureStrategy textureStrategy);
        static TextureStrategy getTextureStrategy();
        
        static void setCallMouseInTouchEvent(bool callMouseInTouchEvent);
        static bool isCallMouseInTouchEvent();

        static void setCallTouchInMouseEvent(bool callTouchInMouseEvent);
        static bool isCallTouchInMouseEvent();
        
        static void setUseDegrees(bool useDegrees);
        static bool isUseDegrees();

        static void setAllowEventsOutCanvas(bool allowEventsOutCanvas);
        static bool isAllowEventsOutCanvas();

        static void setIgnoreEventsHandledByUI(bool ignoreEventsHandledByUI);
        static bool isIgnoreEventsHandledByUI();

        static bool isUIEventReceived();
        
        static void setUpdateTimeMS(unsigned int updateTimeMS);
        static void setUpdateTime(float updateTime);
        static float getUpdateTime();

        // Interpolation alpha in [0,1) representing how far between the previous and the next
        // fixed-update step the current rendered frame is. Useful for visual interpolation of
        // physics-driven entities to remove temporal aliasing.
        static double getInterpolationAlpha();

        // Fixed steps still to run in this frame, the current one included (1 outside the fixed phase).
        static int getFixedStepsLeft();

        static void setMouseCursor(CursorType type);
        static CursorType getMouseCursor();

        static void setMouseMode(MouseMode mode);
        static MouseMode getMouseMode();
        static void setMousePosition(float x, float y);
        
        static Platform getPlatform();
        static GraphicBackend getGraphicBackend();
        static bool isOpenGL();
        static float getFramerate();
        static float getDeltatime();

        static void beginFrameStats();
        static void addDrawStats(uint32_t instances, uint64_t triangles);
        static const FrameStats& getFrameStats();

        // Upper bound (in seconds) for the value returned by getDeltatime() and used by the update loop. Default 0.25.
        static float getMaxDeltatime();
        static void setMaxDeltatime(float seconds);

        // Monotonic wall-clock time in seconds, independent of scene pause / update loop.
        static double getSystemTime();

        static void setAsyncLoading(bool enable);
        static bool isAsyncLoading();

        static void startAsyncThread();
        static void commitThreadQueue();
        static void endAsyncThread();
        static bool isAsyncThread();
        static bool isViewLoaded();
        // inside systemDraw(): scenes are updating, a stack must not be torn down
        static bool isFrameRunning();

        static void setMaxResourceLoadingThreads(size_t maxThreads);
        static size_t getQueuedResourceCount();

        static void setFramebuffer(Framebuffer* framebuffer);
        static Framebuffer* getFramebuffer();
        static void clearPools();
        // Drops pool entries no longer referenced (e.g. after scenes are destroyed on project switch)
        static void clearUnusedPools();

        static void clearAllSubscriptions(bool includeLifecycle);
        // Callbacks scripts left on a scene's components and physics events. Needed before the
        // code behind them goes away, like the editor unloading the project library
        static void clearComponentSubscriptions(Scene* scene);
        static void removeSubscriptionsByTag(const std::string& substring);

        //-----Doriax API functions-----
        static void systemInit(int argc, char* argv[], System* system);
        static void systemViewLoaded();
        static void systemViewChanged();
        static void systemDraw();
        // systemDraw() without drawing, for a view that can't be seen
        static void systemUpdate();
        static void systemViewDestroyed();
        static void systemShutdown();

        static void systemPause();
        static void systemResume();

        static void systemTouchStart(int pointer, float x, float y);
        static void systemTouchEnd(int pointer, float x, float y);
        static void systemTouchMove(int pointer, float x, float y);
        static void systemTouchCancel();
        // One interrupted finger. Not a release: no pointer-up and no click.
        static void systemTouchCancel(int pointer);

        static void systemMouseDown(int button, float x, float y, int mods);
        static void systemMouseUp(int button, float x, float y, int mods);
        static void systemMouseMove(float x, float y, int mods);
        static void systemMouseScroll(float xoffset, float yoffset, int mods);
        static void systemMouseEnter();
        static void systemMouseLeave();

        static void systemKeyDown(int key, bool repeat, int mods);
        static void systemKeyUp(int key, bool repeat, int mods);

        static void systemCharInput(wchar_t codepoint);

        static void systemGamepadConnect(int gamepad, const std::string& name);
        static void systemGamepadDisconnect(int gamepad);
        static void systemGamepadButtonDown(int gamepad, int button);
        static void systemGamepadButtonUp(int gamepad, int button);
        static void systemGamepadAxisMove(int gamepad, int axis, float value);

        //-----Doriax user events-----
        // Safe accessor for the Init event
        static FunctionSubscribe<void()>& getOnInit();

        // Existing events (keep these as they are safe for runtime usage)
        static FunctionSubscribe<void()> onViewLoaded;
        static FunctionSubscribe<void()> onViewChanged;
        static FunctionSubscribe<void()> onViewDestroyed;
        static FunctionSubscribe<void()> onDraw;
        static FunctionSubscribe<void()> onUpdate;
        static FunctionSubscribe<void()> onFixedUpdate;
        static FunctionSubscribe<void()> onPostUpdate;
        static FunctionSubscribe<void()> onPause;
        static FunctionSubscribe<void()> onResume;
        static FunctionSubscribe<void()> onShutdown;
        // The stack loaded by SceneManager is ready, before its loading scene closes.
        static FunctionSubscribe<void()> onSceneLoaded;
        static FunctionSubscribe<void(int,float,float)> onTouchStart;
        static FunctionSubscribe<void(int,float,float)> onTouchEnd;
        static FunctionSubscribe<void(int,float,float)> onTouchMove;
        // One finger was interrupted. Not a pointer-up, so it does not click.
        static FunctionSubscribe<void(int,float,float)> onTouchCancel;
        static FunctionSubscribe<void(int,float,float,int)> onMouseDown;
        static FunctionSubscribe<void(int,float,float,int)> onMouseUp;
        static FunctionSubscribe<void(float,float,int)> onMouseScroll;
        static FunctionSubscribe<void(float,float,int)> onMouseMove;
        static FunctionSubscribe<void()> onMouseEnter;
        static FunctionSubscribe<void()> onMouseLeave;
        static FunctionSubscribe<void(int,bool,int)> onKeyDown;
        static FunctionSubscribe<void(int,bool,int)> onKeyUp;
        static FunctionSubscribe<void(wchar_t)> onCharInput;
        static FunctionSubscribe<void(int)> onGamepadConnect;
        static FunctionSubscribe<void(int)> onGamepadDisconnect;
        static FunctionSubscribe<void(int,int)> onGamepadButtonDown;
        static FunctionSubscribe<void(int,int)> onGamepadButtonUp;
        static FunctionSubscribe<void(int,int,float)> onGamepadAxisMove;

    };
    
}

#endif /* engine_h */
