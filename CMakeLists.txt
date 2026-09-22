cmake_minimum_required(VERSION 3.20)
project(doriax-editor)

# Same as CI (.github/workflows/cmake.yml): git describe --tags --always --dirty.
# CI passes DORIAXEDITOR_VERSION explicitly; local builds ask git here.
if(NOT DORIAXEDITOR_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            OUTPUT_VARIABLE DORIAXEDITOR_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()
endif()

# Git allows characters that would break the generated C++ and .rc string literals
string(REGEX REPLACE "[^A-Za-z0-9._+-]" "" DORIAXEDITOR_VERSION "${DORIAXEDITOR_VERSION}")

if(NOT DORIAXEDITOR_VERSION)
    set(DORIAXEDITOR_VERSION "unknown")
endif()

# Windows VERSIONINFO wants four integers, which only a vX.Y[.Z] tag can give.
set(DORIAX_VERSION_NUMERIC "0,0,0,0")
if(DORIAXEDITOR_VERSION MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    set(DORIAX_VERSION_NUMERIC "${CMAKE_MATCH_1},${CMAKE_MATCH_2},${CMAKE_MATCH_3},0")
elseif(DORIAXEDITOR_VERSION MATCHES "^v?([0-9]+)\\.([0-9]+)$")
    set(DORIAX_VERSION_NUMERIC "${CMAKE_MATCH_1},${CMAKE_MATCH_2},0,0")
endif()

message(STATUS "Doriax version is ${DORIAXEDITOR_VERSION}")

if(APPLE)
    enable_language(OBJCXX)
endif()

set(DORIAX_SHARED ON)

set(ENABLE_OPT OFF CACHE BOOL "Enables spirv-opt capability if present" FORCE)

# Each desktop OS is served by its own native backend and nothing else, so this
# is a detection rather than a choice.
if(NOT API_BACKEND)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(API_BACKEND "linux")
    elseif(WIN32)
        set(API_BACKEND "win")
    elseif(APPLE)
        set(API_BACKEND "mac")
    else()
        message(FATAL_ERROR "No native editor backend for ${CMAKE_SYSTEM_NAME} (expected Linux, Windows, or macOS)")
    endif()
endif()
message(STATUS "API backend is set to ${API_BACKEND}")

# The native Linux and Windows backends render through Vulkan or OpenGL; macOS
# is tied to Metal.
if(API_BACKEND STREQUAL "linux" OR API_BACKEND STREQUAL "win")
    # Cached so a reconfigure keeps the backend the build directory was made with
    set(GRAPHIC_BACKEND "glcore" CACHE STRING "Editor graphic backend: vulkan or glcore")
    if(NOT GRAPHIC_BACKEND STREQUAL "vulkan" AND NOT GRAPHIC_BACKEND STREQUAL "glcore")
        message(FATAL_ERROR "API_BACKEND=${API_BACKEND} supports GRAPHIC_BACKEND vulkan or glcore")
    endif()
else()
    set(GRAPHIC_BACKEND "metal")
endif()
message(STATUS "Graphic backend is set to ${GRAPHIC_BACKEND}")

# Only macOS keeps the menu bar outside the window, so it is the one platform
# where a native menu is worth its own implementation. ImGui draws it elsewhere.
if(NOT DEFINED NATIVE_MENU)
    if(API_BACKEND STREQUAL "mac")
        set(NATIVE_MENU ON)
    else()
        set(NATIVE_MENU OFF)
    endif()
endif()
message(STATUS "Native menu is set to ${NATIVE_MENU}")

# GLVND links libOpenGL.so.0, which lives in libopengl0 and is absent from a stock
# desktop. libGL.so.1 ships with any driver, and is what the engine already links.
set(OpenGL_GL_PREFERENCE LEGACY)

if(WIN32)
    enable_language(RC)

    set(PLATFORM_EXEC_FLAGS WIN32)
 
    if(MSVC)
        set(PLATFORM_PROPERTIES_FLAGS LINK_FLAGS "/ENTRY:mainCRTStartup")
        if(DORIAX_SHARED)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        else()
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    elseif(MINGW)
        # Same call as the CRT above: a shared engine means one GCC runtime for
        # the editor, the DLL and the plugins, shipped beside the binaries.
        get_filename_component(DORIAX_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_file(DORIAX_MINGW_STDCXX libstdc++-6.dll PATHS ${DORIAX_MINGW_BIN} NO_DEFAULT_PATH)
        find_file(DORIAX_MINGW_PTHREAD libwinpthread-1.dll PATHS ${DORIAX_MINGW_BIN} NO_DEFAULT_PATH)
        # The unwind DLL is named after the toolchain's exception model
        find_file(DORIAX_MINGW_GCC NAMES libgcc_s_seh-1.dll libgcc_s_dw2-1.dll libgcc_s_sjlj-1.dll
                  PATHS ${DORIAX_MINGW_BIN} NO_DEFAULT_PATH)

        if(DORIAX_MINGW_STDCXX AND DORIAX_MINGW_PTHREAD AND DORIAX_MINGW_GCC)
            set(DORIAX_MINGW_RUNTIME ${DORIAX_MINGW_STDCXX} ${DORIAX_MINGW_PTHREAD} ${DORIAX_MINGW_GCC})
        else()
            message(WARNING "Incomplete GCC runtime next to ${CMAKE_CXX_COMPILER}: the editor will only start from a shell that has it on PATH.")
        endif()
    endif()
endif()

if(APPLE)
    set(PLATFORM_EXEC_FLAGS MACOSX_BUNDLE)
endif()

set(IMGUI_DIR libs/imgui)
set(EDITOR_DIR editor)
set(DORIAX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/engine)

# Warnings from vendored code are not ours to fix and only hide our own
if(MSVC)
    set(EXTERNAL_WARNING_FLAG /w)
else()
    set(EXTERNAL_WARNING_FLAG -w)
endif()

# Icon and version resource for one binary. Each target needs its own so
# OriginalFilename matches the file on disk. SignPath wants metadata on every
# signed binary, so the DLL gets one too: pass an empty icon_name for it.
function(doriax_add_windows_resource out_var target_name icon_name original_filename description)
    set(DORIAX_ORIGINAL_FILENAME ${original_filename})
    set(DORIAX_FILE_DESCRIPTION ${description})
    set(DORIAX_INTERNAL_NAME ${target_name})

    if(icon_name)
        set(DORIAX_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/icons/${icon_name}")
        set(DORIAX_ICON_RESOURCE "GLFW_ICON ICON \"${DORIAX_ICON_FILE}\"")
    else()
        set(DORIAX_ICON_RESOURCE "")
    endif()

    if(original_filename MATCHES "\\.dll$")
        set(DORIAX_FILE_TYPE "0x2L     // VFT_DLL")
    else()
        set(DORIAX_FILE_TYPE "0x1L     // VFT_APP")
    endif()

    set(generated_rc "${CMAKE_CURRENT_BINARY_DIR}/resources/${target_name}.rc")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/icons/doriax-editor.rc.in"
        ${generated_rc}
        @ONLY
    )
    if(icon_name)
        set_property(SOURCE ${generated_rc} APPEND PROPERTY OBJECT_DEPENDS ${DORIAX_ICON_FILE})
    endif()

    add_library(${target_name}-resource OBJECT ${generated_rc})
    # rc.exe chokes on the editor's include list, and the .rc needs no search path
    set_property(TARGET ${target_name}-resource PROPERTY INCLUDE_DIRECTORIES "")

    set(${out_var} $<TARGET_OBJECTS:${target_name}-resource> PARENT_SCOPE)
endfunction()

if(WIN32)
    include(${DORIAX_ROOT}/platform/win/WindowsManifest.cmake)
endif()

set(DORIAX_EDITOR_ICON_OBJECTS)
if(WIN32)
    doriax_add_windows_resource(
        DORIAX_EDITOR_ICON_OBJECTS
        doriax-editor
        "doriax-editor.ico"
        "doriax-editor.exe"
        "Doriax Engine Editor"
    )
endif()

# Linux has no .rc or .icns equivalent, so the icon reaches the desktop as theme
# files (installed at the end of this file) plus the _NET_WM_ICON pixels below.
set(DORIAX_LINUX_ICON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/icons/linux")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(wm_icon_header "// Generated from editor/resources/icons/linux. Do not edit.\n")
    string(APPEND wm_icon_header "#pragma once\n\n")
    set(wm_icon_entries "")

    # Sizes a window manager or dock actually draws, up to a HiDPI dock's 256.
    # 512 is left out: on the wire it alone would outweigh the whole set.
    foreach(icon_size IN ITEMS 16 32 48 64 128 256)
        set(icon_png "${DORIAX_LINUX_ICON_DIR}/doriax-editor-${icon_size}.png")
        file(READ "${icon_png}" icon_hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," icon_bytes "${icon_hex}")
        string(APPEND wm_icon_header "static const unsigned char doriax_wm_icon_${icon_size}[] = {${icon_bytes}};\n\n")
        string(APPEND wm_icon_entries "    { doriax_wm_icon_${icon_size}, sizeof(doriax_wm_icon_${icon_size}) },\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${icon_png}")
    endforeach()

    string(APPEND wm_icon_header "struct DoriaxWmIconPng { const unsigned char* data; unsigned int size; };\n\n")
    string(APPEND wm_icon_header "static const DoriaxWmIconPng doriax_wm_icon_pngs[] = {\n${wm_icon_entries}};\n")

    # Staged through a .in because configure_file only rewrites on a real change.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/resources/doriax_wm_icon.h.in" "${wm_icon_header}")
    configure_file(
        "${CMAKE_CURRENT_BINARY_DIR}/resources/doriax_wm_icon.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resources/doriax_wm_icon.h"
        COPYONLY
    )
    include_directories("${CMAKE_CURRENT_BINARY_DIR}/resources")
endif()

find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# The AI HTTP client uses the native WinHTTP backend on Windows (no external
# dependency) and libcurl everywhere else (see editor/ai/HttpClient.cpp).
if(NOT WIN32)
    find_package(CURL REQUIRED)
endif()

if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
endif()

set(BACKEND_SOURCES)
set(BACKEND_LIBS)

if(API_BACKEND STREQUAL "linux")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "API_BACKEND=linux is only available on Linux")
    endif()
    find_package(X11 REQUIRED COMPONENTS Xrandr)

    list(APPEND BACKEND_SOURCES
        ${EDITOR_DIR}/backend/BackendLinux.cpp
        ${DORIAX_ROOT}/platform/linux/WindowLinux.cpp
        ${DORIAX_ROOT}/platform/linux/GamepadDB.cpp
    )
    list(APPEND BACKEND_LIBS X11::X11 X11::Xrandr)

    if(GRAPHIC_BACKEND STREQUAL "vulkan")
        find_package(Vulkan REQUIRED)
        list(APPEND BACKEND_LIBS Vulkan::Vulkan)
        add_definitions("-DVK_USE_PLATFORM_XLIB_KHR")
    else()
        find_package(OpenGL REQUIRED)
    endif()
elseif(API_BACKEND STREQUAL "win")
    if(NOT WIN32)
        message(FATAL_ERROR "API_BACKEND=win is only available on Windows")
    endif()
    list(APPEND BACKEND_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${EDITOR_DIR}/backend/BackendWin.cpp
        ${DORIAX_ROOT}/platform/win/WindowWin.cpp
        ${DORIAX_ROOT}/platform/win/GamepadWin.cpp
    )

    list(APPEND BACKEND_LIBS
        user32
        gdi32
        dwmapi
        shell32
    )

    if(GRAPHIC_BACKEND STREQUAL "vulkan")
        find_package(Vulkan REQUIRED)
        list(APPEND BACKEND_LIBS Vulkan::Vulkan)
        add_definitions("-DVK_USE_PLATFORM_WIN32_KHR")
    else()
        list(APPEND BACKEND_LIBS opengl32)
    endif()
elseif(API_BACKEND STREQUAL "mac")
    if(NOT APPLE)
        message(FATAL_ERROR "API_BACKEND=mac is only available on macOS")
    endif()

    list(APPEND BACKEND_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_osx.mm
        ${EDITOR_DIR}/backend/BackendMac.mm
        ${DORIAX_ROOT}/platform/mac/WindowMac.mm
        ${DORIAX_ROOT}/platform/apple/DoriaxGameController.mm
    )

    list(APPEND BACKEND_LIBS
        "-framework AppKit"
        "-framework CoreGraphics"
        "-framework GameController"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
    )

    set_source_files_properties(
        ${IMGUI_DIR}/backends/imgui_impl_osx.mm
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
        ${EDITOR_DIR}/backend/BackendMac.mm
        ${DORIAX_ROOT}/platform/mac/WindowMac.mm
        ${EDITOR_DIR}/util/GraphicUtils.cpp
        ${DORIAX_ROOT}/platform/apple/DoriaxGameController.mm
        PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    set_source_files_properties(
        ${EDITOR_DIR}/util/GraphicUtils.cpp
        PROPERTIES LANGUAGE OBJCXX
    )
else()
    message(FATAL_ERROR "Unknown API_BACKEND '${API_BACKEND}' (expected linux, win, or mac)")
endif()

# The native backends share the frame loop and pick a renderer by GRAPHIC_BACKEND
if(API_BACKEND STREQUAL "linux" OR API_BACKEND STREQUAL "win")
    list(APPEND BACKEND_SOURCES ${EDITOR_DIR}/EditorFrame.cpp)
    if(GRAPHIC_BACKEND STREQUAL "vulkan")
        list(APPEND BACKEND_SOURCES ${EDITOR_DIR}/backend/renderer/RendererVulkan.cpp)
    else()
        list(APPEND BACKEND_SOURCES ${EDITOR_DIR}/backend/renderer/RendererGL.cpp)
    endif()
endif()

set(SHADERCOMPILER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/shadercompiler)

add_definitions("-DDORIAX_EDITOR")
add_definitions("-DDORIAX_CRASH_GUARD")

add_definitions("-DNO_LUA_INIT")
#add_definitions("-DNO_CPP_INIT")
if(GRAPHIC_BACKEND STREQUAL "vulkan")
    add_definitions("-DSOKOL_VULKAN")
elseif(GRAPHIC_BACKEND STREQUAL "metal")
    add_definitions("-DSOKOL_METAL")
else()
    add_definitions("-DSOKOL_GLCORE")
endif()
if(NATIVE_MENU)
    add_definitions("-DDORIAX_NATIVE_MENU")
endif()
add_definitions("-DWITH_MINIAUDIO") # For SoLoud
set(NFD_PORTAL ON CACHE BOOL "Use xdg-desktop-portal instead of GTK") # For NFD
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Enable parse tools" FORCE) # For yaml-cpp

set(SHADERS_DIR "${CMAKE_SOURCE_DIR}/engine/tools/shaderlib")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(SHADERS_HEADER "${GENERATED_DIR}/shaders.h")
set(GENERATE_SHADERS_SCRIPT "${CMAKE_SOURCE_DIR}/generate_shaders.cmake")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/EditorVersion.h.in"
    "${GENERATED_DIR}/EditorVersion.h"
    @ONLY
)

file(GLOB_RECURSE SHADER_FILES CONFIGURE_DEPENDS "${SHADERS_DIR}/*.glsl" "${SHADERS_DIR}/*.frag" "${SHADERS_DIR}/*.vert")

add_custom_command(
    OUTPUT ${SHADERS_HEADER}
    COMMAND ${CMAKE_COMMAND}
            -DSHADERS_DIR=${SHADERS_DIR}
            -DSHADER_FILES="${SHADER_FILES}"
            -DSHADERS_HEADER=${SHADERS_HEADER}
            -P ${GENERATE_SHADERS_SCRIPT}
    DEPENDS ${GENERATE_SHADERS_SCRIPT} ${SHADER_FILES}
    COMMENT "Generating shaders.h"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Generate engine API suggestions header from Lua bindings and C++ headers
set(BINDING_DIR "${CMAKE_SOURCE_DIR}/engine/core/script/binding")
set(ENGINE_CORE_DIR "${CMAKE_SOURCE_DIR}/engine/core")
set(API_SUGGESTIONS_HEADER "${GENERATED_DIR}/engine_api_suggestions.h")
set(GENERATE_API_SCRIPT "${CMAKE_SOURCE_DIR}/generate_api_suggestions.py")

file(GLOB BINDING_FILES CONFIGURE_DEPENDS "${BINDING_DIR}/*.cpp")
file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS "${ENGINE_CORE_DIR}/*.h")

add_custom_command(
    OUTPUT ${API_SUGGESTIONS_HEADER}
    COMMAND ${Python3_EXECUTABLE} ${GENERATE_API_SCRIPT}
            ${BINDING_DIR} ${API_SUGGESTIONS_HEADER} ${ENGINE_CORE_DIR}
    DEPENDS ${GENERATE_API_SCRIPT} ${BINDING_FILES} ${ENGINE_HEADERS}
    COMMENT "Generating engine_api_suggestions.h from Lua bindings and C++ headers"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

include_directories (SYSTEM ${DORIAX_ROOT}/libs/sokol)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/json)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/lua)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/luabridge3)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/stb)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/harfbuzz)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/sheenbidi/Headers)
include_directories (SYSTEM ${DORIAX_ROOT}/libs/soloud/include)

