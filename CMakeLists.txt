cmake_minimum_required(VERSION 3.20)
project(doriax-editor)

set(DORIAX_SHARED ON)
set(GRAPHIC_BACKEND "glcore")

set(ENABLE_OPT OFF CACHE BOOL "Enables spirv-opt capability if present" FORCE)

if(NOT API_BACKEND)
    set(API_BACKEND "glfw")
endif()
message(STATUS "API backend is set to ${API_BACKEND}")

if(POLICY CMP0072)
  cmake_policy(SET CMP0072 NEW) # NEW for GLVND, OLD for legacy OpenGL
endif()

if(WIN32)
    set(PLATFORM_EXEC_FLAGS WIN32)
 
    if(MSVC)
        set(PLATFORM_PROPERTIES_FLAGS LINK_FLAGS "/ENTRY:mainCRTStartup")
        if(DORIAX_SHARED)
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        else()
            set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        endif()
    endif()

    if(MINGW)
        set(MINGW_STATIC_FLAGS -static -static-libgcc -static-libstdc++)
    endif()

    set(CMAKE_PREFIX_PATH "$ENV{ProgramFiles}/GLFW" ${CMAKE_PREFIX_PATH})
    set(CMAKE_PREFIX_PATH "$ENV{ProgramFiles\(X86\)}/GLFW" ${CMAKE_PREFIX_PATH})
endif()

set(IMGUI_DIR libs/imgui)
set(EDITOR_DIR editor)

# Find OpenGL
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(BACKEND_SOURCES)
set(BACKEND_LIBS)

if(API_BACKEND STREQUAL "glfw")
    find_package(glfw3 REQUIRED)

    if(NOT DEFINED GLFW_LIBRARIES)
        set(GLFW_LIBRARIES glfw)
    endif()

    list(APPEND BACKEND_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${EDITOR_DIR}/backend/BackendGLFW.cpp
    )

    list(APPEND BACKEND_LIBS
        ${GLFW_LIBRARIES}
    )
endif()

if(API_BACKEND STREQUAL "sdl")
    find_package(SDL2 REQUIRED)

    list(APPEND BACKEND_SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
        ${EDITOR_DIR}/backend/BackendSDL.cpp
    )

    list(APPEND BACKEND_LIBS
        ${SDL2_LIBRARIES}
    )
endif()

set(DORIAX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/engine)
set(SHADERCOMPILER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/shadercompiler)

add_definitions("-DDORIAX_EDITOR")
add_definitions("-DDORIAX_CRASH_GUARD")

add_definitions("-DNO_LUA_INIT")
#add_definitions("-DNO_CPP_INIT")
add_definitions("-DSOKOL_GLCORE")
add_definitions("-DWITH_MINIAUDIO") # For SoLoud
set(NFD_PORTAL ON CACHE BOOL "Use xdg-desktop-portal instead of GTK") # For NFD

set(SHADERS_DIR "${CMAKE_SOURCE_DIR}/engine/tools/shaderlib")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(SHADERS_HEADER "${GENERATED_DIR}/shaders.h")
set(GENERATE_SHADERS_SCRIPT "${CMAKE_SOURCE_DIR}/generate_shaders.cmake")

file(GLOB_RECURSE SHADER_FILES "${SHADERS_DIR}/*.glsl" "${SHADERS_DIR}/*.frag" "${SHADERS_DIR}/*.vert")

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

include_directories (${CMAKE_CURRENT_SOURCE_DIR})

include_directories (${DORIAX_ROOT}/libs/sokol)
include_directories (${DORIAX_ROOT}/libs/lua)
include_directories (${DORIAX_ROOT}/libs/luabridge3)
include_directories (${DORIAX_ROOT}/libs/box2d/include)
include_directories (${DORIAX_ROOT}/libs/joltphysics)
include_directories (${DORIAX_ROOT}/libs/stb)

include_directories (${DORIAX_ROOT}/core)
include_directories (${DORIAX_ROOT}/core/action)
include_directories (${DORIAX_ROOT}/core/buffer)
include_directories (${DORIAX_ROOT}/core/component)
include_directories (${DORIAX_ROOT}/core/ecs)
include_directories (${DORIAX_ROOT}/core/io)
include_directories (${DORIAX_ROOT}/core/manager)
include_directories (${DORIAX_ROOT}/core/math)
include_directories (${DORIAX_ROOT}/core/object)
include_directories (${DORIAX_ROOT}/core/object/audio)
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
include_directories (${DORIAX_ROOT}/renders)

include_directories (${SHADERCOMPILER_ROOT}/src)

