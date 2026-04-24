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

    add_definitions("-DDEFAULT_WINDOW_WIDTH=960")
    add_definitions("-DDEFAULT_WINDOW_HEIGHT=540")

    if(NOT GRAPHIC_BACKEND)
        if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
            set(GRAPHIC_BACKEND "gles3")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
            set(GRAPHIC_BACKEND "gles3")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(GRAPHIC_BACKEND "d3d11")
            #set(GRAPHIC_BACKEND "glcore")
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
            set(APP_BACKEND "sokol")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            #set(APP_BACKEND "sokol")
            set(APP_BACKEND "glfw")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
            #set(APP_BACKEND "sokol")
            set(APP_BACKEND "glfw")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            if(CMAKE_GENERATOR STREQUAL "Xcode")
                set(APP_BACKEND "apple")
            else()
                set(APP_BACKEND "sokol")
                #set(APP_BACKEND "glfw")
            endif()
        elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
            set(APP_BACKEND "apple")
        else()
            message(FATAL_ERROR "APP_BACKEND is not set")
        endif()
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

        set(ALLOW_MEMORY_GROWTH "-s ALLOW_MEMORY_GROWTH=1")

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
        add_definitions("-DDORIAX_SOKOL")

        if(GRAPHIC_BACKEND STREQUAL "glcore")
            add_definitions("-DSOKOL_GLCORE")
        elseif(GRAPHIC_BACKEND STREQUAL "d3d11")
            add_definitions("-DSOKOL_D3D11")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        if(EXISTS "${PROJECT_ROOT}/assets")
            set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/assets)
        endif()
        if(EXISTS "${PROJECT_ROOT}/lua")
            set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/$<CONFIG>/lua)
        endif()

        set(PLATFORM_EXEC_FLAGS WIN32)

        set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/sokol)

        list(APPEND PLATFORM_SOURCE
            ${PLATFORM_ROOT}/DoriaxSokol.cpp
            ${PLATFORM_ROOT}/main.cpp
        )

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            list(APPEND PLATFORM_LIBS
                -static -static-libgcc -static-libstdc++
            )
        endif()
    endif()

    if((CMAKE_SYSTEM_NAME STREQUAL "Linux") OR (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD"))
        if(GRAPHIC_BACKEND STREQUAL "glcore")
            add_definitions("-DSOKOL_GLCORE")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        if(EXISTS "${PROJECT_ROOT}/assets")
            set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/assets)
        endif()
        if(EXISTS "${PROJECT_ROOT}/lua")
            set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/lua)
        endif()

        if(APP_BACKEND STREQUAL "glfw")
            add_definitions("-DDORIAX_GLFW")

            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/glfw)

            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/DoriaxGLFW.cpp
                ${PLATFORM_ROOT}/main.cpp
            )

            list(APPEND PLATFORM_LIBS
                GL dl m glfw
            )
        else()
            add_definitions("-DDORIAX_SOKOL")

            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/sokol)

            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/DoriaxSokol.cpp
                ${PLATFORM_ROOT}/main.cpp
            )
        endif()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        if(GRAPHIC_BACKEND STREQUAL "glcore")
            add_definitions("-DSOKOL_GLCORE")
        elseif(GRAPHIC_BACKEND STREQUAL "metal")
            add_definitions("-DSOKOL_METAL")
        endif()

        add_definitions("-DWITH_MINIAUDIO")

        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fobjc-arc")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fobjc-arc")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")

        if(APP_BACKEND STREQUAL "apple")
            add_definitions("-DDORIAX_APPLE")

            set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/apple)

            set(APP_BUNDLE_IDENTIFIER "org.doriaxengine.doriax")

            set(PLATFORM_EXEC_FLAGS MACOSX_BUNDLE)

            include_directories(/System/Library/Frameworks)

            list(APPEND PLATFORM_SOURCE
                ${PLATFORM_ROOT}/macos/main.m
                ${PLATFORM_ROOT}/macos/AppDelegate.h
                ${PLATFORM_ROOT}/macos/AppDelegate.m
                ${PLATFORM_ROOT}/macos/EngineView.h
                ${PLATFORM_ROOT}/macos/EngineView.mm
                ${PLATFORM_ROOT}/macos/ViewController.h
                ${PLATFORM_ROOT}/macos/ViewController.m
                ${PLATFORM_ROOT}/Renderer.h
                ${PLATFORM_ROOT}/Renderer.mm
                ${PLATFORM_ROOT}/DoriaxApple.h
                ${PLATFORM_ROOT}/DoriaxApple.mm
            )

            list(APPEND PLATFORM_RESOURCES
                ${PLATFORM_ROOT}/macos/Main.storyboard
            )

            list(APPEND PLATFORM_PROPERTIES
                MACOSX_BUNDLE_INFO_PLIST "${DORIAX_ROOT}/workspaces/xcode/macos/Info.plist"
                XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC "YES"
            )
        else()
            if(EXISTS "${PROJECT_ROOT}/assets")
                set(ASSETS_DEST_DIR ${CMAKE_BINARY_DIR}/assets)
            endif()
            if(EXISTS "${PROJECT_ROOT}/lua")
                set(LUA_DEST_DIR ${CMAKE_BINARY_DIR}/lua)
            endif()

            if(APP_BACKEND STREQUAL "glfw")
                add_definitions("-DDORIAX_GLFW")

                set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/glfw)

                list(APPEND PLATFORM_SOURCE
                    ${PLATFORM_ROOT}/DoriaxGLFW.cpp
                    ${PLATFORM_ROOT}/main.cpp
                )

                list(APPEND PLATFORM_LIBS
                    glfw
                )
            else()
                add_definitions("-DDORIAX_SOKOL")

                set(PLATFORM_ROOT ${DORIAX_ROOT}/platform/sokol)

                list(APPEND PLATFORM_SOURCE
                    ${PLATFORM_ROOT}/DoriaxSokol.cpp
                    ${PLATFORM_ROOT}/main.cpp
                )
            endif()
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
        )

        list(APPEND PLATFORM_RESOURCES
            ${PLATFORM_ROOT}/ios/LaunchScreen.storyboard
            ${PLATFORM_ROOT}/ios/Main.storyboard
        )

        list(APPEND PLATFORM_LIBS
            -ObjC

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
include_directories(core/object/audio)
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

include_directories(libs)
include_directories(libs/luabridge3)
include_directories(libs/json)

include_directories(libs/sokol)
include_directories(libs/lua)
include_directories(libs/stb)
include_directories(libs/soloud/include)
include_directories(libs/tinyobjloader)
include_directories(libs/tinygltf)
include_directories(libs/tinyxml2)
include_directories(libs/box2d/include)
include_directories(libs/joltphysics)

add_subdirectory(libs/sokol)
add_subdirectory(libs/lua)
add_subdirectory(libs/stb)
add_subdirectory(libs/soloud)
add_subdirectory(libs/tinyobjloader)
add_subdirectory(libs/tinygltf)
add_subdirectory(libs/tinyxml2)
add_subdirectory(libs/box2d)
add_subdirectory(libs/joltphysics)

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
    core/io/Data.cpp
    core/io/File.cpp
    core/io/FileData.cpp
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
    core/object/Lines.cpp
    core/object/Mesh.cpp
    core/object/MeshPolygon.cpp
    core/object/Model.cpp
    core/object/Object.cpp
    core/object/Points.cpp
    core/object/Shape.cpp
    core/object/Sprite.cpp
    core/object/Terrain.cpp
    core/object/Tilemap.cpp
    core/object/audio/Audio.cpp
    core/object/environment/Fog.cpp
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
    core/object/ui/Text.cpp
    core/object/ui/TextEdit.cpp
    core/object/ui/UILayout.cpp
    core/pool/FontPool.cpp
    core/pool/ShaderPool.cpp
    core/pool/TextureDataPool.cpp
    core/pool/TexturePool.cpp
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
    core/shader/SBSReader.cpp
    core/shader/ShaderData.cpp
    core/shader/ShaderDataSerializer.cpp
    core/subsystem/ActionSystem.cpp
    core/subsystem/AudioSystem.cpp
    core/subsystem/MeshSystem.cpp
    core/subsystem/PhysicsSystem.cpp
    core/subsystem/RenderSystem.cpp
    core/subsystem/UISystem.cpp
    core/texture/Framebuffer.cpp
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

    sokol lua stb tinyobjloader tinygltf tinyxml2 soloud box2d joltphysics
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

    target_compile_options(
        ${APP_NAME}

        PUBLIC

        ${PLATFORM_OPTIONS}
    )

    target_link_libraries(
        ${APP_NAME}

        doriax
        Threads::Threads
        ${PLATFORM_LIBS}
    )

endif() # End standalone executable build