include_directories (${DORIAX_ROOT}/core)
include_directories (${DORIAX_ROOT}/core/action)
include_directories (${DORIAX_ROOT}/core/buffer)
include_directories (${DORIAX_ROOT}/core/component)
include_directories (${DORIAX_ROOT}/core/ecs)
include_directories (${DORIAX_ROOT}/core/io)
include_directories (${DORIAX_ROOT}/core/manager)
include_directories (${DORIAX_ROOT}/core/math)
include_directories (${DORIAX_ROOT}/core/object)
include_directories (${DORIAX_ROOT}/core/object/sound)
include_directories (${DORIAX_ROOT}/core/object/ui)
include_directories (${DORIAX_ROOT}/core/object/environment)
include_directories (${DORIAX_ROOT}/core/object/physics)
include_directories (${DORIAX_ROOT}/core/pool)
include_directories (${DORIAX_ROOT}/core/registry)
include_directories (${DORIAX_ROOT}/core/render)
include_directories (${DORIAX_ROOT}/core/script)
include_directories (${DORIAX_ROOT}/core/shader)
include_directories (${DORIAX_ROOT}/core/subsystem)
include_directories (${DORIAX_ROOT}/core/texture)
include_directories (${DORIAX_ROOT}/core/util)
# GamepadMappings.h, shared by GamepadDB (editor) and GamepadLinux (games)
include_directories (${DORIAX_ROOT}/platform/common)
# renderer/Renderer.h, which EditorFrame.h includes from one directory up
include_directories (${EDITOR_DIR}/backend)
if(API_BACKEND STREQUAL "linux")
    include_directories (${DORIAX_ROOT}/platform/linux) # WindowLinux.h, GamepadDB.h
