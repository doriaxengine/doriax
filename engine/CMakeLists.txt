#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

# =============================================================
# STANDALONE PROJECT SETUP
# Only runs when this file is used as the top-level build
# (i.e. building doriax-project outside the editor).
# =============================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.15)

    if(NOT DEFINED APP_NAME)
        set(APP_NAME doriax-project)
    endif()

    project(${APP_NAME})

    set(DORIAX_SHARED OFF)

    if(NOT DORIAX_ROOT)
        set(DORIAX_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    file(TO_CMAKE_PATH ${DORIAX_ROOT} DORIAX_ROOT)

    #add_definitions(-DDISABLE_LUA_BINDINGS)
    #add_definitions(-DNO_LUA_INIT)
    #add_definitions(-DNO_CPP_INIT)

    if(NOT PROJECT_ROOT)
        set(PROJECT_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/project)
    endif()
    if(NOT EXISTS "${PROJECT_ROOT}")
        message(FATAL_ERROR "Can't find project root directory: ${PROJECT_ROOT}")
    endif()
    file(TO_CMAKE_PATH ${PROJECT_ROOT} PROJECT_ROOT)

    # Project-wide runtime settings are injected here by the editor Exporter.
    # Standalone engine builds keep the defaults declared by each platform.
    # @DORIAX_PROJECT_SETTINGS@
    if(NOT DEFINED DORIAX_VSYNC_ENABLED)
        set(DORIAX_VSYNC_ENABLED ON)
    endif()
    if(DORIAX_VSYNC_ENABLED)
        set(DORIAX_VSYNC_ENABLED_VALUE 1)
    else()
        set(DORIAX_VSYNC_ENABLED_VALUE 0)
    endif()
    add_definitions("-DDORIAX_VSYNC_ENABLED=${DORIAX_VSYNC_ENABLED_VALUE}")

    if(NOT DEFINED DORIAX_WINDOW_WIDTH)
        set(DORIAX_WINDOW_WIDTH 960)
    endif()
    if(NOT DEFINED DORIAX_WINDOW_HEIGHT)
        set(DORIAX_WINDOW_HEIGHT 540)
    endif()
    # 0 = windowed, 1 = maximized, 2 = fullscreen
    if(NOT DEFINED DORIAX_WINDOW_MODE)
        set(DORIAX_WINDOW_MODE 0)
    endif()
    if(NOT DEFINED DORIAX_WINDOW_RESIZABLE)
        set(DORIAX_WINDOW_RESIZABLE ON)
    endif()
    if(NOT DEFINED DORIAX_WINDOW_TITLE)
        set(DORIAX_WINDOW_TITLE "Doriax")
    endif()
    # ON only when the editor export generated project/app_icon.h (+ .ico/.rc)
    if(NOT DEFINED DORIAX_WINDOW_ICON)
        set(DORIAX_WINDOW_ICON OFF)
    endif()
    if(DORIAX_WINDOW_RESIZABLE)
        set(DORIAX_WINDOW_RESIZABLE_VALUE 1)
    else()
        set(DORIAX_WINDOW_RESIZABLE_VALUE 0)
    endif()
    add_definitions("-DDEFAULT_WINDOW_WIDTH=${DORIAX_WINDOW_WIDTH}")
    add_definitions("-DDEFAULT_WINDOW_HEIGHT=${DORIAX_WINDOW_HEIGHT}")
    add_definitions("-DDORIAX_WINDOW_MODE=${DORIAX_WINDOW_MODE}")
    add_definitions("-DDORIAX_WINDOW_RESIZABLE=${DORIAX_WINDOW_RESIZABLE_VALUE}")
    add_definitions("-DDORIAX_WINDOW_TITLE=\"${DORIAX_WINDOW_TITLE}\"")
    # X11 class / Wayland app_id (APP_NAME is a plain identifier, safe to quote).
    # Desktop environments match it against <APP_NAME>.desktop for the app icon.
    add_definitions("-DDORIAX_APP_ID=\"${APP_NAME}\"")
    if(DORIAX_WINDOW_ICON AND EXISTS "${PROJECT_ROOT}/app_icon.h")
        add_definitions("-DDORIAX_WINDOW_ICON")
    endif()

    # Per-project HybridArray capacities (SceneMaxValues) are injected here by the
    # editor Exporter, replacing the marker below. When building manually (without
    # the editor), the marker is left untouched and the engine defaults declared in
    # core/Engine.h apply.
    # @DORIAX_SCENE_MAX_VALUES@

    if(NOT GRAPHIC_BACKEND)
        if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
            set(GRAPHIC_BACKEND "gles3")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
            set(GRAPHIC_BACKEND "gles3")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(GRAPHIC_BACKEND "glcore")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(GRAPHIC_BACKEND "glcore")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
            set(GRAPHIC_BACKEND "glcore")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(GRAPHIC_BACKEND "metal")
            #set(GRAPHIC_BACKEND "glcore")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(GRAPHIC_BACKEND "metal")
        else()
            message(FATAL_ERROR "GRAPHIC_BACKEND is not set")
        endif()
    endif()
    message(STATUS "Graphic backend is set to ${GRAPHIC_BACKEND}")

    if(NOT APP_BACKEND)
        if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
            set(APP_BACKEND "emscripten")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
            set(APP_BACKEND "android")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(APP_BACKEND "win")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(APP_BACKEND "linux")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
            set(APP_BACKEND "linux")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(APP_BACKEND "apple")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(APP_BACKEND "apple")
        else()
            message(FATAL_ERROR "APP_BACKEND is not set")
        endif()
    endif()
    if((NOT DORIAX_VSYNC_ENABLED) AND (CMAKE_SYSTEM_NAME STREQUAL "Darwin") AND
       (APP_BACKEND STREQUAL "apple"))
        message(WARNING "VSync off is not supported by the macOS Metal application backend; this build will remain synchronized")
    endif()
    message(STATUS "Application backend is set to ${APP_BACKEND}")

    set(COMPILE_ZLIB OFF)
    set(IS_ARM OFF)

    set(PLATFORM_EXEC_FLAGS)
    set(PLATFORM_ROOT)
    set(PLATFORM_SOURCE)
    set(PLATFORM_LIBS)
    set(PLATFORM_RESOURCES)
    set(PLATFORM_PROPERTIES)
    set(PLATFORM_OPTIONS)

    find_package(Threads REQUIRED)

    # FindVulkan reads the VULKAN_SDK environment variable the SDK installer
    # sets. A macro, not a function, so Vulkan_FOUND lands in the caller.
    macro(doriax_find_vulkan backendOption)
        # sokol_gfx binds every resource through VK_EXT_descriptor_buffer, which
        # MoltenVK does not implement, so the build only fails at device selection
        if(APPLE)
            message(WARNING "${backendOption}=vulkan runs on MoltenVK here, which does not implement VK_EXT_descriptor_buffer: this build will fail at startup unless the driver provides it. ${backendOption}=metal is the working macOS backend.")
        endif()

        # An app started from the Dock inherits no shell environment, so on macOS
        # the SDK is also looked for where its installer unpacks it
        if(APPLE AND NOT DEFINED ENV{VULKAN_SDK})
            file(GLOB doriaxVulkanSdkDirs "$ENV{HOME}/VulkanSDK/*/macOS")
            if(doriaxVulkanSdkDirs)
                list(SORT doriaxVulkanSdkDirs)
                list(POP_BACK doriaxVulkanSdkDirs doriaxVulkanSdkNewest)
                set(ENV{VULKAN_SDK} "${doriaxVulkanSdkNewest}")
                message(STATUS "Using the Vulkan SDK at ${doriaxVulkanSdkNewest}")
            endif()
        endif()
        find_package(Vulkan QUIET)
        if(NOT Vulkan_FOUND)
            message(FATAL_ERROR "${backendOption}=vulkan needs the Vulkan SDK (https://vulkan.lunarg.com/sdk/home), and VULKAN_SDK is not set here. If it is installed outside the default location, restart the shell or editor this build was started from with VULKAN_SDK in its environment: Windows only gives the variable to processes started after the installer ran. Otherwise use ${backendOption}=glcore, or metal on macOS.")
        endif()
    endmacro()

    if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        add_definitions("-DDORIAX_WEB")

        if(GRAPHIC_BACKEND STREQUAL "gles3")
            add_definitions("-DSOKOL_GLES3")
            set(USE_WEBGL2 "-s USE_WEBGL2=1")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        set(CMAKE_EXECUTABLE_SUFFIX ".html")

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/emscripten)

        set(EM_PRELOAD_FILES)
        if(EXISTS "${PROJECT_ROOT}/assets")
            set(EM_PRELOAD_FILES "${EM_PRELOAD_FILES} --preload-file ${PROJECT_ROOT}/assets@/")
        endif()
        if(EXISTS "${PROJECT_ROOT}/lua")
            set(EM_PRELOAD_FILES "${EM_PRELOAD_FILES} --preload-file ${PROJECT_ROOT}/lua@/")
        endif()

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxWeb.cpp
            ${PLATFORM_ROOT}/main.cpp
        )

        list(APPEND PLATFORM_LIBS
            idbfs.js
        )

        option(EMSCRIPTEN_THREAD_SUPPORT "Enable pthreads for Emscripten builds" OFF)

        if(EMSCRIPTEN_THREAD_SUPPORT)
            set(USE_PTHREADS "-s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4")
            list(APPEND PLATFORM_OPTIONS -pthread)
        endif()

        # GROWABLE_ARRAYBUFFERS only exists on recent Emscripten, and emcc errors
        # out on unknown -s names, so probe settings.js instead of passing it
        # blindly. SDKs without the setting predate the feature entirely — their
        # behaviour already matches =0, so omitting the flag there is equivalent.
        set(ALLOW_MEMORY_GROWTH "-s ALLOW_MEMORY_GROWTH=1")
        if(EXISTS "${EMSCRIPTEN_ROOT_PATH}/src/settings.js")
            file(STRINGS "${EMSCRIPTEN_ROOT_PATH}/src/settings.js" HAS_GROWABLE_ARRAYBUFFERS
                REGEX "^var GROWABLE_ARRAYBUFFERS" LIMIT_COUNT 1)
            if(HAS_GROWABLE_ARRAYBUFFERS)
                set(ALLOW_MEMORY_GROWTH "${ALLOW_MEMORY_GROWTH} -s GROWABLE_ARRAYBUFFERS=0")
            endif()
        endif()

        list(APPEND PLATFORM_PROPERTIES
            LINK_FLAGS
                "-g \
                -s INITIAL_MEMORY=256MB \
                -s STACK_SIZE=4MB \
                -s EXPORTED_FUNCTIONS=\"['_getScreenWidth','_getScreenHeight','_changeCanvasSize','_main']\" \
                -s EXPORTED_RUNTIME_METHODS=\"['ccall', 'cwrap']\" \
                ${EM_PRELOAD_FILES} \
                ${EM_ADDITIONAL_LINK_FLAGS} \
                ${USE_PTHREADS} \
                ${ALLOW_MEMORY_GROWTH} \
                ${USE_WEBGL2}"
        )
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        add_definitions("-DDORIAX_ANDROID")

        if(GRAPHIC_BACKEND STREQUAL "gles3")
            add_definitions("-DSOKOL_GLES3")
            set(OPENGL_LIB GLESv3)
        endif()

        add_definitions("-D\"lua_getlocaledecpoint()='.'\"")
        add_definitions("-DLUA_ANSI")
        add_definitions("-DLUA_USE_C89")
        add_definitions("-DWITH_MINIAUDIO")

        set(APP_NAME doriax-android)

        if(ANDROID_ABI MATCHES "^arm(eabi)?(-v7a)?(64-v8a)?$")
            #add_compile_options("-march=armv8.2a+fp16")
            if(ANDROID_ABI MATCHES "^arm(eabi)?(-v7a)?$")
                add_compile_options("-mfpu=fp-armv8")
            endif()
            set(IS_ARM ON)
        endif()

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/android)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxAndroid.cpp
            ${PLATFORM_ROOT}/AndroidMain.cpp
            ${PLATFORM_ROOT}/NativeEngine.cpp
        )

        set(CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

        find_package(game-activity REQUIRED CONFIG)
        find_package(games-frame-pacing REQUIRED CONFIG)

        list(APPEND PLATFORM_LIBS
            ${OPENGL_LIB}
            log
            android
            EGL
            OpenSLES
            game-activity::game-activity_static
            games-frame-pacing::swappy_static
        )
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        add_definitions("-DDORIAX_WIN")

        # The native Win32 backend creates a WGL context, a Vulkan surface and
        # swapchain, or a D3D11 device and swapchain.
        if(GRAPHIC_BACKEND STREQUAL "glcore")
            add_definitions("-DSOKOL_GLCORE")
        elseif(GRAPHIC_BACKEND STREQUAL "vulkan")
            add_definitions("-DSOKOL_VULKAN")
            add_definitions("-DVK_USE_PLATFORM_WIN32_KHR")
        elseif(GRAPHIC_BACKEND STREQUAL "d3d11")
            add_definitions("-DSOKOL_D3D11")
        else()
            message(FATAL_ERROR "APP_BACKEND=win supports GRAPHIC_BACKEND glcore, vulkan or d3d11 (got '${GRAPHIC_BACKEND}')")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        if(EXISTS "${PROJECT_ROOT}/assets")
            set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/assets)
        endif()
        if(EXISTS "${PROJECT_ROOT}/lua")
            set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/lua)
        endif()

        set(PLATFORM_EXEC_FLAGS WIN32)

        if(MSVC)
            # The windowed subsystem above enters at WinMain, but the backend's
            # entry point is main() (platform/win/main.cpp). MinGW handles this
            # on its own with -mwindows.
            list(APPEND PLATFORM_PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
        endif()

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/win)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxWin.cpp
            ${PLATFORM_ROOT}/WindowWin.cpp
            ${PLATFORM_ROOT}/SystemWin.cpp
            ${PLATFORM_ROOT}/WinInputRouter.cpp
            ${PLATFORM_ROOT}/GamepadWin.cpp
            ${PLATFORM_ROOT}/main.cpp
        )

        list(APPEND PLATFORM_LIBS
            user32
            gdi32
            shell32
        )

        if(GRAPHIC_BACKEND STREQUAL "vulkan")
            # Device and swapchain are shared with the other desktops
            include_directories(${DORIAX_ROOT}/platform/common)
            list(APPEND PLATFORM_SOURCE
                ${DORIAX_ROOT}/platform/common/VulkanContext.cpp
            )
            doriax_find_vulkan("GRAPHIC_BACKEND")
            list(APPEND PLATFORM_LIBS Vulkan::Vulkan)
        elseif(GRAPHIC_BACKEND STREQUAL "d3d11")
            # d3d11 and dxgi ship with the Windows SDK, nothing to find
            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/D3D11Context.cpp
            )
            list(APPEND PLATFORM_LIBS d3d11 dxgi)
        else()
            list(APPEND PLATFORM_LIBS opengl32)
        endif()

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND PLATFORM_LIBS
                -static -static-libgcc -static-libstdc++
            )
        endif()
    endif()

    if((CMAKE_SYSTEM_NAME STREQUAL "Linux") OR (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"))
        # The native X11 backend creates a GLX context or a Vulkan surface and
        # swapchain.
        if(GRAPHIC_BACKEND STREQUAL "glcore")
            add_definitions("-DSOKOL_GLCORE")
        elseif(GRAPHIC_BACKEND STREQUAL "vulkan")
            add_definitions("-DSOKOL_VULKAN")
            add_definitions("-DVK_USE_PLATFORM_XLIB_KHR")
        else()
            message(FATAL_ERROR "APP_BACKEND=linux supports GRAPHIC_BACKEND glcore or vulkan (got '${GRAPHIC_BACKEND}')")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        if(EXISTS "${PROJECT_ROOT}/assets")
            set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/assets)
        endif()
        if(EXISTS "${PROJECT_ROOT}/lua")
            set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/lua)
        endif()

        add_definitions("-DDORIAX_LINUX")

        find_package(X11 REQUIRED)

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/linux)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxLinux.cpp
            ${PLATFORM_ROOT}/WindowLinux.cpp
            ${PLATFORM_ROOT}/SystemLinux.cpp
            ${PLATFORM_ROOT}/LinuxInputRouter.cpp
            ${PLATFORM_ROOT}/GamepadLinux.cpp
            ${PLATFORM_ROOT}/GamepadDB.cpp
            ${PLATFORM_ROOT}/main.cpp
        )

        # GamepadDB parses the community mapping database in common/
        include_directories(${DORIAX_ROOT}/platform/common)

        list(APPEND PLATFORM_LIBS
            X11::X11 dl m
        )

        if(GRAPHIC_BACKEND STREQUAL "vulkan")
            # The device and swapchain are the same on every desktop; only the
            # surface differs, so the backend passes that in.
            list(APPEND PLATFORM_SOURCE
                ${DORIAX_ROOT}/platform/common/VulkanContext.cpp
            )
            doriax_find_vulkan("GRAPHIC_BACKEND")
            list(APPEND PLATFORM_LIBS Vulkan::Vulkan)
        else()
            list(APPEND PLATFORM_LIBS GL)
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        # The native Cocoa backend creates a GL context, a Metal device, or a
        # Vulkan surface and swapchain over a CAMetalLayer.
        if(GRAPHIC_BACKEND STREQUAL "glcore")
            add_definitions("-DSOKOL_GLCORE")
        elseif(GRAPHIC_BACKEND STREQUAL "metal")
            add_definitions("-DSOKOL_METAL")
        elseif(GRAPHIC_BACKEND STREQUAL "vulkan")
            add_definitions("-DSOKOL_VULKAN")
            add_definitions("-DVK_USE_PLATFORM_METAL_EXT")
        else()
            message(FATAL_ERROR "APP_BACKEND=apple supports GRAPHIC_BACKEND glcore, metal or vulkan (got '${GRAPHIC_BACKEND}')")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")

        # Only the Xcode generator assembles the real .app that the storyboard
        # entry point in platform/apple needs, shared with iOS. Every other
        # generator builds a bare executable, on the backend in platform/mac.
        if(GRAPHIC_BACKEND STREQUAL "metal" AND CMAKE_GENERATOR STREQUAL "Xcode")
            add_definitions("-DDORIAX_APPLE")

            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/apple)

            set(APP_BUNDLE_IDENTIFIER "org.doriaxengine.doriax")
            set(PLATFORM_EXEC_FLAGS MACOSX_BUNDLE)

            include_directories(/System/Library/Frameworks)

            # The drawable and the input translation come from platform/mac
            include_directories(${DORIAX_ROOT}/platform/mac)

            list(APPEND PLATFORM_SOURCE
                ${DORIAX_ROOT}/platform/mac/MacInputRouter.mm
                ${DORIAX_ROOT}/platform/mac/MacViewMetal.mm
                ${PLATFORM_ROOT}/macos/EngineView.h
                ${PLATFORM_ROOT}/macos/EngineView.mm
                ${PLATFORM_ROOT}/macos/main.m
                ${PLATFORM_ROOT}/macos/AppDelegate.h
                ${PLATFORM_ROOT}/macos/AppDelegate.m
                ${PLATFORM_ROOT}/macos/ViewController.h
                ${PLATFORM_ROOT}/macos/ViewController.m
                ${PLATFORM_ROOT}/Renderer.h
                ${PLATFORM_ROOT}/Renderer.mm
                ${PLATFORM_ROOT}/DoriaxApple.h
                ${PLATFORM_ROOT}/DoriaxApple.mm
                ${PLATFORM_ROOT}/DoriaxGameController.h
                ${PLATFORM_ROOT}/DoriaxGameController.mm
            )

            list(APPEND PLATFORM_RESOURCES
                ${PLATFORM_ROOT}/macos/Main.storyboard
            )

            list(APPEND PLATFORM_PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${DORIAX_ROOT}/workspaces/xcode/macos/Info.plist"
                XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
            )

            list(APPEND PLATFORM_LIBS
                "-framework Cocoa"
                "-framework Metal"
                "-framework MetalKit"
                "-framework QuartzCore"
                "-framework GameController"
            )
        else()
            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/mac)

            if(EXISTS "${PROJECT_ROOT}/assets")
                set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/assets)
            endif()
            if(EXISTS "${PROJECT_ROOT}/lua")
                set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/lua)
            endif()

            # Gamepads come from the same GameController code iOS and the editor use
            include_directories(${DORIAX_ROOT}/platform/apple)

            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/DoriaxMac.h
                ${PLATFORM_ROOT}/DoriaxMac.mm
                ${PLATFORM_ROOT}/WindowMac.mm
                ${PLATFORM_ROOT}/SystemMac.mm
                ${PLATFORM_ROOT}/MacInputRouter.mm
                ${PLATFORM_ROOT}/main.mm
                ${DORIAX_ROOT}/platform/apple/DoriaxGameController.mm
            )

            list(APPEND PLATFORM_LIBS
                "-framework Cocoa"
                "-framework QuartzCore"
                "-framework GameController"
            )

            # MTKView has no GL or Vulkan drawable, so those get views of their own
            if(GRAPHIC_BACKEND STREQUAL "glcore")
                list(APPEND PLATFORM_SOURCE ${PLATFORM_ROOT}/MacViewGL.mm)
                list(APPEND PLATFORM_LIBS "-framework OpenGL")
            elseif(GRAPHIC_BACKEND STREQUAL "vulkan")
                # Device and swapchain are shared with the other desktops
                include_directories(${DORIAX_ROOT}/platform/common)
                list(APPEND PLATFORM_SOURCE
                    ${PLATFORM_ROOT}/MacViewVulkan.mm
                    ${DORIAX_ROOT}/platform/common/VulkanContext.cpp
                )
                doriax_find_vulkan("GRAPHIC_BACKEND")
                list(APPEND PLATFORM_LIBS Vulkan::Vulkan)
            else()
                list(APPEND PLATFORM_SOURCE ${PLATFORM_ROOT}/MacViewMetal.mm)
                list(APPEND PLATFORM_LIBS "-framework Metal" "-framework MetalKit")
            endif()

            list(APPEND PLATFORM_PROPERTIES
                XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
            )
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        add_definitions("-DDORIAX_APPLE")

        if(GRAPHIC_BACKEND STREQUAL "metal")
            add_definitions("-DSOKOL_METAL")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/apple)

        set(APP_BUNDLE_IDENTIFIER "org.doriaxengine.doriax")

        include_directories(/System/Library/Frameworks)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/ios/main.m
            ${PLATFORM_ROOT}/ios/AppDelegate.h
            ${PLATFORM_ROOT}/ios/AppDelegate.m
            ${PLATFORM_ROOT}/ios/EngineView.h
            ${PLATFORM_ROOT}/ios/EngineView.mm
            ${PLATFORM_ROOT}/ios/ViewController.h
            ${PLATFORM_ROOT}/ios/ViewController.m
            ${PLATFORM_ROOT}/ios/AdMobAdapter.h
            ${PLATFORM_ROOT}/ios/AdMobAdapter.m
            ${PLATFORM_ROOT}/Renderer.h
            ${PLATFORM_ROOT}/Renderer.mm
            ${PLATFORM_ROOT}/DoriaxApple.h
            ${PLATFORM_ROOT}/DoriaxApple.mm
            ${PLATFORM_ROOT}/DoriaxGameController.h
            ${PLATFORM_ROOT}/DoriaxGameController.mm
        )

        list(APPEND PLATFORM_RESOURCES
            ${PLATFORM_ROOT}/ios/LaunchScreen.storyboard
            ${PLATFORM_ROOT}/ios/Main.storyboard
        )

        list(APPEND PLATFORM_LIBS
            -ObjC

            # GameController framework for gamepad input (DoriaxGameController.mm)
            "-framework GameController"

            # To run on iOS simulator
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/FBLPromises.xcframework/ios-arm64_x86_64-simulator/FBLPromises.framework
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleAppMeasurement.xcframework/ios-arm64_x86_64-simulator/GoogleAppMeasurement.framework
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleAppMeasurementIdentitySupport.xcframework/ios-arm64_x86_64-simulator/GoogleAppMeasurementIdentitySupport.framework
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleMobileAds.xcframework/ios-arm64_x86_64-simulator/GoogleMobileAds.framework
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleUtilities.xcframework/ios-arm64_x86_64-simulator/GoogleUtilities.framework
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/UserMessagingPlatform.xcframework/ios-arm64_x86_64-simulator/UserMessagingPlatform.framework
            ${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/nanopb.xcframework/ios-arm64_x86_64-simulator/nanopb.framework

            # TODO: use here to run on real iOS devices
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/FBLPromises.xcframework/ios-arm64/FBLPromises.framework
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleAppMeasurement.xcframework/ios-arm64/GoogleAppMeasurement.framework
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleAppMeasurementIdentitySupport.xcframework/ios-arm64/GoogleAppMeasurementIdentitySupport.framework
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleMobileAds.xcframework/ios-arm64/GoogleMobileAds.framework
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/GoogleUtilities.xcframework/ios-arm64/GoogleUtilities.framework
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/UserMessagingPlatform.xcframework/ios-arm64_armv7/UserMessagingPlatform.framework
            #${PLATFORM_ROOT}/GoogleMobileAdsSdkiOS-10.14.0/nanopb.xcframework/ios-arm64/nanopb.framework
        )

        add_compile_options(-fmodules)

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0")

        list(APPEND PLATFORM_PROPERTIES
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${APP_BUNDLE_IDENTIFIER}
            MACOSX_BUNDLE_INFO_PLIST "${DORIAX_ROOT}/workspaces/xcode/ios/Info.plist"
            XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
        )
    endif()

    include_directories(${PLATFORM_ROOT})
    include_directories(${PROJECT_ROOT})
    include_directories(${PROJECT_ROOT}/scripts)

endif() # End standalone project setup

# =============================================================
# DORIAX LIBRARY BUILD
# Always runs (both standalone and when included by the editor).
# =============================================================

option(DORIAX_SHARED "Build doriax as a shared library" OFF)

if(DORIAX_SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(DORIAX_LIB_TYPE SHARED)
else()
    set(DORIAX_LIB_TYPE STATIC)
endif()

if(WIN32)
    if(MSVC)
        add_compile_options(/bigobj)
        if(DORIAX_SHARED)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        else()
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    else()
        add_compile_options(-Wa,-mbig-obj)
    endif()
endif()

include_directories(shaders)

include_directories(core)
include_directories(core/action)
include_directories(core/buffer)
include_directories(core/component)
include_directories(core/ecs)
include_directories(core/io)
include_directories(core/manager)
include_directories(core/math)
include_directories(core/object)
include_directories(core/object/sound)
include_directories(core/object/ui)
include_directories(core/object/environment)
include_directories(core/object/physics)
include_directories(core/pool)
include_directories(core/registry)
include_directories(core/render)
include_directories(core/script)
include_directories(core/shader)
include_directories(core/subsystem)
include_directories(core/texture)
include_directories(core/util)

include_directories(renders)

include_directories(SYSTEM libs)
include_directories(SYSTEM libs/luabridge3)
include_directories(SYSTEM libs/json)

include_directories(SYSTEM libs/sokol)
include_directories(SYSTEM libs/lua)
include_directories(SYSTEM libs/stb)
include_directories(SYSTEM libs/nanosvg)
include_directories(SYSTEM libs/soloud/include)
include_directories(SYSTEM libs/tinyobjloader)
include_directories(SYSTEM libs/tinygltf)
include_directories(SYSTEM libs/tinyxml2)
include_directories(SYSTEM libs/box2d/include)
include_directories(SYSTEM libs/joltphysics)
include_directories(SYSTEM libs/harfbuzz)
include_directories(SYSTEM libs/sheenbidi/Headers)

add_subdirectory(libs/sokol)
add_subdirectory(libs/lua)
add_subdirectory(libs/stb)
add_subdirectory(libs/soloud)
add_subdirectory(libs/tinyobjloader)
add_subdirectory(libs/tinygltf)
add_subdirectory(libs/tinyxml2)
add_subdirectory(libs/box2d)
add_subdirectory(libs/joltphysics)
add_subdirectory(libs/harfbuzz)
add_subdirectory(libs/sheenbidi)

# Set again here because standalone builds skip the root CMakeLists
if(MSVC)
    set(EXTERNAL_WARNING_FLAG /w)
else()
    set(EXTERNAL_WARNING_FLAG -w)
endif()

foreach(lib IN ITEMS
    sokol lua stb soloud tinyobjloader tinygltf tinyxml2
    box2d joltphysics harfbuzz sheenbidi)
    target_compile_options(${lib} PRIVATE ${EXTERNAL_WARNING_FLAG})
endforeach()

if(DEFINED NO_LUA_INIT)
    add_definitions("-DNO_LUA_INIT=${NO_LUA_INIT}")
    unset(NO_LUA_INIT CACHE)
endif()
if(DEFINED NO_CPP_INIT)
    add_definitions("-DNO_CPP_INIT=${NO_CPP_INIT}")
    unset(NO_CPP_INIT CACHE)
endif()

#add_definitions("-DPROJECT_ROOT=\"${PROJECT_ROOT}\"")

set(DORIAX_SRCS
    core/Engine.cpp
    core/Input.cpp
    core/Log.cpp
    core/Scene.cpp
    core/System.cpp
    core/action/Action.cpp
    core/action/AlphaAction.cpp
    core/action/Animation.cpp
    core/action/ColorAction.cpp
    core/action/Particles.cpp
    core/action/PositionAction.cpp
    core/action/RotationAction.cpp
    core/action/ScaleAction.cpp
    core/action/SpriteAnimation.cpp
    core/action/TimedAction.cpp
    core/action/keyframe/MorphTracks.cpp
    core/action/keyframe/RotateTracks.cpp
    core/action/keyframe/ScaleTracks.cpp
    core/action/keyframe/TranslateTracks.cpp
    core/buffer/Attribute.cpp
    core/buffer/Buffer.cpp
    core/buffer/ExternalBuffer.cpp
    core/buffer/IndexBuffer.cpp
    core/buffer/InterleavedBuffer.cpp
    core/ecs/ComponentManager.cpp
    core/io/Data.cpp
    core/io/File.cpp
    core/io/FileData.cpp
    core/io/ResourcePack.cpp
    core/manager/BundleManager.cpp
    core/manager/SceneManager.cpp
    core/io/UserSettings.cpp
    core/math/AABB.cpp
    core/math/Matrix3.cpp
    core/math/Matrix4.cpp
    core/math/OBB.cpp
    core/math/Plane.cpp
    core/math/Quaternion.cpp
    core/math/Ray.cpp
    core/math/Rect.cpp
    core/math/Sphere.cpp
    core/math/Vector2.cpp
    core/math/Vector3.cpp
    core/math/Vector4.cpp
    core/object/Bone.cpp
    core/object/Camera.cpp
    core/object/EntityHandle.cpp
    core/object/Light.cpp
    core/object/Light2D.cpp
    core/object/Lines.cpp
    core/object/Mesh.cpp
    core/object/MeshPolygon.cpp
    core/object/Mirror.cpp
    core/object/Model.cpp
    core/object/Object.cpp
    core/object/Occluder2D.cpp
    core/object/Points.cpp
    core/object/Shape.cpp
    core/object/Sprite.cpp
    core/object/Terrain.cpp
    core/object/Tilemap.cpp
    core/object/sound/Sound.cpp
    core/object/environment/Fog.cpp
    core/object/environment/ReflectionProbe.cpp
    core/object/environment/SkyBox.cpp
    core/object/physics/Body2D.cpp
    core/object/physics/Body3D.cpp
    core/object/physics/CollideShapeResult3D.cpp
    core/object/physics/Contact2D.cpp
    core/object/physics/Contact3D.cpp
    core/object/physics/Joint2D.cpp
    core/object/physics/Joint3D.cpp
    core/object/physics/Manifold2D.cpp
    core/object/ui/Button.cpp
    core/object/ui/Container.cpp
    core/object/ui/Image.cpp
    core/object/ui/Panel.cpp
    core/object/ui/Polygon.cpp
    core/object/ui/Scrollbar.cpp
    core/object/ui/Progressbar.cpp
    core/object/ui/Text.cpp
    core/object/ui/TextEdit.cpp
    core/object/ui/UILayout.cpp
    core/pool/FontPool.cpp
    core/pool/ShaderPool.cpp
    core/pool/SoundPool.cpp
    core/pool/TextureDataPool.cpp
    core/pool/TexturePool.cpp
    core/pool/ModelPool.cpp
    core/registry/EntityRegistry.cpp
    core/render/BufferRender.cpp
    core/render/CameraRender.cpp
    core/render/FramebufferRender.cpp
    core/render/ObjectRender.cpp
    core/render/ShaderRender.cpp
    core/render/SystemRender.cpp
    core/render/TextureRender.cpp
    core/script/LuaBinding.cpp
    core/script/LuaFunctionBase.cpp
    core/script/LuaScript.cpp
    core/script/ScriptBase.cpp
    core/script/ScriptProperty.cpp
    core/script/binding/ActionClassesLua.cpp
    core/script/binding/CoreClassesLua.cpp
    core/script/binding/ECSClassesLua.cpp
    core/script/binding/IOClassesLua.cpp
    core/script/binding/MathClassesLua.cpp
    core/script/binding/ObjectClassesLua.cpp
    core/script/binding/ThreadClassesLua.cpp
    core/script/binding/UtilClassesLua.cpp
    core/shader/ShaderData.cpp
    core/shader/ShaderDataSerializer.cpp
    core/subsystem/ActionSystem.cpp
    core/subsystem/AudioSystem.cpp
    core/subsystem/MeshSystem.cpp
    core/subsystem/PhysicsSystem.cpp
    core/subsystem/RenderSystem.cpp
    core/subsystem/UISystem.cpp
    core/texture/Framebuffer.cpp
    core/texture/IBLEnvironment.cpp
    core/texture/Texture.cpp
    core/texture/TextureData.cpp
    core/thread/ResourceProgress.cpp
    core/thread/ThreadPoolManager.cpp
    core/util/Angle.cpp
    core/util/Base64.cpp
    core/util/Color.cpp
    core/util/STBText.cpp
    core/util/StringUtils.cpp
    core/util/UniqueToken.cpp
    core/util/XMLUtils.cpp
)

set(RENDER_SOKOL_SRC
    renders/sokol/SokolBuffer.cpp
    renders/sokol/SokolTexture.cpp
    renders/sokol/SokolSystem.cpp
    renders/sokol/SokolCamera.cpp
    renders/sokol/SokolObject.cpp
    renders/sokol/SokolShader.cpp
    renders/sokol/SokolCmdQueue.cpp
    renders/sokol/SokolFramebuffer.cpp
)

add_library(
    doriax

    ${DORIAX_LIB_TYPE}

    ${RENDER_SOKOL_SRC}
    ${DORIAX_SRCS}
)

find_package(Threads REQUIRED)
target_link_libraries(doriax PRIVATE Threads::Threads)

if(EMSCRIPTEN AND EMSCRIPTEN_THREAD_SUPPORT)
	target_compile_options(doriax PRIVATE -pthread)
endif()

target_link_libraries(
    doriax

    PUBLIC

    sokol lua stb tinyobjloader tinygltf tinyxml2 soloud box2d joltphysics harfbuzz sheenbidi
)

set_target_properties(
    doriax

    PROPERTIES

    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(DORIAX_SHARED)
    target_compile_definitions(doriax 
        PRIVATE 
        DORIAX_EXPORTS
        PUBLIC
        DORIAX_SHARED
    )
endif()

# =============================================================
# STANDALONE EXECUTABLE BUILD
# Only runs when this file is used as the top-level build.
# =============================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)

    file(GLOB_RECURSE PROJECT_SOURCE ${PROJECT_ROOT}/*.cpp)

    # Embed the exported icon into the Windows executable (Explorer, taskbar
    # and window icon). Other platforms set the window icon at runtime instead.
    if(WIN32 AND DORIAX_WINDOW_ICON AND EXISTS "${PROJECT_ROOT}/app_icon.rc")
        list(APPEND PLATFORM_SOURCE ${PROJECT_ROOT}/app_icon.rc)
    endif()

    set(all_code_files
        ${PROJECT_SOURCE}
        ${PLATFORM_SOURCE}
        ${PLATFORM_RESOURCES}
    )

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        add_executable(
            ${APP_NAME}
            ${PLATFORM_EXEC_FLAGS}
            ${all_code_files}
        )
    else()
        add_library(
            ${APP_NAME}
            SHARED
            ${all_code_files}
        )
    endif()

    if(WIN32)
        include(${DORIAX_ROOT}/platform/win/WindowsManifest.cmake)
        doriax_add_windows_manifest(${APP_NAME})
    endif()

    # Project script roots are injected here by the editor Exporter. They are
    # scoped to this target: include_directories() would reach the doriax
    # library too, ahead of core/, letting a project header shadow an engine one.
    # @DORIAX_SCRIPT_DIRS@

    if(DEFINED ASSETS_DEST_DIR OR DEFINED LUA_DEST_DIR)
        install(TARGETS ${APP_NAME} RUNTIME DESTINATION bin)
    endif()

    if(DEFINED ASSETS_DEST_DIR)
        add_custom_command(
            TARGET ${APP_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E remove_directory ${ASSETS_DEST_DIR}
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_ROOT}/assets ${ASSETS_DEST_DIR}
        )
        install(DIRECTORY ${ASSETS_DEST_DIR} DESTINATION bin)
    endif()

    if(DEFINED LUA_DEST_DIR)
        add_custom_command(
            TARGET ${APP_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E remove_directory ${LUA_DEST_DIR}
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_ROOT}/lua ${LUA_DEST_DIR}
        )
        install(DIRECTORY ${LUA_DEST_DIR} DESTINATION bin)
    endif()

    set_target_properties(
        ${APP_NAME}

        PROPERTIES
        ${PLATFORM_PROPERTIES}
        RESOURCE "${PLATFORM_RESOURCES}"
        CXX_STANDARD 17
    )

    # Generated sources embed UTF-8 literals and carry no BOM, so MSVC would
    # otherwise decode them in the active code page.
    target_compile_options(
        ${APP_NAME}

        PUBLIC

        ${PLATFORM_OPTIONS}
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    )

    target_link_libraries(
        ${APP_NAME}

        PRIVATE

        doriax
        Threads::Threads
        ${PLATFORM_LIBS}
    )

    # Optional user build file; the editor writes the same hook, with the same
    # variable names, into the project CMakeLists.txt it generates.
    set(DORIAX_TARGET ${APP_NAME})
    set(DORIAX_SCRIPTS_DIR ${PROJECT_ROOT}/scripts)
    include(${PROJECT_ROOT}/ProjectBuild.cmake OPTIONAL)

endif() # End standalone executable build
