#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")

option(DORIAX_SHARED "Build doriax as a shared library" OFF)

if(DORIAX_SHARED)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    set(DORIAX_LIB_TYPE SHARED)
else()
    set(DORIAX_LIB_TYPE STATIC)
endif()

if (WIN32)
    if (MSVC)
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