elseif(API_BACKEND STREQUAL "win")
    include_directories (${DORIAX_ROOT}/platform/win) # GamepadWin.h for BackendWin
elseif(API_BACKEND STREQUAL "mac")
    # WindowMac.h for BackendMac, DoriaxGameController.h from the shared Apple path
    include_directories (${DORIAX_ROOT}/platform/mac)
    include_directories (${DORIAX_ROOT}/platform/apple)
endif()
include_directories (${DORIAX_ROOT}/renders)

include_directories (${SHADERCOMPILER_ROOT}/src)

add_subdirectory(${DORIAX_ROOT})

# The engine declares the physics options, including their defaults.
if(DORIAX_PHYSICS_2D)
    include_directories (SYSTEM ${DORIAX_ROOT}/libs/box2d/include)
endif()
if(DORIAX_PHYSICS_3D)
    include_directories (SYSTEM ${DORIAX_ROOT}/libs/joltphysics)
endif()

# Signed alongside the exes, so it needs the same metadata. MinGW prefixes the name.
if(WIN32 AND DORIAX_SHARED)
    doriax_add_windows_resource(
        DORIAX_LIB_ICON_OBJECTS
        doriax
        ""
        "${CMAKE_SHARED_LIBRARY_PREFIX}doriax${CMAKE_SHARED_LIBRARY_SUFFIX}"
        "Doriax Engine Runtime"
    )
    target_sources(doriax PRIVATE ${DORIAX_LIB_ICON_OBJECTS})