add_subdirectory(${DORIAX_ROOT})
add_subdirectory(${SHADERCOMPILER_ROOT} EXCLUDE_FROM_ALL)

add_subdirectory(libs/nfd)
add_subdirectory(libs/yaml-cpp)

# Add ImGui source files
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

include_directories (${EDITOR_DIR})

if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
endif()

add_custom_target(generate_shaders DEPENDS ${SHADERS_HEADER})

add_executable(
    doriax-editor

    ${PLATFORM_EXEC_FLAGS}

    ${EDITOR_DIR}/main.cpp
    ${EDITOR_DIR}/App.cpp
    ${EDITOR_DIR}/AppSettings.cpp
    ${EDITOR_DIR}/Factory.cpp
    ${EDITOR_DIR}/Out.cpp
    ${EDITOR_DIR}/Platform.cpp
    ${EDITOR_DIR}/Project.cpp
    ${EDITOR_DIR}/Catalog.cpp
    ${EDITOR_DIR}/Conector.cpp
    ${EDITOR_DIR}/Generator.cpp
    ${EDITOR_DIR}/Stream.cpp
    ${EDITOR_DIR}/Exporter.cpp

    ${EDITOR_DIR}/util/GraphicUtils.cpp
    ${EDITOR_DIR}/util/EntityBundle.cpp
    ${EDITOR_DIR}/util/ProjectUtils.cpp
    ${EDITOR_DIR}/util/ScriptParser.cpp
    ${EDITOR_DIR}/util/UIUtils.cpp

    ${EDITOR_DIR}/shader/ShaderBuilder.cpp

    ${EDITOR_DIR}/window/Properties.cpp
    ${EDITOR_DIR}/window/OutputWindow.cpp
    ${EDITOR_DIR}/window/SceneWindow.cpp
    ${EDITOR_DIR}/window/Structure.cpp
    ${EDITOR_DIR}/window/ResourcesWindow.cpp
    ${EDITOR_DIR}/window/CodeEditor.cpp
    ${EDITOR_DIR}/window/AnimationWindow.cpp
    ${EDITOR_DIR}/window/LoadingWindow.cpp
    ${EDITOR_DIR}/window/dialog/SceneSaveDialog.cpp
    ${EDITOR_DIR}/window/dialog/ComponentAddDialog.cpp
    ${EDITOR_DIR}/window/dialog/ProjectSaveDialog.cpp
    ${EDITOR_DIR}/window/dialog/ScriptCreateDialog.cpp
    ${EDITOR_DIR}/window/dialog/TextureSlicerToolDialog.cpp
    ${EDITOR_DIR}/window/dialog/ExportWindow.cpp
    ${EDITOR_DIR}/window/dialog/ProjectSettingsWindow.cpp
    ${EDITOR_DIR}/window/widget/CustomTextEditor.cpp
    ${EDITOR_DIR}/window/widget/SemanticSuggestions.cpp

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
    ${EDITOR_DIR}/command/type/DeleteEntityCmd.cpp
    ${EDITOR_DIR}/command/type/DuplicateEntityCmd.cpp
    ${EDITOR_DIR}/command/type/MoveEntityOrderCmd.cpp
    ${EDITOR_DIR}/command/type/ObjectTransformCmd.cpp
    ${EDITOR_DIR}/command/type/RemoveChildSceneCmd.cpp
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

    ${IMGUI_SOURCES}
    ${BACKEND_SOURCES}
)

add_dependencies(doriax-editor generate_shaders)

# Include directories
target_include_directories(doriax-editor PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${OPENGL_INCLUDE_DIR}
    ${GENERATED_DIR}
)

# Link libraries
target_link_libraries(
    doriax-editor
    
    PRIVATE
    
    ${BACKEND_LIBS}
    ${OPENGL_LIBRARIES}
    ${MINGW_STATIC_FLAGS}
    Threads::Threads
    doriax
    shadercompiler
    nfd
    yaml-cpp
)

set_target_properties(
    doriax-editor

    PROPERTIES

    ${PLATFORM_PROPERTIES_FLAGS}
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Copy doriax engine directory to executable output directory for doriax-editor
add_custom_command(TARGET doriax-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/engine
        $<TARGET_FILE_DIR:doriax-editor>/engine
    COMMENT "Copying engine to executable directory"
)

install(TARGETS doriax-editor doriax
        CONFIGURATIONS Release MinSizeRel 
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
        ARCHIVE DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/engine
        DESTINATION bin
        CONFIGURATIONS Release MinSizeRel
)