endif()

# The engine compiles sokol's Vulkan backend, so it needs the headers and the
# loader itself. Only the standalone engine build sets this up on its own.
if(GRAPHIC_BACKEND STREQUAL "vulkan")
    target_link_libraries(doriax PUBLIC Vulkan::Vulkan)
endif()

add_subdirectory(${SHADERCOMPILER_ROOT} EXCLUDE_FROM_ALL)

add_subdirectory(libs/nfd)
add_subdirectory(libs/yaml-cpp)

foreach(lib IN ITEMS nfd yaml-cpp)
    target_compile_options(${lib} PRIVATE ${EXTERNAL_WARNING_FLAG})
    # Reaches the headers they export to the editor, which ImGui gets below
    get_target_property(lib_includes ${lib} INTERFACE_INCLUDE_DIRECTORIES)
    set_target_properties(${lib} PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${lib_includes}")
endforeach()

# Add ImGui source files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

# ImGui platform backends go with API_BACKEND above, renderers with GRAPHIC_BACKEND
if(GRAPHIC_BACKEND STREQUAL "vulkan")
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
elseif(GRAPHIC_BACKEND STREQUAL "metal")
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_metal.mm)
else()
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

# ImGui has no target of its own; only one of these backends builds per platform
set_property(
    SOURCE
        ${IMGUI_SOURCES}
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_osx.mm
    APPEND PROPERTY COMPILE_OPTIONS ${EXTERNAL_WARNING_FLAG}
)

include_directories (${EDITOR_DIR})

add_custom_target(generate_shaders DEPENDS ${SHADERS_HEADER})
add_custom_target(generate_api_suggestions DEPENDS ${API_SUGGESTIONS_HEADER})

set(DORIAX_EDITOR_SOURCES
    ${EDITOR_DIR}/main.cpp
    ${EDITOR_DIR}/cli/CommandLine.cpp
    ${EDITOR_DIR}/App.cpp
    ${EDITOR_DIR}/AppSettings.cpp
    ${EDITOR_DIR}/Theme.cpp
    ${EDITOR_DIR}/Factory.cpp
    ${EDITOR_DIR}/Out.cpp
    ${EDITOR_DIR}/Platform.cpp
    ${EDITOR_DIR}/Project.cpp
    ${EDITOR_DIR}/Catalog.cpp
    ${EDITOR_DIR}/Conector.cpp
    ${EDITOR_DIR}/EditorHost.cpp
    ${EDITOR_DIR}/Generator.cpp
    ${EDITOR_DIR}/Stream.cpp
    ${EDITOR_DIR}/Workspace.cpp
    ${EDITOR_DIR}/Exporter.cpp

    ${EDITOR_DIR}/ai/AiPathUtils.cpp
    ${EDITOR_DIR}/ai/AiProvider.cpp
    ${EDITOR_DIR}/ai/AiService.cpp
    ${EDITOR_DIR}/ai/AiEngineApiContext.cpp
    ${EDITOR_DIR}/ai/AiTerrainCapability.cpp
    ${EDITOR_DIR}/ai/AiTypes.cpp
    ${EDITOR_DIR}/ai/ConversationStore.cpp
    ${EDITOR_DIR}/ai/EditorActionExecutor.cpp
    ${EDITOR_DIR}/ai/EditorActionRegistry.cpp
    ${EDITOR_DIR}/ai/HttpClient.cpp
    ${EDITOR_DIR}/ai/ModelCatalog.cpp
    ${EDITOR_DIR}/ai/RateLimitRetry.cpp
    ${EDITOR_DIR}/ai/SecretStore.cpp

    ${EDITOR_DIR}/util/ShaderHeaderBuilder.cpp
    ${EDITOR_DIR}/util/BidiText.cpp
    ${EDITOR_DIR}/util/CrashHandler.cpp
    ${EDITOR_DIR}/util/GraphicUtils.cpp
    ${EDITOR_DIR}/util/CameraTextureLink.cpp
    ${EDITOR_DIR}/util/Clipboard.cpp
    ${EDITOR_DIR}/util/EntityBundle.cpp
    ${EDITOR_DIR}/util/ProjectUtils.cpp
    ${EDITOR_DIR}/util/ScriptParser.cpp
    ${EDITOR_DIR}/util/PngWriter.cpp
    ${EDITOR_DIR}/util/TerrainMapFileWriter.cpp
    ${EDITOR_DIR}/util/TerrainMapUtils.cpp
    ${EDITOR_DIR}/util/TerrainErosion.cpp
    ${EDITOR_DIR}/util/UIUtils.cpp
    ${EDITOR_DIR}/util/CommandRunner.cpp
    ${EDITOR_DIR}/util/ShellEnv.cpp

    ${EDITOR_DIR}/shader/ShaderBuilder.cpp

    ${EDITOR_DIR}/window/Properties.cpp
    ${EDITOR_DIR}/window/OutputWindow.cpp
    ${EDITOR_DIR}/window/SceneWindow.cpp
    ${EDITOR_DIR}/window/Structure.cpp
    ${EDITOR_DIR}/window/ResourcesWindow.cpp
    ${EDITOR_DIR}/window/ImageViewerWindow.cpp
    ${EDITOR_DIR}/window/CodeEditor.cpp
    ${EDITOR_DIR}/window/AnimationWindow.cpp
    ${EDITOR_DIR}/window/TerrainEditWindow.cpp
    ${EDITOR_DIR}/window/AiChatWindow.cpp
    ${EDITOR_DIR}/window/LoadingWindow.cpp
    ${EDITOR_DIR}/window/dialog/SceneSaveDialog.cpp
    ${EDITOR_DIR}/window/dialog/ComponentAddDialog.cpp
    ${EDITOR_DIR}/window/dialog/ProjectSaveDialog.cpp
    ${EDITOR_DIR}/window/dialog/ScriptCreateDialog.cpp
    ${EDITOR_DIR}/window/dialog/ShaderForkDialog.cpp
    ${EDITOR_DIR}/window/dialog/TextureSlicerToolDialog.cpp
    ${EDITOR_DIR}/window/dialog/ExportWindow.cpp
    ${EDITOR_DIR}/window/dialog/EditorSettingsWindow.cpp
    ${EDITOR_DIR}/window/dialog/ProjectSettingsWindow.cpp
    ${EDITOR_DIR}/window/dialog/BundlesWindow.cpp
    ${EDITOR_DIR}/window/dialog/AboutWindow.cpp
    ${EDITOR_DIR}/window/dialog/ScenesWindow.cpp
    ${EDITOR_DIR}/window/dialog/AiSettingsWindow.cpp
    ${EDITOR_DIR}/window/widget/CustomTextEditor.cpp
    ${EDITOR_DIR}/window/widget/InputTextContextMenu.cpp
    ${EDITOR_DIR}/window/widget/SemanticSuggestions.cpp
    ${EDITOR_DIR}/window/widget/SelectableTextView.cpp

    ${EDITOR_DIR}/render/ToolsLayer.cpp
    ${EDITOR_DIR}/render/UILayer.cpp
    ${EDITOR_DIR}/render/SceneRender.cpp
    ${EDITOR_DIR}/render/SceneRender2D.cpp
    ${EDITOR_DIR}/render/SceneRender3D.cpp
    ${EDITOR_DIR}/render/gizmo/TranslateGizmo.cpp
    ${EDITOR_DIR}/render/gizmo/RotateGizmo.cpp
    ${EDITOR_DIR}/render/gizmo/ScaleGizmo.cpp
    ${EDITOR_DIR}/render/gizmo/Object2DGizmo.cpp
    ${EDITOR_DIR}/render/gizmo/AnchorGizmo.cpp
    ${EDITOR_DIR}/render/gizmo/ViewportGizmo.cpp
    ${EDITOR_DIR}/render/preview/MaterialRender.cpp
    ${EDITOR_DIR}/render/preview/MeshPreviewRender.cpp
    ${EDITOR_DIR}/render/preview/DirectionRender.cpp
    ${EDITOR_DIR}/render/preview/ModelRender.cpp

    ${EDITOR_DIR}/command/CommandHandle.cpp
    ${EDITOR_DIR}/command/CommandHistory.cpp
    ${EDITOR_DIR}/command/type/AddChildSceneCmd.cpp
    ${EDITOR_DIR}/command/type/AddComponentCmd.cpp
    ${EDITOR_DIR}/command/type/RemoveComponentCmd.cpp
    ${EDITOR_DIR}/command/type/CreateEntityCmd.cpp
    ${EDITOR_DIR}/command/type/CreateTrackKeyCmd.cpp
    ${EDITOR_DIR}/command/type/DeleteEntityCmd.cpp
    ${EDITOR_DIR}/command/type/DuplicateEntityCmd.cpp
    ${EDITOR_DIR}/command/type/MoveEntityOrderCmd.cpp
    ${EDITOR_DIR}/command/type/ObjectTransformCmd.cpp
    ${EDITOR_DIR}/command/type/RemoveChildSceneCmd.cpp
    ${EDITOR_DIR}/command/type/SetChildSceneStartActiveCmd.cpp
    ${EDITOR_DIR}/command/type/EntityNameCmd.cpp
    ${EDITOR_DIR}/command/type/ImportEntityBundleCmd.cpp
    ${EDITOR_DIR}/command/type/RemoveEntityFromBundleCmd.cpp
    ${EDITOR_DIR}/command/type/AddEntityToBundleCmd.cpp
    ${EDITOR_DIR}/command/type/MeshChangeCmd.cpp
    ${EDITOR_DIR}/command/type/ModelLoadCmd.cpp
    ${EDITOR_DIR}/command/type/SceneNameCmd.cpp
    ${EDITOR_DIR}/command/type/CopyFileCmd.cpp
    ${EDITOR_DIR}/command/type/RenameFileCmd.cpp
    ${EDITOR_DIR}/command/type/CreateDirCmd.cpp
    ${EDITOR_DIR}/command/type/DeleteFileCmd.cpp
    ${EDITOR_DIR}/command/type/SetMainCameraCmd.cpp
    ${EDITOR_DIR}/command/type/CreateMaterialFileCmd.cpp
    ${EDITOR_DIR}/command/type/UnlinkMaterialCmd.cpp
    ${EDITOR_DIR}/command/type/LinkMaterialCmd.cpp
    ${EDITOR_DIR}/command/type/CreateEntityBundleCmd.cpp
    ${EDITOR_DIR}/command/type/ComponentToBundleLocalCmd.cpp
    ${EDITOR_DIR}/command/type/ComponentToBundleSharedCmd.cpp
    ${EDITOR_DIR}/command/type/ForkShaderCmd.cpp
    ${EDITOR_DIR}/command/type/TerrainMapPatchCmd.cpp

    ${IMGUI_SOURCES}
    ${BACKEND_SOURCES}
)

if(APPLE)
    # NSPasteboard image/file-URL reading for the AI chat attachment paste.
    list(APPEND DORIAX_EDITOR_SOURCES ${EDITOR_DIR}/util/ClipboardMac.mm)
endif()

if(WIN32)
    list(APPEND DORIAX_EDITOR_SOURCES ${EDITOR_DIR}/util/MsBuildProgress.cpp)
endif()

function(configure_doriax_editor_target target_name)
    add_dependencies(${target_name} generate_shaders generate_api_suggestions)

    target_include_directories(${target_name} SYSTEM PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${OPENGL_INCLUDE_DIR}
    )

    target_include_directories(${target_name} PRIVATE
        ${GENERATED_DIR}
    )

    target_link_libraries(
        ${target_name}

        PRIVATE

        ${BACKEND_LIBS}
        ${OPENGL_LIBRARIES}
        Threads::Threads
        doriax
        shadercompiler
        nfd
        yaml-cpp
    )

    if(WIN32)
        # dbghelp resolves the crash-report backtrace.
        target_link_libraries(${target_name} PRIVATE winhttp shell32 dbghelp)
    else()
        target_link_libraries(${target_name} PRIVATE CURL::libcurl)
        # -rdynamic names the editor's own frames in the crash backtrace, at about 10 MB of
        # symbol table. A Release build stays lean and is resolved with addr2line instead.
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
            set_target_properties(${target_name} PROPERTIES ENABLE_EXPORTS ON)
        endif()
    endif()

    if(X11_FOUND)
        target_compile_definitions(${target_name} PRIVATE DORIAX_HAS_X11_CLIPBOARD)
        target_link_libraries(${target_name} PRIVATE X11::X11)
    endif()

    if(APPLE)
        target_link_libraries(${target_name} PRIVATE "-framework AppKit")
    endif()

    set_target_properties(
        ${target_name}

        PROPERTIES

        CXX_STANDARD ${DORIAX_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        OBJCXX_STANDARD ${DORIAX_CXX_STANDARD}
        OBJCXX_STANDARD_REQUIRED ON
    )

    if(APPLE)
        set_target_properties(${target_name} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path"
        )
    elseif(UNIX)
        set_target_properties(${target_name} PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
endfunction()

add_executable(
    doriax-editor

    ${PLATFORM_EXEC_FLAGS}

    ${DORIAX_EDITOR_SOURCES}
    ${DORIAX_EDITOR_ICON_OBJECTS}
)

configure_doriax_editor_target(doriax-editor)

if(WIN32)
    doriax_add_windows_manifest(doriax-editor)
endif()

if(PLATFORM_PROPERTIES_FLAGS)
    set_target_properties(doriax-editor PROPERTIES ${PLATFORM_PROPERTIES_FLAGS})
endif()

# The dylib goes in Contents/MacOS, where @loader_path already finds it.
if(APPLE)
    set(DORIAX_BUNDLE_NAME "Doriax")
    set(DORIAX_BUNDLE_IDENTIFIER "org.doriax.editor")
    set(DORIAX_BUNDLE_ICON_NAME "doriax.icns")

    # CFBundleShortVersionString takes digits and dots only.
    string(REPLACE "," "." DORIAX_BUNDLE_VERSION "${DORIAX_VERSION_NUMERIC}")
    string(REGEX REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+).*$" "\\1" DORIAX_BUNDLE_VERSION "${DORIAX_BUNDLE_VERSION}")

    # engine/CMakeLists.txt sets this in its own scope, which does not reach here.
    set(DORIAX_BUNDLE_MIN_OS "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(NOT DORIAX_BUNDLE_MIN_OS)
        set(DORIAX_BUNDLE_MIN_OS "12.0")
    endif()

    set(DORIAX_ICON_PNG "${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/icons/doriax-editor.png")
    set(DORIAX_ICONSET "${CMAKE_CURRENT_BINARY_DIR}/resources/doriax.iconset")
    set(DORIAX_ICNS "${CMAKE_CURRENT_BINARY_DIR}/resources/${DORIAX_BUNDLE_ICON_NAME}")

    add_custom_command(
        OUTPUT ${DORIAX_ICNS}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${DORIAX_ICONSET}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DORIAX_ICONSET}
        COMMAND sips -z 16 16     ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_16x16.png
        COMMAND sips -z 32 32     ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_16x16@2x.png
        COMMAND sips -z 32 32     ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_32x32.png
        COMMAND sips -z 64 64     ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_32x32@2x.png
        COMMAND sips -z 128 128   ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_128x128.png
        COMMAND sips -z 256 256   ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_128x128@2x.png
        COMMAND sips -z 256 256   ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_256x256.png
        COMMAND sips -z 512 512   ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_256x256@2x.png
        COMMAND sips -z 512 512   ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_512x512.png
        COMMAND sips -z 1024 1024 ${DORIAX_ICON_PNG} --out ${DORIAX_ICONSET}/icon_512x512@2x.png
        COMMAND iconutil -c icns ${DORIAX_ICONSET} -o ${DORIAX_ICNS}
        DEPENDS ${DORIAX_ICON_PNG}
        COMMENT "Generating ${DORIAX_BUNDLE_ICON_NAME}"
        VERBATIM
    )

    target_sources(doriax-editor PRIVATE ${DORIAX_ICNS})
    set_source_files_properties(${DORIAX_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/macos/Info.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/resources/Info.plist"
        @ONLY
    )

    set_target_properties(doriax-editor PROPERTIES
        OUTPUT_NAME "${DORIAX_BUNDLE_NAME}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/resources/Info.plist"
    )

    if(DORIAX_SHARED)
        add_custom_command(TARGET doriax-editor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:doriax> $<TARGET_FILE_DIR:doriax-editor>
            COMMENT "Copying the engine runtime into the bundle"
        )
    endif()
endif()

# In the bundle this has to be Resources, not MacOS: codesign treats everything in
# Contents/MacOS as code. FileUtils::getEngineDir() looks in both places.
if(APPLE)
    set(DORIAX_EDITOR_ENGINE_DIR $<TARGET_BUNDLE_CONTENT_DIR:doriax-editor>/Resources/engine)
    # Build trees from before the move still hold a copy in MacOS, which codesign rejects.
    set(DORIAX_CLEAN_STALE_ENGINE COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_FILE_DIR:doriax-editor>/engine)
else()
    set(DORIAX_EDITOR_ENGINE_DIR $<TARGET_FILE_DIR:doriax-editor>/engine)
    set(DORIAX_CLEAN_STALE_ENGINE)
endif()

add_custom_target(copy_doriax_editor_engine
    ${DORIAX_CLEAN_STALE_ENGINE}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/engine
        ${DORIAX_EDITOR_ENGINE_DIR}
    COMMENT "Copying engine next to the editor"
)

add_dependencies(doriax-editor copy_doriax_editor_engine)

set(DORIAX_EDITOR_INSTALL_TARGETS doriax-editor)

# Windows GUI-subsystem executables do not behave like normal CLI programs
# when launched from cmd.exe/PowerShell. Provide a console-subsystem twin for
# automation while keeping Unix-like platforms on a single editor binary.
#
# EXCLUDE_FROM_ALL keeps it out of the default ("all") build, so local and IDE
# builds only produce doriax-editor. Release builds compile it explicitly by
# name (`cmake --build --target doriax-editor-cmd`, see
# .github/workflows/cmake.yml); the install rule below still installs it because
# CI builds it before installing.
if(WIN32)
    # Its own icon because both executables install to the same folder
    doriax_add_windows_resource(
        DORIAX_EDITOR_CMD_ICON_OBJECTS
        doriax-editor-cmd
        "doriax-editor-cmd.ico"
        "doriax-editor-cmd.exe"
        "Doriax Engine Editor (command line)"
    )

    add_executable(
        doriax-editor-cmd

        EXCLUDE_FROM_ALL

        ${DORIAX_EDITOR_SOURCES}
        ${DORIAX_EDITOR_CMD_ICON_OBJECTS}
    )

    configure_doriax_editor_target(doriax-editor-cmd)
    doriax_add_windows_manifest(doriax-editor-cmd)
    add_dependencies(doriax-editor-cmd copy_doriax_editor_engine)
    list(APPEND DORIAX_EDITOR_INSTALL_TARGETS doriax-editor-cmd)
endif()

# The bundle already holds the dylib and engine/.
if(APPLE)
    install(TARGETS doriax-editor
            CONFIGURATIONS Release MinSizeRel
            BUNDLE DESTINATION bin
    )
else()
    install(TARGETS ${DORIAX_EDITOR_INSTALL_TARGETS} doriax
            CONFIGURATIONS Release MinSizeRel
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION bin
            ARCHIVE DESTINATION bin
    )

    if(DORIAX_MINGW_RUNTIME)
        install(FILES ${DORIAX_MINGW_RUNTIME}
                CONFIGURATIONS Release MinSizeRel
                DESTINATION bin
        )
    endif()

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/engine
            DESTINATION bin
            CONFIGURATIONS Release MinSizeRel
    )
endif()

# Desktop integration: a launcher entry plus the themed icons the launcher,
# taskbar and alt-tab switcher look up by the "doriax-editor" icon name.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/linux/doriax-editor.desktop
            DESTINATION share/applications
            CONFIGURATIONS Release MinSizeRel
    )

    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${EDITOR_DIR}/resources/linux/org.doriax.editor.metainfo.xml
            DESTINATION share/metainfo
            CONFIGURATIONS Release MinSizeRel
    )

    foreach(icon_size IN ITEMS 16 32 48 64 128 256 512)
        install(FILES ${DORIAX_LINUX_ICON_DIR}/doriax-editor-${icon_size}.png
                DESTINATION share/icons/hicolor/${icon_size}x${icon_size}/apps
                RENAME doriax-editor.png
                CONFIGURATIONS Release MinSizeRel
        )
    endforeach()
endif()
