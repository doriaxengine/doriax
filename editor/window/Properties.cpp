// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Properties.h"
#include "SceneWindow.h"

#include "imgui_internal.h"

#include "util/Util.h"
#include "util/UIUtils.h"
#include "util/FileDialogs.h"
#include "util/EntityPayload.h"
#include "util/CameraTextureLink.h"
#include "external/IconsFontAwesome6.h"
#include "command/CommandHandle.h"
#include "command/type/PropertyCmd.h"
#include "command/type/MultiPropertyCmd.h"
#include "command/type/UnlinkMaterialCmd.h"
#include "command/type/LinkMaterialCmd.h"
#include "command/type/EntityNameCmd.h"
#include "command/type/SceneNameCmd.h"
#include "command/type/MeshChangeCmd.h"
#include "command/type/ModelLoadCmd.h"
#include "command/type/SetMainCameraCmd.h"
#include "command/type/AddComponentCmd.h"
#include "command/type/RemoveComponentCmd.h"
#include "command/type/ComponentToBundleSharedCmd.h"
#include "command/type/ComponentToBundleLocalCmd.h"
#include "command/type/ForkShaderCmd.h"
#include "command/type/ScenePropertyCmd.h"
#include "render/SceneRender2D.h"
#include "window/ResourcesWindow.h"
#include "window/Structure.h"
#include "App.h"
#include "Backend.h"
#include "Theme.h"
#include "Widgets.h"
#include "Factory.h"
#include "component/ActionComponent.h"
#include "component/AlphaActionComponent.h"
#include "component/AnimationComponent.h"
#include "component/BoneComponent.h"
#include "component/ColorActionComponent.h"
#include "component/KeyframeTracksComponent.h"
#include "component/PositionActionComponent.h"
#include "component/RotateTracksComponent.h"
#include "component/RotationActionComponent.h"
#include "component/ScaleActionComponent.h"
#include "component/ScaleTracksComponent.h"
#include "component/TimedActionComponent.h"
#include "component/TranslateTracksComponent.h"
#include "component/MorphTracksComponent.h"
#include "component/ParticlesComponent.h"
#include "component/LinesComponent.h"
#include "component/PointsComponent.h"
#include "component/PolygonComponent.h"
#include "component/MeshPolygonComponent.h"
#include "component/PanelComponent.h"
#include "util/SHA1.h"
#include "util/ProjectUtils.h"
#include "Stream.h"
#include "Out.h"
#include "subsystem/ActionSystem.h"
#include "subsystem/PhysicsSystem.h"
#include "subsystem/AudioSystem.h"
#include "action/Action.h"
#include "pool/TexturePool.h"
#include "yaml-cpp/yaml.h"
#include "soloud.h"
#include "soloud_wav.h"

#include <map>
#include <type_traits>
#include <cstring>
#include <fstream>
#include <cctype>
#include <cfloat>
#include <algorithm>
#include <unordered_set>
#include <memory>

using namespace doriax;

// Assets are stored relative to the assets root. A file outside it still previews from
// its absolute path, but the drop is refused on delivery.
static std::string droppedAssetPath(editor::Project* project, const std::string& path, bool insideAssets) {
    return insideAssets ? project->normalizeToAssetsRelative(path).generic_string()
                        : std::filesystem::path(path).generic_string();
}

// Redraws the component a dropped texture belongs to, used when a preview is undone.
static void markTextureUpdate(Scene* scene, Entity entity, editor::ComponentType componentType) {
    if (componentType == editor::ComponentType::MeshComponent) {
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        for (unsigned int i = 0; i < mesh.numSubmeshes; i++) {
            mesh.submeshes[i].needUpdateTexture = true;
        }
        editor::Catalog::updateSpriteBatch(scene, entity);
    }
    if (componentType == editor::ComponentType::UIComponent) {
        scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
    }
    if (componentType == editor::ComponentType::SkyComponent) {
        scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
    }
    if (componentType == editor::ComponentType::WaterComponent) {
        scene->getComponent<WaterComponent>(entity).needUpdateTexture = true;
    }
}

// The file name of a fork, the .frag's when its entry points are separate files.
static std::string shaderForkName(const std::string& shader) {
    return std::filesystem::path(editor::Util::resolveCustomShaderPaths(shader).frag).stem().string();
}

// The line over a fork's members, "NewSceneMesh Uniforms".
static void shaderUniformsSeparator(const std::string& shader) {
    ImGui::SeparatorText((shaderForkName(shader) + " Uniforms").c_str());
}

// The engine-written members first, each group in declaration order.
static std::vector<ShaderUniform> orderShaderUniforms(const std::vector<ShaderUniform>& members) {
    std::vector<ShaderUniform> ordered;
    ordered.reserve(members.size());
    for (bool reserved : {true, false}) {
        for (const ShaderUniform& uniform : members) {
            if (ShaderUniforms::isReserved(uniform.name) == reserved)
                ordered.push_back(uniform);
        }
    }
    return ordered;
}

// Each name once, in block order (the vertex block first).
static void appendShaderUniformMembers(std::vector<ShaderUniform>& members, const std::vector<const CustomUniformBlock*>& blocks) {
    for (const CustomUniformBlock* block : blocks) {
        for (const ShaderUniform& uniform : block->members) {
            bool listed = false;
            for (const ShaderUniform& existing : members) {
                if (existing.name == uniform.name) {
                    listed = true;
                    break;
                }
            }
            if (!listed)
                members.push_back(uniform);
        }
    }
}

// The color blocks of the components rendering with their scene default fork, once per shader
// variant (variants can declare different members).
struct DefaultShaderUse {
    std::vector<const CustomUniformBlock*> blocks;
    bool used = false;   // a component renders with the fork
    bool failed = false; // a component fell back to the built-in
};

template<typename T>
static DefaultShaderUse findDefaultShaderUse(Scene* scene, editor::ComponentType cpType) {
    DefaultShaderUse use;
    std::vector<const ShaderRender*> variants;
    auto addVariant = [&](const std::shared_ptr<ShaderRender>& shader, const CustomUniformBlock& vsBlock, const CustomUniformBlock& fsBlock) {
        use.used = true;
        if (std::find(variants.begin(), variants.end(), shader.get()) != variants.end())
            return;
        variants.push_back(shader.get());
        use.blocks.push_back(&vsBlock);
        use.blocks.push_back(&fsBlock);
    };

    auto components = scene->getComponentArray<T>();
    for (size_t i = 0; i < components->size(); i++) {
        const T& component = components->getComponentFromIndex(i);
        if (!component.customShader.empty() || !component.loaded)
            continue;

        if constexpr (std::is_same_v<T, MeshComponent>) {
            for (unsigned int s = 0; s < component.numSubmeshes; s++) {
                const Submesh& submesh = component.submeshes[s];
                if (submesh.customShaderId != 0)
                    addVariant(submesh.shader, submesh.customVSParams, submesh.customFSParams);
            }
        } else if (component.customShaderId != 0) {
            addVariant(component.shader, component.customVSParams, component.customFSParams);
        }

        if (!use.used && editor::Catalog::isCustomShaderBuildFailed(scene, components->getEntity(i), cpType))
            use.failed = true;
    }
    return use;
}

static std::vector<editor::EnumEntry> entriesPrimitiveType = {
    { (int)PrimitiveType::TRIANGLES, "Triangles" },
    { (int)PrimitiveType::TRIANGLE_STRIP, "Triangle Strip" },
    { (int)PrimitiveType::POINTS, "Points" },
    { (int)PrimitiveType::LINES, "Lines" }
};

static std::vector<editor::EnumEntry> entriesMaterialAlphaMode = {
    { (int)MaterialAlphaMode::AUTO, "Auto" },
    { (int)MaterialAlphaMode::ALPHA_OPAQUE, "Opaque" },
    { (int)MaterialAlphaMode::MASK, "Mask" },
    { (int)MaterialAlphaMode::BLEND, "Blend" }
};

static std::vector<editor::EnumEntry> entriesPivotPreset = {
    { (int)PivotPreset::CENTER, "Center" },
    { (int)PivotPreset::TOP_CENTER, "Top Center" },
    { (int)PivotPreset::BOTTOM_CENTER, "Bottom Center" },
    { (int)PivotPreset::LEFT_CENTER, "Left Center" },
    { (int)PivotPreset::RIGHT_CENTER, "Right Center" },
    { (int)PivotPreset::TOP_LEFT, "Top Left" },
    { (int)PivotPreset::BOTTOM_LEFT, "Bottom Left" },
    { (int)PivotPreset::TOP_RIGHT, "Top Right" },
    { (int)PivotPreset::BOTTOM_RIGHT, "Bottom Right" }
};

static std::vector<editor::EnumEntry> entriesAnchorPreset = {
    { (int)AnchorPreset::NONE, "None" },
    { (int)AnchorPreset::TOP_LEFT, "Top Left" },
    { (int)AnchorPreset::TOP_RIGHT, "Top Right" },
    { (int)AnchorPreset::BOTTOM_LEFT, "Bottom Left" },
    { (int)AnchorPreset::BOTTOM_RIGHT, "Bottom Right" },
    { (int)AnchorPreset::CENTER_LEFT, "Center Left" },
    { (int)AnchorPreset::CENTER_TOP, "Center Top" },
    { (int)AnchorPreset::CENTER_RIGHT, "Center Right" },
    { (int)AnchorPreset::CENTER_BOTTOM, "Center Bottom" },
    { (int)AnchorPreset::CENTER, "Center" },
    { (int)AnchorPreset::LEFT_WIDE, "Left Wide" },
    { (int)AnchorPreset::TOP_WIDE, "Top Wide" },
    { (int)AnchorPreset::RIGHT_WIDE, "Right Wide" },
    { (int)AnchorPreset::BOTTOM_WIDE, "Bottom Wide" },
    { (int)AnchorPreset::VERTICAL_CENTER_WIDE, "Vertical Center Wide" },
    { (int)AnchorPreset::HORIZONTAL_CENTER_WIDE, "Horizontal Center Wide" },
    { (int)AnchorPreset::FULL_LAYOUT, "Full Layout" }
};

static std::vector<editor::EnumEntry> entriesLightType = {
    { (int)LightType::DIRECTIONAL, "Directional" },
    { (int)LightType::POINT, "Point" },
    { (int)LightType::SPOT, "Spot" }
};

static std::vector<editor::EnumEntry> entriesOccluder2DShape = {
    { (int)Occluder2DShape::AUTO_QUAD, "Auto Quad" },
    { (int)Occluder2DShape::POLYGON, "Polygon" }
};

static std::vector<editor::EnumEntry> entriesFogType = {
    { (int)FogType::LINEAR, "Linear" },
    { (int)FogType::EXPONENTIAL, "Exponential" },
    { (int)FogType::EXPONENTIALSQUARED, "Exponential Squared" }
};

static std::vector<editor::EnumEntry> entriesReflectionProbeMode = {
    { (int)ReflectionProbeMode::STATIC, "Static" },
    { (int)ReflectionProbeMode::DYNAMIC, "Dynamic" }
};

static std::vector<editor::EnumEntry> entriesReflectionProbeUpdateMode = {
    { (int)ReflectionProbeUpdateMode::ON_LOAD, "On Load" },
    { (int)ReflectionProbeUpdateMode::ON_MOVE, "On Move" },
    { (int)ReflectionProbeUpdateMode::INTERVAL, "Interval" },
    { (int)ReflectionProbeUpdateMode::MANUAL, "Manual" }
};

static std::vector<editor::EnumEntry> entriesWaterExclusionShape = {
    { (int)WaterExclusionShape::BOX, "Box" },
    { (int)WaterExclusionShape::SPHERE, "Sphere" },
    { (int)WaterExclusionShape::HULL, "Hull" }
};

static std::vector<editor::EnumEntry> entriesCameraType = {
    { (int)CameraType::CAMERA_ORTHO, "Orthographic" },
    { (int)CameraType::CAMERA_PERSPECTIVE, "Perspective" }
};

// render-target framebuffers have no mipmaps, so only the non-mipmap filters apply
static std::vector<editor::EnumEntry> entriesTextureFilter = {
    { (int)TextureFilter::NEAREST, "Nearest" },
    { (int)TextureFilter::LINEAR, "Linear" }
};

// full filter set for a texture's minification (mipmap variants trigger mipmap generation)
static std::vector<editor::EnumEntry> entriesTextureMinFilter = {
    { (int)TextureFilter::NEAREST, "Nearest" },
    { (int)TextureFilter::LINEAR, "Linear" },
    { (int)TextureFilter::NEAREST_MIPMAP_NEAREST, "Nearest Mipmap Nearest" },
    { (int)TextureFilter::NEAREST_MIPMAP_LINEAR, "Nearest Mipmap Linear" },
    { (int)TextureFilter::LINEAR_MIPMAP_NEAREST, "Linear Mipmap Nearest" },
    { (int)TextureFilter::LINEAR_MIPMAP_LINEAR, "Linear Mipmap Linear" }
};

static std::vector<editor::EnumEntry> entriesTextureWrap = {
    { (int)TextureWrap::REPEAT, "Repeat" },
    { (int)TextureWrap::MIRRORED_REPEAT, "Mirrored Repeat" },
    { (int)TextureWrap::CLAMP_TO_EDGE, "Clamp to Edge" },
    { (int)TextureWrap::CLAMP_TO_BORDER, "Clamp to Border" }
};

static std::vector<editor::EnumEntry> entriesTextureAlphaBorder = {
    { (int)TextureAlphaBorder::AUTO, "Auto" },
    { (int)TextureAlphaBorder::FIX, "Fix" },
    { (int)TextureAlphaBorder::KEEP, "Keep" }
};

static std::vector<editor::EnumEntry> entriesEmitterShape = {
    { (int)ParticleEmitterShape::Box, "Box" },
    { (int)ParticleEmitterShape::Sphere, "Sphere" },
    { (int)ParticleEmitterShape::Hemisphere, "Hemisphere" },
    { (int)ParticleEmitterShape::Circle, "Circle" },
    { (int)ParticleEmitterShape::Cone, "Cone" }
};

static std::vector<editor::EnumEntry> entriesAudioAttenuation = {
    { (int)SoundAttenuation::NO_ATTENUATION, "No Attenuation" },
    { (int)SoundAttenuation::INVERSE_DISTANCE, "Inverse Distance" },
    { (int)SoundAttenuation::LINEAR_DISTANCE, "Linear Distance" },
    { (int)SoundAttenuation::EXPONENTIAL_DISTANCE, "Exponential Distance" }
};

static std::vector<editor::EnumEntry> entriesAudioState = {
    { (int)SoundState::Playing, "Playing" },
    { (int)SoundState::Paused, "Paused" },
    { (int)SoundState::Stopped, "Stopped" }
};

static std::vector<editor::EnumEntry> entriesBodyType = {
    { (int)BodyType::STATIC, "Static" },
    { (int)BodyType::KINEMATIC, "Kinematic" },
    { (int)BodyType::DYNAMIC, "Dynamic" }
};

static std::vector<editor::EnumEntry> entriesBody3DMotionQuality = {
    { (int)Body3DMotionQuality::DISCRETE, "Discrete" },
    { (int)Body3DMotionQuality::LINEAR_CAST, "Linear Cast" }
};

static std::vector<editor::EnumEntry> entriesShape2DType = {
    { (int)Shape2DType::POLYGON, "Polygon" },
    { (int)Shape2DType::CIRCLE, "Circle" },
    { (int)Shape2DType::CAPSULE, "Capsule" },
    { (int)Shape2DType::SEGMENT, "Segment" },
    { (int)Shape2DType::CHAIN, "Chain" }
};

static std::vector<editor::EnumEntry> entriesShape3DType = {
    { (int)Shape3DType::BOX, "Box" },
    { (int)Shape3DType::SPHERE, "Sphere" },
    { (int)Shape3DType::CAPSULE, "Capsule" },
    { (int)Shape3DType::TAPERED_CAPSULE, "Tapered Capsule" },
    { (int)Shape3DType::CYLINDER, "Cylinder" },
    { (int)Shape3DType::CONVEX_HULL, "Convex Hull" },
    { (int)Shape3DType::MESH, "Mesh" },
    { (int)Shape3DType::HEIGHTFIELD, "Heightfield" }
};

static std::vector<editor::EnumEntry> entriesShape3DSource = {
    { (int)Shape3DSource::NONE, "None" },
    { (int)Shape3DSource::RAW_VERTICES, "Raw Vertices" },
    { (int)Shape3DSource::RAW_MESH, "Raw Mesh" },
    { (int)Shape3DSource::ENTITY_MESH, "Entity Mesh" },
    { (int)Shape3DSource::ENTITY_HEIGHTFIELD, "Entity Heightfield" }
};

static std::vector<editor::EnumEntry> entriesShape3DSourceConvexHull = {
    { (int)Shape3DSource::RAW_VERTICES, "Raw Vertices" },
    { (int)Shape3DSource::ENTITY_MESH, "Entity Mesh" }
};

static std::vector<editor::EnumEntry> entriesShape3DSourceMesh = {
    { (int)Shape3DSource::RAW_MESH, "Raw Mesh" },
    { (int)Shape3DSource::ENTITY_MESH, "Entity Mesh" }
};

static std::vector<editor::EnumEntry> entriesShape3DSourceHeightfield = {
    { (int)Shape3DSource::ENTITY_HEIGHTFIELD, "Entity Heightfield" }
};

static std::vector<editor::EnumEntry> entriesJoint2DType = {
    { (int)Joint2DType::DISTANCE, "Distance" },
    { (int)Joint2DType::REVOLUTE, "Revolute" },
    { (int)Joint2DType::PRISMATIC, "Prismatic" },
    { (int)Joint2DType::MOUSE, "Mouse" },
    { (int)Joint2DType::WHEEL, "Wheel" },
    { (int)Joint2DType::WELD, "Weld" },
    { (int)Joint2DType::MOTOR, "Motor" }
};

static std::vector<editor::EnumEntry> entriesJoint3DType = {
    { (int)Joint3DType::FIXED, "Fixed" },
    { (int)Joint3DType::DISTANCE, "Distance" },
    { (int)Joint3DType::POINT, "Point" },
    { (int)Joint3DType::HINGE, "Hinge" },
    { (int)Joint3DType::CONE, "Cone" },
    { (int)Joint3DType::PRISMATIC, "Prismatic" },
    { (int)Joint3DType::SWINGTWIST, "Swing Twist" },
    { (int)Joint3DType::SIXDOF, "Six DOF" },
    { (int)Joint3DType::PATH, "Path" },
    { (int)Joint3DType::GEAR, "Gear" },
    { (int)Joint3DType::RACKANDPINON, "Rack and Pinion" },
    { (int)Joint3DType::PULLEY, "Pulley" }
};

static std::vector<int> cascadeValues = { 1, 2, 3, 4, 5, 6 };
static std::vector<int> po2Values = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384 };
// reflection probe captures are clamped to [16, 1024] in RenderSystem
static std::vector<int> probeResolutionValues = { 16, 32, 64, 128, 256, 512, 1024 };

static std::vector<int> waterSubdivisionValues = { 16, 32, 64, 128, 256, 512 };

static std::vector<editor::EnumEntry> entriesActionState = {
    { (int)ActionState::Running, "Running" },
    { (int)ActionState::Paused, "Paused" },
    { (int)ActionState::Stopped, "Stopped" }
};

static std::vector<editor::EnumEntry> entriesEaseType = {
    { (int)EaseType::LINEAR, "Linear" },
    { (int)EaseType::STEP, "Step" },
    { (int)EaseType::QUAD_IN, "Quad In" },
    { (int)EaseType::QUAD_OUT, "Quad Out" },
    { (int)EaseType::QUAD_IN_OUT, "Quad In Out" },
    { (int)EaseType::CUBIC_IN, "Cubic In" },
    { (int)EaseType::CUBIC_OUT, "Cubic Out" },
    { (int)EaseType::CUBIC_IN_OUT, "Cubic In Out" },
    { (int)EaseType::QUART_IN, "Quart In" },
    { (int)EaseType::QUART_OUT, "Quart Out" },
    { (int)EaseType::QUART_IN_OUT, "Quart In Out" },
    { (int)EaseType::QUINT_IN, "Quint In" },
    { (int)EaseType::QUINT_OUT, "Quint Out" },
    { (int)EaseType::QUINT_IN_OUT, "Quint In Out" },
    { (int)EaseType::SINE_IN, "Sine In" },
    { (int)EaseType::SINE_OUT, "Sine Out" },
    { (int)EaseType::SINE_IN_OUT, "Sine In Out" },
    { (int)EaseType::EXPO_IN, "Expo In" },
    { (int)EaseType::EXPO_OUT, "Expo Out" },
    { (int)EaseType::EXPO_IN_OUT, "Expo In Out" },
    { (int)EaseType::CIRC_IN, "Circ In" },
    { (int)EaseType::CIRC_OUT, "Circ Out" },
    { (int)EaseType::CIRC_IN_OUT, "Circ In Out" },
    { (int)EaseType::ELASTIC_IN, "Elastic In" },
    { (int)EaseType::ELASTIC_OUT, "Elastic Out" },
    { (int)EaseType::ELASTIC_IN_OUT, "Elastic In Out" },
    { (int)EaseType::BACK_IN, "Back In" },
    { (int)EaseType::BACK_OUT, "Back Out" },
    { (int)EaseType::BACK_IN_OUT, "Back In Out" },
    { (int)EaseType::BOUNCE_IN, "Bounce In" },
    { (int)EaseType::BOUNCE_OUT, "Bounce Out" },
    { (int)EaseType::BOUNCE_IN_OUT, "Bounce In Out" },
    { (int)EaseType::CUSTOM, "Custom" }
};

std::vector<editor::Properties::DirtyMaterialEntry> editor::Properties::dirtyMaterials;

static bool isLastHeaderRowClicked() {
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    max.x = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

    return ImGui::IsMouseHoveringRect(min, max) && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

void editor::Properties::markMaterialDirty(unsigned int sceneId, Entity entity, int submeshIndex, const std::string& relativePath) {
    for (auto& entry : dirtyMaterials) {
        if (entry.sceneId == sceneId && entry.entity == entity && entry.submeshIndex == submeshIndex) {
            entry.timer = 0.0f;
            entry.relativePath = relativePath;
            return;
        }
    }

    dirtyMaterials.push_back({sceneId, entity, submeshIndex, relativePath, 0.0f});
}

void editor::Properties::writeDirtyMaterial(const DirtyMaterialEntry& entry) {
    SceneProject* sp = project->getScene(entry.sceneId);
    if (!sp || !sp->scene) {
        return;
    }

    MeshComponent* mesh = sp->scene->findComponent<MeshComponent>(entry.entity);
    if (!mesh || entry.submeshIndex >= mesh->numSubmeshes) {
        return;
    }

    Material& material = mesh->submeshes[entry.submeshIndex].material;
    std::filesystem::path absolutePath = project->getProjectPath() / entry.relativePath;

    try {
        std::ofstream out(absolutePath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            std::string payload = YAML::Dump(Stream::encodeMaterial(material));
            out.write(payload.c_str(), payload.size());
            out.close();

            project->linkMaterialFile(entry.sceneId, entry.entity, entry.submeshIndex, entry.relativePath);
            project->refreshLinkedMaterials(true);

            if (ResourcesWindow* resourcesWindow = Backend::getApp().getResourcesWindow()) {
                resourcesWindow->notifyResourceFileChanged(absolutePath);
            }
        }
    } catch (const std::exception& e) {
        Out::error("Error saving linked material file '%s': %s", absolutePath.string().c_str(), e.what());
    }
}

void editor::Properties::flushDirtyMaterials(float deltaTime) {
    if (dirtyMaterials.empty()) {
        return;
    }

    auto it = dirtyMaterials.begin();
    while (it != dirtyMaterials.end()) {
        it->timer += deltaTime;
        if (it->timer < materialWriteDelaySec) {
            ++it;
            continue;
        }

        writeDirtyMaterial(*it);

        it = dirtyMaterials.erase(it);
    }
}

// The scene is about to go away, so write its pending edits without waiting out the debounce
void editor::Properties::flushDirtyMaterialsForScene(uint32_t sceneId) {
    auto it = dirtyMaterials.begin();
    while (it != dirtyMaterials.end()) {
        if (it->sceneId != sceneId) {
            ++it;
            continue;
        }

        writeDirtyMaterial(*it);

        it = dirtyMaterials.erase(it);
    }
}

template<typename T>
void editor::Properties::drawScenePropertyRow(SceneProject* sceneProject, const std::string& propertyName, const char* label, ScenePropertyInputType inputType, float inputWidth, float minValue, float maxValue, const std::string& help) {
    T value = Catalog::getSceneProperty<T>(sceneProject->scene, propertyName);
    bool changed = false;

    Command* cmd = nullptr;

    // Detect whether the value differs from the factory default so we can show the
    // "reset to default" arrow next to the label (like component property rows).
    T defaultValue = Catalog::getScenePropertyDefault<T>(propertyName);
    bool defChanged = false;
    if constexpr (std::is_same_v<T, float>) {
        defChanged = std::fabs(value - defaultValue) > 1e-4f;
    } else if constexpr (std::is_same_v<T, Vector2>) {
        defChanged = std::fabs(value.x - defaultValue.x) > 1e-4f || std::fabs(value.y - defaultValue.y) > 1e-4f;
    } else if constexpr (std::is_same_v<T, Vector3>) {
        defChanged = std::fabs(value.x - defaultValue.x) > 1e-4f || std::fabs(value.y - defaultValue.y) > 1e-4f
            || std::fabs(value.z - defaultValue.z) > 1e-4f;
    } else if constexpr (std::is_same_v<T, Vector4>) {
        defChanged = std::fabs(value.x - defaultValue.x) > 1e-4f || std::fabs(value.y - defaultValue.y) > 1e-4f
            || std::fabs(value.z - defaultValue.z) > 1e-4f || std::fabs(value.w - defaultValue.w) > 1e-4f;
    } else {
        defChanged = (value != defaultValue);
    }

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label);

    // Reset-to-default arrow, only shown when the value differs from the default.
    if (defChanged) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Button((ICON_FA_ROTATE_LEFT"##reset_" + propertyName).c_str())) {
            Command* resetCmd = new ScenePropertyCmd<T>(project, sceneProject->id, propertyName, defaultValue);
            CommandHandle::get(sceneProject->id)->addCommandNoMerge(resetCmd);
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    ImGui::TableSetColumnIndex(1);

    if (inputWidth > 0.0f) {
        if (!help.empty()) {
            const float reservedHelpWidth = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
            const float availableInputWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - reservedHelpWidth);
            ImGui::SetNextItemWidth(std::min(inputWidth, availableInputWidth));
        } else {
            ImGui::SetNextItemWidth(inputWidth);
        }
    } else if (!help.empty()){
        // leave room in the value column for the trailing "(?)" marker
        ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x));
    }

    switch (inputType) {
        case ScenePropertyInputType::Checkbox:
            if constexpr (std::is_same_v<T, bool>) {
                changed = ImGui::Checkbox(("##" + propertyName).c_str(), &value);
            }
            break;
        case ScenePropertyInputType::DragFloat:
            if constexpr (std::is_same_v<T, float>) {
                changed = ImGui::DragFloat(("##" + propertyName).c_str(), &value, 0.01f);
            } else if constexpr (std::is_same_v<T, Vector2>) {
                changed = ImGui::DragFloat2(("##" + propertyName).c_str(), (float*)&value.x);
            } else if constexpr (std::is_same_v<T, Vector3>) {
                changed = ImGui::DragFloat3(("##" + propertyName).c_str(), (float*)&value.x);
            } else if constexpr (std::is_same_v<T, Vector4>) {
                changed = ImGui::DragFloat4(("##" + propertyName).c_str(), (float*)&value.x);
            }
            break;
        case ScenePropertyInputType::DragInt:
            if constexpr (std::is_same_v<T, int>) {
                changed = ImGui::DragInt(("##" + propertyName).c_str(), &value, 1.0f, (int)minValue, (int)maxValue);
            }
            break;
        case ScenePropertyInputType::SliderFloat:
            if constexpr (std::is_same_v<T, float>) {
                // Slider that can be double-clicked to type an exact value (also allows values above the max).
                ImGui::SetNextItemWidth(-1);
                changed = UIUtils::sliderFloatInput(("##" + propertyName).c_str(), &value, minValue, maxValue);
            }
            break;
        case ScenePropertyInputType::ColorRGB:
            if constexpr (std::is_same_v<T, Vector3>) {
                changed = ImGui::ColorEdit3(("##" + propertyName).c_str(), (float*)&value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
            }
            break;
        case ScenePropertyInputType::ColorRGBA:
            if constexpr (std::is_same_v<T, Vector4>) {
                changed = ImGui::ColorEdit4(("##" + propertyName).c_str(), (float*)&value.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
            }
            break;
        case ScenePropertyInputType::Combo:
            if constexpr (std::is_same_v<T, LightState>) {
                const char* lightStateNames[] = { "Off", "On", "Auto" };
                int currentItem = static_cast<int>(value);
                changed = ImGui::Combo(("##" + propertyName).c_str(), &currentItem, lightStateNames, IM_ARRAYSIZE(lightStateNames));
                if (changed) {
                    value = static_cast<LightState>(currentItem);
                }
            }
            if constexpr (std::is_same_v<T, ShadowQuality>) {
                const char* qualityNames[] = { "None", "Low", "Medium", "High" };
                int currentItem = static_cast<int>(value);
                changed = ImGui::Combo(("##" + propertyName).c_str(), &currentItem, qualityNames, IM_ARRAYSIZE(qualityNames));
                if (changed) {
                    value = static_cast<ShadowQuality>(currentItem);
                }
            }
            if constexpr (std::is_same_v<T, TextureFilter>) {
                const char* filterNames[] = { "Nearest", "Linear" };
                int currentItem = (value == TextureFilter::LINEAR) ? 1 : 0;
                changed = ImGui::Combo(("##" + propertyName).c_str(), &currentItem, filterNames, IM_ARRAYSIZE(filterNames));
                if (changed) {
                    value = (currentItem == 1) ? TextureFilter::LINEAR : TextureFilter::NEAREST;
                }
            }
            break;
    }

    if (changed) {
        cmd = new ScenePropertyCmd<T>(project, sceneProject->id, propertyName, value);
        CommandHandle::get(sceneProject->id)->addCommand(cmd);
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        if (cmd) {
            cmd->setNoMerge();
            cmd = nullptr;
        }
    }

    // after IsItemDeactivatedAfterEdit so that check still refers to the widget
    if (!help.empty()){
        ImGui::SameLine(); helpMarker(help);
    }
}

editor::Properties::SoundPreviewRuntime& editor::Properties::soundPreview() {
    return soundPreviewRuntime;
}

bool editor::Properties::ensureSoundPreviewInitialized() {
    SoundPreviewRuntime& ap = soundPreview();
    if (!AudioSystem::init()) {
        ap.error = "Failed to initialize audio preview output.";
        return false;
    }

    return true;
}

void editor::Properties::stopSoundPreview(bool unload) {
    SoundPreviewRuntime& ap = soundPreview();
    if (ap.handle != 0) {
        AudioSystem::getSoloud().stop(ap.handle);
    }

    ap.active = false;
    ap.playing = false;
    ap.handle = 0;
    ap.time = 0.0;

    if (unload) {
        ap.loaded = false;
        ap.filename.clear();
        ap.length = 0.0;
        ap.entity = NULL_ENTITY;
        ap.sceneId = 0;
    }
}

std::filesystem::path editor::Properties::resolveSoundPreviewPath(const std::string& filename) {
    return project->resolveAssetPath(filename);
}

void editor::Properties::applySoundPreviewSettings(const SoundComponent& audio) {
    SoundPreviewRuntime& ap = soundPreview();
    if (!ap.active || ap.handle == 0) {
        return;
    }

    SoLoud::Soloud& soloud = AudioSystem::getSoloud();
    soloud.setVolume(ap.handle, static_cast<float>(audio.volume));
    soloud.setRelativePlaySpeed(ap.handle, audio.speed);
    soloud.setPan(ap.handle, audio.pan);
    // Preview should always terminate even if the runtime sound is configured to loop.
    soloud.setLooping(ap.handle, false);
    soloud.setLoopPoint(ap.handle, audio.loopingPoint);
    soloud.setProtectVoice(ap.handle, audio.protectVoice);
    soloud.setInaudibleBehavior(ap.handle, audio.inaudibleBehaviorMustTick, audio.inaudibleBehaviorKill);
}

bool editor::Properties::loadSoundPreview(SceneProject* sceneProject, Entity entity, const SoundComponent& audio) {
    SoundPreviewRuntime& ap = soundPreview();
    if (audio.filename.empty()) {
        ap.error = "Select a sound file before previewing.";
        return false;
    }

    if (!ensureSoundPreviewInitialized()) {
        return false;
    }

    if (ap.loaded && ap.sceneId == sceneProject->id && ap.entity == entity && ap.filename == audio.filename) {
        return true;
    }

    stopSoundPreview(true);

    std::filesystem::path audioPath = resolveSoundPreviewPath(audio.filename);
    SoLoud::result result = ap.sample.load(audioPath.string().c_str());
    if (result != SoLoud::SOLOUD_ERRORS::SO_NO_ERROR) {
        ap.error = "Failed to load audio preview file:\n" + audioPath.string();
        return false;
    }

    ap.sample.setSingleInstance(true);
    ap.sample.setVolume(1.0f);
    ap.loaded = true;
    ap.sceneId = sceneProject->id;
    ap.entity = entity;
    ap.filename = audio.filename;
    ap.length = ap.sample.getLength();
    ap.error.clear();

    return true;
}

bool editor::Properties::startSoundPreview(SceneProject* sceneProject, Entity entity, const SoundComponent& audio) {
    if (!loadSoundPreview(sceneProject, entity, audio)) {
        return false;
    }

    SoundPreviewRuntime& ap = soundPreview();
    if (ap.active && ap.handle != 0) {
        AudioSystem::getSoloud().setPause(ap.handle, false);
        ap.playing = true;
        applySoundPreviewSettings(audio);
        return true;
    }

    ap.handle = AudioSystem::getSoloud().play(ap.sample);
    if (ap.handle == 0) {
        ap.error = "Failed to start audio preview.";
        return false;
    }

    ap.active = true;
    ap.playing = true;
    ap.time = 0.0;
    applySoundPreviewSettings(audio);

    return true;
}

void editor::Properties::pauseSoundPreview() {
    SoundPreviewRuntime& ap = soundPreview();
    if (!ap.active || ap.handle == 0) {
        return;
    }

    AudioSystem::getSoloud().setPause(ap.handle, true);
    ap.playing = false;
}

bool editor::Properties::seekSoundPreview(SceneProject* sceneProject, Entity entity, const SoundComponent& audio, double time) {
    if (!loadSoundPreview(sceneProject, entity, audio)) {
        return false;
    }

    SoundPreviewRuntime& ap = soundPreview();
    bool shouldKeepPlaying = ap.active && ap.playing;
    if (!ap.active || ap.handle == 0) {
        ap.handle = AudioSystem::getSoloud().play(ap.sample);
        if (ap.handle == 0) {
            ap.error = "Failed to start audio preview.";
            return false;
        }
        ap.active = true;
        ap.playing = false;
        shouldKeepPlaying = false;
    }

    SoLoud::Soloud& soloud = AudioSystem::getSoloud();
    SoLoud::result result = soloud.seek(ap.handle, time);
    if (result != SoLoud::SOLOUD_ERRORS::SO_NO_ERROR) {
        ap.error = "Failed to seek audio preview.";
        return false;
    }

    ap.time = time;
    ap.playing = shouldKeepPlaying;
    soloud.setPause(ap.handle, !shouldKeepPlaying);
    applySoundPreviewSettings(audio);

    return true;
}

void editor::Properties::updateSoundPreview(const SoundComponent& audio) {
    SoundPreviewRuntime& ap = soundPreview();
    if (!ap.active || ap.handle == 0) {
        return;
    }

    SoLoud::Soloud& soloud = AudioSystem::getSoloud();
    if (!soloud.isValidVoiceHandle(ap.handle)) {
        stopSoundPreview();
        return;
    }

    applySoundPreviewSettings(audio);
    ap.time = soloud.getStreamTime(ap.handle);
}

std::string editor::Properties::formatPropertyLabelValue(const PropertyData& prop) {
    if (!prop.ref) {
        return "-";
    }

    char buffer[64];
    switch (prop.type) {
        case PropertyType::Bool:
            return (*static_cast<bool*>(prop.ref)) ? "true" : "false";
        case PropertyType::Float:
            snprintf(buffer, sizeof(buffer), "%.3f", *static_cast<float*>(prop.ref));
            return buffer;
        case PropertyType::Double:
            snprintf(buffer, sizeof(buffer), "%.3f", *static_cast<double*>(prop.ref));
            return buffer;
        case PropertyType::Int:
            return std::to_string(*static_cast<int*>(prop.ref));
        case PropertyType::UInt:
            return std::to_string(*static_cast<unsigned int*>(prop.ref));
        case PropertyType::Entity:
        case PropertyType::EntityReference:
            return std::to_string(*static_cast<Entity*>(prop.ref));
        case PropertyType::String:
            return *static_cast<std::string*>(prop.ref);
        case PropertyType::Font:
            return static_cast<FontArray*>(prop.ref)->front();
        case PropertyType::Ease: {
            EaseType type = static_cast<Ease*>(prop.ref)->getType();
            for (const auto& entry : entriesEaseType) {
                if (entry.value == static_cast<int>(type)) {
                    return entry.name;
                }
            }
            return "Ease";
        }
        default:
            return "-";
    }
}

editor::Properties::Properties(Project* project){
    this->project = project;
    this->sceneWindow = nullptr;
    this->cmd = nullptr;

    this->windowOpen = true;
    this->focusRequested = false;
    this->finishProperty = false;
}

void editor::Properties::setSceneWindow(SceneWindow* sceneWindow){
    this->sceneWindow = sceneWindow;
}

void editor::Properties::setOpen(bool open){
    if (open){
        if (!windowOpen){
            focusRequested = true;
        }
        windowOpen = true;
        return;
    }

    if (windowOpen) {
        stopTransientPreviews();
    }

    windowOpen = false;
    focusRequested = false;
    nameFocusFrames = 0;
}

// F2: edit the name of what the window shows — a single entity, or the main scene
void editor::Properties::focusNameInput(){
    uint32_t sceneId = project->getSelectedSceneForProperties();
    if (!project->getScene(sceneId)) sceneId = project->getSelectedSceneId();
    if (!project->getScene(sceneId)) return;

    const size_t selected = project->getSelectedEntities(sceneId).size();
    const bool mainScene = (sceneId == project->getSelectedSceneId());
    if (selected != 1 && !(selected == 0 && mainScene)) return;

    setOpen(true);
    focusRequested = true;
    nameFocusFrames = 3; // the field activates a frame after SetKeyboardFocusHere
}

void editor::Properties::stopTransientPreviews() {
    stopSoundPreview();
    stopActionPreviewIfActive();
}

void editor::Properties::clearSceneState(uint32_t sceneId) {
    if (actionPreviewing && actionPreviewSceneId == sceneId) {
        // The scene is already deleted, drop the snapshots instead of restoring them
        actionPreviewStates.clear();
        actionPreviewing = false;
        actionPreviewPlaying = false;
        actionPreviewEntity = NULL_ENTITY;
        actionPreviewSceneId = 0;
    }

    if (soundPreviewRuntime.sceneId == sceneId) {
        stopSoundPreview(true);
    }

    // Pending writes would otherwise be flushed against whatever scene takes the id
    dirtyMaterials.erase(
        std::remove_if(dirtyMaterials.begin(), dirtyMaterials.end(),
            [sceneId](const DirtyMaterialEntry& entry){ return entry.sceneId == sceneId; }),
        dirtyMaterials.end());
}

bool editor::Properties::isOpen() const{
    return windowOpen;
}

editor::RowPropertyType editor::Properties::scriptPropertyTypeToRowPropertyType(ScriptPropertyType scriptType){
    switch (scriptType) {
        case doriax::ScriptPropertyType::Bool: return RowPropertyType::Bool;
        case doriax::ScriptPropertyType::Int: return RowPropertyType::Int;
        case doriax::ScriptPropertyType::Float: return RowPropertyType::Float;
        case doriax::ScriptPropertyType::String: return RowPropertyType::String;
        case doriax::ScriptPropertyType::Vector2: return RowPropertyType::Vector2;
        case doriax::ScriptPropertyType::Vector3: return RowPropertyType::Vector3;
        case doriax::ScriptPropertyType::Vector4: return RowPropertyType::Vector4;
        case doriax::ScriptPropertyType::Color3: return RowPropertyType::Color3L;
        case doriax::ScriptPropertyType::Color4: return RowPropertyType::Color4L;
        case doriax::ScriptPropertyType::EntityReference: return RowPropertyType::ExternalEntity;
        default: return RowPropertyType::Custom;
    }
}

std::string editor::Properties::replaceNumberedBrackets(const std::string& input) {
    std::string result = input;
    size_t pos = 0;

    while ((pos = result.find('[', pos)) != std::string::npos) {
        size_t end_pos = result.find(']', pos);
        if (end_pos != std::string::npos && std::isdigit(result[pos + 1])) {
            result.replace(pos, end_pos - pos + 1, "[]");
            pos += 2;
        } else {
            ++pos;
        }
    }

    return result;
}

Vector3 editor::Properties::roundZero(const Vector3& val, const float threshold) const{
    return Vector3(
        (fabs(val.x) < threshold) ? 0.0f : val.x,
        (fabs(val.y) < threshold) ? 0.0f : val.y,
        (fabs(val.z) < threshold) ? 0.0f : val.z
    );
}

float editor::Properties::snapDisplayedAngle(float angle, float threshold) const{
    float rounded = std::round(angle);
    return (std::fabs(angle - rounded) <= threshold) ? rounded : angle;
}

Vector3 editor::Properties::getDisplayedEulerAngles(const Quaternion& value, const RotationOrder& order, float zeroThreshold) const{
    constexpr float displaySnapThreshold = 0.05f;

    Vector3 euler = roundZero(Quaternion(value).normalize().getEulerAngles(order), zeroThreshold);
    euler.x = snapDisplayedAngle(euler.x, displaySnapThreshold);
    euler.y = snapDisplayedAngle(euler.y, displaySnapThreshold);
    euler.z = snapDisplayedAngle(euler.z, displaySnapThreshold);

    return euler;
}

bool editor::Properties::compareVectorFloat(const float* a, const float* b, size_t elements, const float threshold){
    for (size_t i = 0; i < elements; ++i) {
        if (fabs(a[i] - b[i]) > threshold) {
            return true;
        }
    }
    return false;
}

float editor::Properties::getLabelSize(std::string label, bool addRotateIconSpace){
    float iconSize = ImGui::CalcTextSize(ICON_FA_ROTATE_LEFT).x;
    float labelSize = ImGui::CalcTextSize(label.c_str()).x;
    return labelSize + (addRotateIconSpace ? iconSize : iconSize / 2.0f);
}

void editor::Properties::helpMarker(std::string desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

Texture* editor::Properties::findThumbnail(const std::string& path) {
    if (path.empty()) return nullptr;

    std::filesystem::path texPath = project->resolveAssetPath(path);

    if (!texPath.is_absolute()) return nullptr;

    std::error_code ec;
    if (!std::filesystem::exists(texPath, ec) || ec) {
        return nullptr;
    }

    const std::filesystem::path thumbnailPath = project->getThumbnailPath(texPath);
    const std::string thumbPathStr = thumbnailPath.string();

    // Fast path: return from cache if already loaded
    auto thumbIt = thumbnailTextures.find(thumbPathStr);
    if (thumbIt != thumbnailTextures.end() && !thumbIt->second.empty()) {
        return &thumbIt->second;
    }

    std::error_code thumbEc;
    const bool thumbnailExists = std::filesystem::exists(thumbnailPath, thumbEc) && !thumbEc;

    if (thumbnailExists) {
        TextureData thumbData(thumbnailPath.string().c_str());
        if (thumbData.getData() && thumbData.getSize() > 0) {
            Texture thumbTexture(thumbPathStr, thumbData);
            thumbnailTextures[thumbPathStr] = thumbTexture;
            return &thumbnailTextures[thumbPathStr];
        }
    }

    // Thumbnail missing or failed to load — request generation for all file types
    if (ResourcesWindow* resourcesWindow = Backend::getApp().getResourcesWindow()) {
        resourcesWindow->requestThumbnailGeneration(texPath, thumbnailExists);
    }
    return nullptr;
}

void editor::Properties::drawImageWithBorderAndRounding(Texture* texture, const ImVec2& size, float rounding, ImU32 border_col, float border_thickness, bool flipY) {
    if (!texture) return;

    ImTextureID tex_id = Backend::getImGuiTexture(texture->getRender());
    int texWidth = texture->getWidth();
    int texHeight = texture->getHeight();

    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Calculate source aspect and target aspect
    float srcAspect = static_cast<float>(texWidth) / texHeight;
    float dstAspect = size.x / size.y;

    // Default UVs (full image)
    ImVec2 uv0(0, 0);
    ImVec2 uv1(1, 1);

    // If aspect ratios differ, calculate the crop
    if (fabs(srcAspect - dstAspect) > 1e-3f) {
        if (srcAspect > dstAspect) {
            // Source is wider; crop left and right
            float newWidth = texHeight * dstAspect;
            float x0 = (texWidth - newWidth) / 2.0f;
            uv0.x = x0 / texWidth;
            uv1.x = (x0 + newWidth) / texWidth;
        } else {
            // Source is taller; crop top and bottom
            float newHeight = texWidth / dstAspect;
            float y0 = (texHeight - newHeight) / 2.0f;
            uv0.y = y0 / texHeight;
            uv1.y = (y0 + newHeight) / texHeight;
        }
    }

    if (flipY) {
        float temp = uv0.y;
        uv0.y = uv1.y;
        uv1.y = temp;
    }

    ImVec2 p_min = cursor;
    ImVec2 p_max = ImVec2(cursor.x + size.x, cursor.y + size.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Draw the cropped image with rounding
    Widgets::addImageRounded(draw_list, tex_id, p_min, p_max, uv0, uv1, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);

    // Draw the border
    draw_list->AddRect(p_min, p_max, border_col, rounding, ImDrawFlags_RoundCornersAll, border_thickness);

    // Reserve space for interaction
    ImGui::InvisibleButton("##image", size);
}


void editor::Properties::dragDropResourcesFont(ComponentType cpType, std::string id, size_t slot, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    if (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) {
        return;
    }

    const std::string slotId = id + "#" + std::to_string(slot);

    auto markTextDirty = [sceneProject, componentType](Entity entity){
        if (componentType == ComponentType::TextComponent){
            sceneProject->scene->getComponent<TextComponent>(entity).needReloadAtlas = true;
            sceneProject->scene->getComponent<TextComponent>(entity).needUpdateText = true;
        }
    };

    if (ImGui::BeginDragDropTarget()){

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (receivedStrings.size() > 0){
                const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                const std::string droppedRelativePath = droppedAssetPath(project, receivedStrings[0], insideAssets);

                bool isFont = Util::isFontFile(droppedRelativePath);

                if (isFont) {
                    if (!hasFontDrag.count(slotId)){
                        hasFontDrag[slotId] = true;
                        for (Entity& entity : entities){
                            FontArray* valueRef = Catalog::getPropertyRef<FontArray>(sceneProject->scene, entity, cpType, id);
                            if (!valueRef)
                                continue;
                            originalFont[slotId][entity] = *valueRef;
                            if ((*valueRef)[slot] != droppedRelativePath){
                                (*valueRef)[slot] = droppedRelativePath;
                                markTextDirty(entity);
                            }
                        }
                    }
                    if (payload->IsDelivery()){
                        for (Entity& entity : entities){
                            FontArray* valueRef = Catalog::getPropertyRef<FontArray>(sceneProject->scene, entity, cpType, id);
                            if (!valueRef)
                                continue;
                            *valueRef = originalFont[slotId][entity];
                            if (!insideAssets){
                                markTextDirty(entity);
                                continue;
                            }
                            FontArray newValue = originalFont[slotId][entity];
                            newValue[slot] = droppedRelativePath;
                            cmd = new PropertyCmd<FontArray>(project, sceneProject->id, entity, cpType, id, newValue);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        if (!insideAssets){
                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasFontDrag.erase(slotId);
                        originalFont.erase(slotId);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasFontDrag.count(slotId) && hasFontDrag[slotId]){
            for (Entity& entity : entities){
                FontArray* valueRef = Catalog::getPropertyRef<FontArray>(sceneProject->scene, entity, cpType, id);
                if (!valueRef)
                    continue;
                if (*valueRef != originalFont[slotId][entity]){
                    *valueRef = originalFont[slotId][entity];
                    markTextDirty(entity);
                }
            }

            hasFontDrag.erase(slotId);
            originalFont.erase(slotId);
        }
    }
}

// The Catalog property is the whole array, so a slot edit commits a modified copy of it
void editor::Properties::setFontSlot(ComponentType cpType, const std::string& id, size_t slot, const std::string& path, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged){
    for (Entity& entity : entities){
        FontArray* valueRef = Catalog::getPropertyRef<FontArray>(sceneProject->scene, entity, cpType, id);
        if (!valueRef || (*valueRef)[slot] == path)
            continue;

        FontArray newValue = *valueRef;
        newValue[slot] = path;

        cmd = new PropertyCmd<FontArray>(project, sceneProject->id, entity, cpType, id, newValue, onValueChanged);
        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
        finishProperty = true;
    }
}

void editor::Properties::drawFontSlot(ComponentType cpType, const std::string& id, size_t slot, bool dif, const FontArray& value, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged, float width, const std::function<void(const ImVec2&)>& extraButton){
    ImGui::BeginGroup();
    ImGui::PushID(("font_" + id + "_" + std::to_string(slot)).c_str());

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);

    // reserve room for the action buttons, with one gap between each item and a trailing one
    ImVec2 fontButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
    int fontButtonCount = extraButton ? 3 : 2;
    float fontButtonsReserve = fontButtonSize.x * fontButtonCount + ImGui::GetStyle().ItemSpacing.x * (fontButtonCount + 1);

    // a fill width cannot be measured by an auto sized window, those pass an explicit one
    float frameWidth = (width > 0.0f) ? std::max(1.0f, width - fontButtonsReserve) : -fontButtonsReserve;

    ImGui::BeginChild("fontframe", ImVec2(frameWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2),
        false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    std::string fontName = value[slot];
    std::error_code ec;
    if (std::filesystem::exists(fontName, ec)) {
        fontName = std::filesystem::path(fontName).filename().string();
    }
    if (fontName.empty()) {
        fontName = (slot == 0) ? "< Default >" : "< Not set >";
    }

    float textWidth = ImGui::CalcTextSize(fontName.c_str()).x;
    float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(availWidth - textWidth - 2);
    ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
    if (dif)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::Text("%s", fontName.c_str());
    if (dif)
        ImGui::PopStyleColor();

    ImGui::EndChild();
    if (!value[slot].empty()){
        ImGui::SetItemTooltip("%s", value[slot].c_str());
    }

    ImGui::PopStyleColor();

    ImGui::SameLine();

    // no horizontal frame padding so the icons fit centered in the square buttons
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));

    if (ImGui::Button(ICON_FA_FOLDER_OPEN, fontButtonSize)) {
        std::string path = editor::FileDialogs::openFileDialog(project->getAssetsPath().string(), FILE_DIALOG_FONT);
        if (!path.empty()) {
            std::filesystem::path assetsPath = project->getAssetsPath();
            std::filesystem::path filePath = std::filesystem::absolute(path);

            // References are stored relative to the assets root
            auto relative = std::filesystem::relative(filePath, assetsPath, ec);
            if (ec || relative.empty() || *relative.begin() == "..") {
                ImGui::OpenPopup("File Import Error");
            }else{
                setFontSlot(cpType, id, slot, relative.string(), sceneProject, entities, onValueChanged);
            }
        }
    }
    ImGui::SetItemTooltip("Font file");

    ImGui::SameLine();

    ImGui::BeginDisabled(value[slot].empty());
    if (ImGui::Button(ICON_FA_XMARK, fontButtonSize)) {
        setFontSlot(cpType, id, slot, "", sceneProject, entities, onValueChanged);
    }
    ImGui::EndDisabled();
    if (!value[slot].empty()){
        ImGui::SetItemTooltip(slot == 0 ? "Use the built-in font" : "Clear this font");
    }

    if (extraButton){
        ImGui::SameLine();
        extraButton(fontButtonSize);
    }

    ImGui::PopStyleVar();

    // Error popup modal
    if (ImGui::BeginPopupModal("File Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Selected file must be within the assets directory.");
        ImGui::Separator();

        float buttonWidth = 120;
        float windowWidth = ImGui::GetWindowSize().x;
        ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PopID();
    ImGui::EndGroup();

    dragDropResourcesFont(cpType, id, slot, sceneProject, entities, cpType);
}

void editor::Properties::drawFontFallbacksPopup(const char* popupId, ComponentType cpType, const std::string& id, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged){
    const float slotWidth = 16 * ImGui::GetFontSize();
    const float labelWidth = getLabelSize("Fallback 1");

    ImGui::SetNextWindowSizeConstraints(ImVec2(labelWidth + slotWidth + ImGui::GetStyle().WindowPadding.x * 2 + ImGui::GetStyle().CellPadding.x * 2, 0), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopup(popupId))
        return;

    // first selected entity is the representative state, like the texture settings popup
    PropertyData fontProp = Catalog::getProperty(sceneProject->scene, entities.front(), cpType, id);
    FontArray* current = static_cast<FontArray*>(fontProp.ref);
    if (!current){
        ImGui::EndPopup();
        return;
    }
    const FontArray defaults = fontProp.def ? *static_cast<FontArray*>(fontProp.def) : FontArray();

    ImGui::Text("Additional fonts");
    ImGui::SameLine();
    helpMarker("Used, in order, for the characters the main font does not have.");
    ImGui::Separator();

    beginTable(cpType, labelWidth, "fontfallbacks_" + id);

    for (size_t slot = 1; slot < current->size(); slot++){
        bool dif = false;
        for (Entity& entity : entities){
            FontArray* valueRef = Catalog::getPropertyRef<FontArray>(sceneProject->scene, entity, cpType, id);
            if (valueRef && (*valueRef)[slot] != (*current)[slot])
                dif = true;
        }

        std::string label = "Fallback " + std::to_string(slot);
        if (propertyHeader(label, slotWidth, (*current)[slot] != defaults[slot], false)){
            setFontSlot(cpType, id, slot, defaults[slot], sceneProject, entities, onValueChanged);
        }

        drawFontSlot(cpType, id, slot, dif, *current, sceneProject, entities, onValueChanged, slotWidth);
    }

    endTable();

    ImGui::EndPopup();
}


void editor::Properties::applyCameraTexture(Entity cameraEntity, ComponentType cpType, const std::string& id, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged){
    Scene* scene = sceneProject->scene;
    CameraComponent* camera = scene->findComponent<CameraComponent>(cameraEntity);
    if (!camera)
        return;

    Texture texture = CameraTextureLink::make(scene, cameraEntity);

    auto multiCmd = new MultiPropertyCmd();
    if (!camera->renderToTexture) {
        multiCmd->addPropertyCmd<bool>(project, sceneProject->id, cameraEntity, ComponentType::CameraComponent, "renderToTexture", true);
    }
    if (sceneProject->mainCamera == cameraEntity) {
        // a render-to-texture camera cannot be the scene main camera
        multiCmd->addCommand(std::make_unique<SetMainCameraCmd>(project, sceneProject->id, NULL_ENTITY));
    }
    for (Entity& entity : entities){
        multiCmd->addPropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, onValueChanged);
    }
    cmd = multiCmd;
    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
    cmd->setNoMerge();
}

// Popup that lets the user configure the sampler settings (min/mag filter, U/V wrap), the
// alpha border fix and the SVG rasterization scale for vector sources of a texture
// property. Laid out with the project's standard "<label> | <input>" property table,
// including the rotate-left reset button when a row differs from the Catalog default. Each
// change commits a PropertyCmd that copies the current per-entity Texture and mutates only
// the chosen field.
void editor::Properties::drawTextureSettingsPopup(const char* popupId, ComponentType cpType, const std::string& id, SceneProject* sceneProject, std::vector<Entity>& entities, std::function<void()> onValueChanged){
    ImGui::SetNextWindowSizeConstraints(ImVec2(19 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopup(popupId))
        return;

    // Applies mutator to a copy of each selected entity's texture and commits a PropertyCmd
    // per entity whose texture actually changed. The mutator returns false to skip an entity.
    auto applyToTextures = [&](const std::function<bool(Texture&)>& mutator){
        for (Entity& entity : entities){
            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
            if (!valueRef)
                continue;

            Texture modified = *valueRef;
            if (!mutator(modified) || modified == *valueRef)
                continue;

            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, modified, onValueChanged);
            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
        }
        finishProperty = true;
    };

    // Renders a "<label> | <combo>" property row for an enum-backed sampler field, matching
    // propertyRow's Enum layout, including the rotate-left reset button when the value
    // differs from the default. propertyRow itself can't be used here: it resolves ids
    // through the Catalog and commits PropertyCmd<int>, while these fields live inside the
    // Texture value (private members, not Catalog properties), so the whole Texture must be
    // committed instead. Returns true and sets outValue when the selection changes; a reset
    // click reports the default as the new selection.
    auto comboRow = [this](const char* label, const char* comboId, const std::vector<editor::EnumEntry>& entries, int currentValue, int defaultValue, int& outValue) -> bool {
        int item = 0;
        for (size_t i = 0; i < entries.size(); ++i){
            if (entries[i].value == currentValue){
                item = (int)i;
                break;
            }
        }
        std::vector<const char*> names;
        for (const auto& entry : entries)
            names.push_back(entry.name);

        bool reset = propertyHeader(label, 12 * ImGui::GetFontSize(), currentValue != defaultValue, false);
        if (ImGui::Combo(comboId, &item, names.data(), (int)names.size())){
            outValue = entries[item].value;
            return true;
        }
        if (reset){
            outValue = defaultValue;
            return true;
        }
        return false;
    };

    // Read current values from the first selected entity as the representative state; the
    // Catalog default texture provides the per-field reset targets.
    PropertyData textureProp = Catalog::getProperty(sceneProject->scene, entities.front(), cpType, id);
    Texture* current = static_cast<Texture*>(textureProp.ref);
    if (!current){
        ImGui::EndPopup();
        return;
    }
    const Texture defaults = textureProp.def ? *static_cast<Texture*>(textureProp.def) : Texture();

    // SVG rasterization scale is only relevant for vector sources.
    bool isSvg = TextureData::hasSvgExtension(current->getPath().c_str());
    float svgCurrentScale = current->getSvgScale();

    // setSvgScale re-keys the pool entry (the scale is part of the texture identity) while
    // the sampler fields stay untouched, so a scale change never resets filter/wrap.
    auto applyScale = [&](float scale){
        applyToTextures([scale](Texture& t){
            if (!TextureData::hasSvgExtension(t.getPath().c_str()))
                return false;
            t.setSvgScale(scale);
            return true;
        });
    };

    ImGui::Text("Texture settings");
    ImGui::Separator();

    beginTable(cpType, getLabelSize("Alpha Border"), "texsettings_" + id);

    struct SamplerRow {
        const char* label;
        const char* comboId;
        const std::vector<editor::EnumEntry>* entries;
        int current;
        int def;
        void (*set)(Texture&, int);
    };
    const SamplerRow samplerRows[] = {
        {"Min Filter", "##texset_min", &entriesTextureMinFilter, (int)current->getMinFilter(), (int)defaults.getMinFilter(), [](Texture& t, int v){ t.setMinFilter((TextureFilter)v); }},
        {"Mag Filter", "##texset_mag", &entriesTextureFilter, (int)current->getMagFilter(), (int)defaults.getMagFilter(), [](Texture& t, int v){ t.setMagFilter((TextureFilter)v); }},
        {"Wrap U", "##texset_wrapu", &entriesTextureWrap, (int)current->getWrapU(), (int)defaults.getWrapU(), [](Texture& t, int v){ t.setWrapU((TextureWrap)v); }},
        {"Wrap V", "##texset_wrapv", &entriesTextureWrap, (int)current->getWrapV(), (int)defaults.getWrapV(), [](Texture& t, int v){ t.setWrapV((TextureWrap)v); }},
        {"Alpha Border", "##texset_alphaborder", &entriesTextureAlphaBorder, (int)current->getAlphaBorder(), (int)defaults.getAlphaBorder(), [](Texture& t, int v){ t.setAlphaBorder((TextureAlphaBorder)v); }},
    };
    for (const SamplerRow& row : samplerRows){
        int chosen = 0;
        if (comboRow(row.label, row.comboId, *row.entries, row.current, row.def, chosen)){
            applyToTextures([&](Texture& t){ row.set(t, chosen); return true; });
        }
    }

    endTable();

    if (isSvg){
        //ImGui::SeparatorText("SVG");

        beginTable(cpType, getLabelSize("Alpha Border"), "texsettings_svg_" + id);

        // "SVG Scale" | drag field; commit only on release so each distinct scale rasterizes
        // a new texture at most once.
        static std::map<std::string, float> svgScaleEditing;
        const std::string editKey = std::to_string(static_cast<int>(cpType)) + ":" + id;
        auto editIt = svgScaleEditing.find(editKey);
        float editScale = (editIt != svgScaleEditing.end()) ? editIt->second : svgCurrentScale;

        float svgDefaultScale = defaults.getSvgScale();

        bool resetScale = propertyHeader("SVG Scale", 6 * ImGui::GetFontSize(), std::fabs(svgCurrentScale - svgDefaultScale) > 1e-4f, false);
        if (ImGui::DragFloat("##texset_svgscale", &editScale, 0.05f, 0.1f, 16.0f, "%.2fx")) {
            svgScaleEditing[editKey] = editScale;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            svgScaleEditing.erase(editKey);
            applyScale(editScale);
        }
        if (resetScale) {
            svgScaleEditing.erase(editKey);
            applyScale(svgDefaultScale);
        }

        // "Presets" | quick-pick scale buttons (the active scale is highlighted)
        propertyHeader("Presets", -1, false, false);
        struct ScalePreset { const char* label; float value; };
        static const ScalePreset presets[] = {
            {"0.5x", 0.5f}, {"1x", 1.0f}, {"2x", 2.0f}, {"3x", 3.0f}, {"4x", 4.0f}
        };
        for (int i = 0; i < IM_ARRAYSIZE(presets); ++i){
            if (i > 0)
                ImGui::SameLine();
            bool selected = std::fabs(svgCurrentScale - presets[i].value) < 1e-4f;
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 1.4, ImGui::GetStyle().FramePadding.y));
            if (ImGui::SmallButton(presets[i].label)) {
                applyScale(presets[i].value);
            }
            ImGui::PopStyleVar(1);
            if (selected)
                ImGui::PopStyleColor();
        }

        endTable();
    }

    ImGui::EndPopup();
}

void editor::Properties::dragDropResourcesTexture(ComponentType cpType, std::string id, SceneProject* sceneProject, std::vector<Entity> entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    if (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) {
        return;
    }

    if (ImGui::BeginDragDropTarget()){

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (receivedStrings.size() > 0){
                const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                const std::string droppedRelativePath = droppedAssetPath(project, receivedStrings[0], insideAssets);

                if (Util::isImageFile(droppedRelativePath)) {
                    if (!hasTextureDrag.count(id)){
                        hasTextureDrag[id] = true;
                        for (Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            originalTex[id][entity] = Texture(*valueRef);
                            if (*valueRef != Texture(droppedRelativePath)){
                                *valueRef = Texture(droppedRelativePath);
                                if (componentType == ComponentType::MeshComponent){
                                    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                                    for (unsigned int i = 0; i < numSubmeshes; i++){
                                        sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                                    }
                                    Catalog::updateSpriteBatch(sceneProject->scene, entity);
                                }
                                if (componentType == ComponentType::UIComponent){
                                    sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                                }
                                if (componentType == ComponentType::WaterComponent){
                                    sceneProject->scene->getComponent<WaterComponent>(entity).needUpdateTexture = true;
                                }
                                //printf("needUpdateTexture %s\n", name.c_str());
                            }
                        }
                    }
                    if (payload->IsDelivery()){
                        Texture texture(droppedRelativePath);
                        for (Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalTex[id][entity];
                            if (!insideAssets){
                                markTextureUpdate(sceneProject->scene, entity, componentType);
                                continue;
                            }
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        if (!insideAssets){
                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasTextureDrag.erase(id);
                        originalTex.erase(id);
                    }
                }
            }
        }

        // camera entity dragged from the structure window (render to texture)
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity")) {
            const EntityPayload* entityPayload = reinterpret_cast<const EntityPayload*>(payload->Data);
            bool sameScene = (entityPayload->entitySceneId == 0 || entityPayload->entitySceneId == sceneProject->id);
            if (sameScene && sceneProject->scene->findComponent<CameraComponent>(entityPayload->entity)) {
                applyCameraTexture(entityPayload->entity, cpType, id, sceneProject, entities, nullptr);
                finishProperty = true;
                ImGui::SetWindowFocus(Properties::WINDOW_NAME);
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasTextureDrag.count(id) && hasTextureDrag[id]){
            for (Entity& entity : entities){
                Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalTex[id][entity]){
                    *valueRef = originalTex[id][entity];
                    if (componentType == ComponentType::MeshComponent){
                        unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                        for (unsigned int i = 0; i < numSubmeshes; i++){
                            sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                        }
                        Catalog::updateSpriteBatch(sceneProject->scene, entity);
                    }
                    if (componentType == ComponentType::UIComponent){
                        sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                    }
                    if (componentType == ComponentType::WaterComponent){
                        sceneProject->scene->getComponent<WaterComponent>(entity).needUpdateTexture = true;
                    }
                    //printf("needUpdateTexture %s\n", id.c_str());
                }
            }

            hasTextureDrag.erase(id);
            originalTex.erase(id);
        }
    }
}

void editor::Properties::dragDropResourcesTextureCubeFace(ComponentType cpType, const std::string& id, size_t faceIndex, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    const std::string dragId = id + "##cube_face_" + std::to_string(faceIndex);

    const ImRect rect(rectMin, rectMax);
    if (ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(dragId.c_str()))){
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (!receivedStrings.empty()){
                const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                const std::string droppedRelativePath = droppedAssetPath(project, receivedStrings[0], insideAssets);

                if (Util::isImageFile(droppedRelativePath)) {
                    if (!hasTextureDrag.count(dragId)){
                        hasTextureDrag[dragId] = true;
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            originalTex[dragId][entity] = Texture(*valueRef);

                            Texture updated = Texture(*valueRef);
                            updated.setCubePath(faceIndex, droppedRelativePath);
                            if (*valueRef != updated){
                                *valueRef = updated;
                                if (componentType == ComponentType::MeshComponent){
                                    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                                    for (unsigned int i = 0; i < numSubmeshes; i++){
                                        sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                                    }
                                    Catalog::updateSpriteBatch(sceneProject->scene, entity);
                                }
                                if (componentType == ComponentType::UIComponent){
                                    sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                                }
                                if (componentType == ComponentType::SkyComponent){
                                    sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                                }
                            }
                        }
                    }

                    if (payload->IsDelivery()){
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalTex[dragId][entity];
                            if (!insideAssets){
                                markTextureUpdate(sceneProject->scene, entity, componentType);
                                continue;
                            }

                            Texture updated = Texture(*valueRef);
                            updated.setCubePath(faceIndex, droppedRelativePath);
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, updated);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        if (!insideAssets){
                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasTextureDrag.erase(dragId);
                        originalTex.erase(dragId);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasTextureDrag.count(dragId) && hasTextureDrag[dragId]){
            for (const Entity& entity : entities){
                Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalTex[dragId][entity]){
                    *valueRef = originalTex[dragId][entity];
                    if (componentType == ComponentType::MeshComponent){
                        unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                        for (unsigned int i = 0; i < numSubmeshes; i++){
                            sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                        }
                        Catalog::updateSpriteBatch(sceneProject->scene, entity);
                    }
                    if (componentType == ComponentType::UIComponent){
                        sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                    }
                    if (componentType == ComponentType::SkyComponent){
                        sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                    }
                }
            }

            hasTextureDrag.erase(dragId);
            originalTex.erase(dragId);
        }
    }
}

void editor::Properties::dragDropResourcesTextureCubeSingleFile(ComponentType cpType, const std::string& id, const ImVec2& rectMin, const ImVec2& rectMax, SceneProject* sceneProject, const std::vector<Entity>& entities, ComponentType componentType){
    // Block DnD while playing for non-script components
    if (sceneProject && sceneProject->playState != ScenePlayState::STOPPED) {
        return;
    }

    const std::string dragId = id + "##cube_single";

    const ImRect rect(rectMin, rectMax);
    if (ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(dragId.c_str()))){
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (!receivedStrings.empty()){
                const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                const std::string droppedRelativePath = droppedAssetPath(project, receivedStrings[0], insideAssets);

                if (Util::isImageFile(droppedRelativePath)) {
                    if (!hasTextureDrag.count(dragId)){
                        hasTextureDrag[dragId] = true;
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            originalTex[dragId][entity] = Texture(*valueRef);

                            Texture updated = Texture(*valueRef);
                            updated.setCubeMap(droppedRelativePath);
                            if (*valueRef != updated){
                                *valueRef = updated;
                                if (componentType == ComponentType::MeshComponent){
                                    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                                    for (unsigned int i = 0; i < numSubmeshes; i++){
                                        sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                                    }
                                    Catalog::updateSpriteBatch(sceneProject->scene, entity);
                                }
                                if (componentType == ComponentType::UIComponent){
                                    sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                                }
                                if (componentType == ComponentType::SkyComponent){
                                    sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                                }
                            }
                        }
                    }

                    if (payload->IsDelivery()){
                        for (const Entity& entity : entities){
                            Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                            *valueRef = originalTex[dragId][entity];
                            if (!insideAssets){
                                markTextureUpdate(sceneProject->scene, entity, componentType);
                                continue;
                            }

                            Texture updated = Texture(*valueRef);
                            updated.setCubeMap(droppedRelativePath);
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, updated);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                                finishProperty = true;
                            }
                        }

                        if (!insideAssets){
                            Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                        }

                        ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        hasTextureDrag.erase(dragId);
                        originalTex.erase(dragId);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }else{
        if (hasTextureDrag.count(dragId) && hasTextureDrag[dragId]){
            for (const Entity& entity : entities){
                Texture* valueRef = Catalog::getPropertyRef<Texture>(sceneProject->scene, entity, cpType, id);
                if (*valueRef != originalTex[dragId][entity]){
                    *valueRef = originalTex[dragId][entity];
                    if (componentType == ComponentType::MeshComponent){
                        unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes;
                        for (unsigned int i = 0; i < numSubmeshes; i++){
                            sceneProject->scene->getComponent<MeshComponent>(entity).submeshes[i].needUpdateTexture = true;
                        }
                        Catalog::updateSpriteBatch(sceneProject->scene, entity);
                    }
                    if (componentType == ComponentType::UIComponent){
                        sceneProject->scene->getComponent<UIComponent>(entity).needUpdateTexture = true;
                    }
                    if (componentType == ComponentType::SkyComponent){
                        sceneProject->scene->getComponent<SkyComponent>(entity).needUpdateTexture = true;
                    }
                }
            }

            hasTextureDrag.erase(dragId);
            originalTex.erase(dragId);
        }
    }
}

void editor::Properties::handleComponentMenu(SceneProject* sceneProject, std::vector<Entity> entities, ComponentType cpType, bool isBundle, bool isBundleOverridden, bool& headerOpen, bool readOnly) {
    if (ImGui::BeginPopupContextItem(("component_options_menu_" + std::to_string(static_cast<int>(cpType))).c_str())) {
        ImGui::TextDisabled("Component options");
        ImGui::Separator();

        ImGui::BeginDisabled(readOnly); // disable all actions while playing

        if (isBundle){
            if (isBundleOverridden) {
                if (ImGui::MenuItem(ICON_FA_CUBE " Revert to Bundle")) {
                    for (Entity& entity : entities){
                        cmd = new ComponentToBundleSharedCmd(project, sceneProject->id, entity, cpType);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    cmd->setNoMerge();
                }

            } else {
                if (ImGui::MenuItem(ICON_FA_LOCK_OPEN " Make Unique")) {
                    for (Entity& entity : entities){
                        cmd = new ComponentToBundleLocalCmd(project, sceneProject->id, entity, cpType);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    cmd->setNoMerge();
                }
            }
        }

        bool canRemove = !(cpType == ComponentType::Transform && isBundle);
        if (ImGui::MenuItem(ICON_FA_TRASH " Remove", nullptr, false, canRemove)) {
            for (Entity& entity : entities){
                cmd = new RemoveComponentCmd(project, sceneProject->id, entity, cpType);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            cmd->setNoMerge();

            headerOpen = false;
        }

        ImGui::EndDisabled();

        ImGui::EndPopup();
    }
}

bool editor::Properties::canAddComponent(SceneProject* sceneProject, Entity entity, ComponentType cpType) {
    // Check if entity already has this component
    std::vector<ComponentType> existingComponents = Catalog::findComponents(sceneProject->scene, entity);
    return std::find(existingComponents.begin(), existingComponents.end(), cpType) == existingComponents.end();
}

Texture editor::Properties::getMaterialPreview(const Material& material, const std::string id, bool receiveIBL){
    MaterialRender& materialRender = materialRenders[id];

    auto texPending = [](const Texture& t) {
        if (t.isFramebuffer() || CameraTextureLink::isCameraTexture(t))
            return false; // framebuffer textures never enter the pool
        std::string tid = t.getRenderId();
        return !tid.empty() && !TexturePool::get(tid);
    };

    bool pending = texPending(material.baseColorTexture) || texPending(material.emissiveTexture) ||
                   texPending(material.metallicRoughnessTexture) || texPending(material.occlusionTexture) ||
                   texPending(material.normalTexture);

    if ((materialRender.getMaterial() != material) || materialRender.getReceiveIBL() != receiveIBL ||
        !materialRender.getFramebuffer()->isCreated() || pending){
        materialRender.setReceiveIBL(receiveIBL);
        materialRender.applyMaterial(material);
        Engine::executeSceneOnce(materialRender.getScene());
    }

    usedPreviewIds.insert(id);

    return materialRender.getTexture();
}

Texture editor::Properties::getDirectionPreview(const Vector3& direction, const std::string id){
    DirectionRender& directionRender = directionRenders[id];

    if ((directionRender.getDirection() != direction) || !directionRender.getFramebuffer()->isCreated()){
        directionRender.setDirection(direction);
        Engine::executeSceneOnce(directionRender.getScene());
    }

    usedPreviewIds.insert(id);

    return directionRender.getTexture();
}

bool editor::Properties::drawSpriteFramePreview(Texture* texture, const Rect& rect, const ImVec2& size, const char* itemId){
    if (!texture || texture->empty() || !texture->getRender()) {
        return false;
    }

    const float texWidth = static_cast<float>(texture->getWidth());
    const float texHeight = static_cast<float>(texture->getHeight());
    const float rectWidth = rect.getWidth();
    const float rectHeight = rect.getHeight();

    if (texWidth <= 0.0f || texHeight <= 0.0f || rectWidth <= 0.0f || rectHeight <= 0.0f) {
        return false;
    }

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImVec2 p_min = cursor;
    ImVec2 p_max = ImVec2(cursor.x + size.x, cursor.y + size.y);

    float srcAspect = rectWidth / rectHeight;
    float dstAspect = size.x / size.y;

    float drawWidth = size.x;
    float drawHeight = size.y;
    if (fabs(srcAspect - dstAspect) > 1e-3f) {
        if (srcAspect > dstAspect) {
            drawHeight = size.x / srcAspect;
        } else {
            drawWidth = size.y * srcAspect;
        }
    }

    ImVec2 imageMin(cursor.x + (size.x - drawWidth) * 0.5f, cursor.y + (size.y - drawHeight) * 0.5f);
    ImVec2 imageMax(imageMin.x + drawWidth, imageMin.y + drawHeight);

    ImVec2 uv0(rect.getX() / texWidth, rect.getY() / texHeight);
    ImVec2 uv1((rect.getX() + rectWidth) / texWidth, (rect.getY() + rectHeight) / texHeight);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float rounding = ImGui::GetStyle().FrameRounding;
    ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

    drawList->AddRectFilled(p_min, p_max, ImGui::GetColorU32(Theme::Colors::filenameLabel), rounding, ImDrawFlags_RoundCornersAll);
    Widgets::addImageRounded(drawList, Backend::getImGuiTexture(texture->getRender()), imageMin, imageMax, uv0, uv1, IM_COL32_WHITE, rounding, ImDrawFlags_RoundCornersAll);
    drawList->AddRect(p_min, p_max, borderColor, rounding, ImDrawFlags_RoundCornersAll, Theme::dpi(1.0f));

    ImGui::InvisibleButton(itemId, size);
    return true;
}

void editor::Properties::updateShapePreview(const ShapeParameters& shapeParams){
    ImVec4 frameBgColor = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    std::shared_ptr<doriax::MeshSystem> meshSys = shapePreviewRender.getScene()->getSystem<MeshSystem>();

    MeshComponent meshComp;

    updateMeshShape(meshComp, meshSys.get(), shapeParams);

    shapePreviewRender.applyMesh(Stream::encodeMeshComponent(meshComp), true, true);
    shapePreviewRender.setBackground(Vector4(frameBgColor.x, frameBgColor.y, frameBgColor.z, frameBgColor.w));

    Engine::executeSceneOnce(shapePreviewRender.getScene());
}

void editor::Properties::updateMeshShape(MeshComponent& meshComp, MeshSystem* meshSys, const ShapeParameters& shapeParams){
    switch (shapeParams.geometryType) {
        case 0: // Plane
            meshSys->createPlane(meshComp, shapeParams.planeWidth, shapeParams.planeDepth, shapeParams.planeTiles);
            break;
        case 1: // Box
            meshSys->createBox(meshComp, shapeParams.boxWidth, shapeParams.boxHeight, shapeParams.boxDepth, shapeParams.boxTiles);
            break;
        case 2: // Sphere
            meshSys->createSphere(meshComp, shapeParams.sphereRadius, shapeParams.sphereSlices, shapeParams.sphereStacks);
            break;
        case 3: // Cylinder
            meshSys->createCylinder(meshComp, shapeParams.cylinderBaseRadius, shapeParams.cylinderTopRadius, shapeParams.cylinderHeight, shapeParams.cylinderSlices, shapeParams.cylinderStacks);
            break;
        case 4: // Capsule
            meshSys->createCapsule(meshComp, shapeParams.capsuleBaseRadius, shapeParams.capsuleTopRadius, shapeParams.capsuleHeight, shapeParams.capsuleSlices, shapeParams.capsuleStacks);
            break;
        case 5: // Torus
            meshSys->createTorus(meshComp, shapeParams.torusRadius, shapeParams.torusRingRadius, shapeParams.torusSides, shapeParams.torusRings);
            break;
        case 6: // Wall
            meshSys->createWall(meshComp, shapeParams.wallWidth, shapeParams.wallHeight, shapeParams.wallTiles);
            break;
    }
}

void editor::Properties::drawNinePatchesPreview(const ImageComponent& img, Texture* texture, Texture* thumbTexture, const ImVec2& size){
    float availWidth = ImGui::GetContentRegionAvail().x;

    // Calculate display size based on input parameter or default to texture size
    float displayWidth = (size.x > 0) ? size.x : thumbTexture->getWidth();
    float displayHeight = (size.y > 0) ? size.y : thumbTexture->getHeight();

    // If only one dimension is specified, maintain aspect ratio
    if (size.x > 0 && size.y <= 0) {
        float aspectRatio = (float)thumbTexture->getHeight() / thumbTexture->getWidth();
        displayHeight = displayWidth * aspectRatio;
    } else if (size.x <= 0 && size.y > 0) {
        float aspectRatio = (float)thumbTexture->getWidth() / thumbTexture->getHeight();
        displayWidth = displayHeight * aspectRatio;
    }

    // Calculate position to center the image
    float xPos = (availWidth - displayWidth) * 0.5f;
    // Set cursor position to create centering effect
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xPos);

    // Store the cursor position before drawing the image
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Draw the image with the calculated size
    Widgets::image(Backend::getImGuiTexture(thumbTexture->getRender()), ImVec2(displayWidth, displayHeight));

    // Get draw list for custom rendering
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Define line color
    ImU32 line_color = IM_COL32(255, 0, 0, 255); // Red color

    // Get the original texture and thumbnail dimensions
    float origWidth = static_cast<float>(texture->getWidth());
    float origHeight = static_cast<float>(texture->getHeight());
    float thumbWidth = static_cast<float>(thumbTexture->getWidth());
    float thumbHeight = static_cast<float>(thumbTexture->getHeight());

    // Calculate scale factors between original texture and display size
    float scaleX = displayWidth / origWidth;
    float scaleY = displayHeight / origHeight;

    // Scale the margin values by the appropriate scale factor
    float scaledLeftMargin = img.patchMarginLeft * scaleX;
    float scaledRightMargin = img.patchMarginRight * scaleX;
    float scaledTopMargin = img.patchMarginTop * scaleY;
    float scaledBottomMargin = img.patchMarginBottom * scaleY;

    // Draw left margin line
    float leftX = cursor.x + scaledLeftMargin;
    draw_list->AddLine(
        ImVec2(leftX, cursor.y),
        ImVec2(leftX, cursor.y + displayHeight),
        line_color, 1.0f
    );

    // Draw right margin line
    float rightX = cursor.x + displayWidth - scaledRightMargin;
    draw_list->AddLine(
        ImVec2(rightX, cursor.y),
        ImVec2(rightX, cursor.y + displayHeight),
        line_color, 1.0f
    );

    // Draw top margin line
    float topY = cursor.y + scaledTopMargin;
    draw_list->AddLine(
        ImVec2(cursor.x, topY),
        ImVec2(cursor.x + displayWidth, topY),
        line_color, 1.0f
    );

    // Draw bottom margin line
    float bottomY = cursor.y + displayHeight - scaledBottomMargin;
    draw_list->AddLine(
        ImVec2(cursor.x, bottomY),
        ImVec2(cursor.x + displayWidth, bottomY),
        line_color, 1.0f
    );
}

void editor::Properties::syncSubSelection(SceneProject* sceneProject, Entity entity, int tileIndex, int instanceIndex) {
    if (!sceneProject) {
        return;
    }

    project->setSelectedSceneForProperties(sceneProject->id);
    project->setSelectedEntity(sceneProject->id, entity);

    if (!sceneProject->sceneRender) {
        return;
    }

    sceneProject->sceneRender->clearTileSelection();
    sceneProject->sceneRender->clearInstanceSelection();

    if (tileIndex >= 0) {
        sceneProject->sceneRender->selectTile(entity, tileIndex);
    }
    if (instanceIndex >= 0) {
        sceneProject->sceneRender->selectInstance(entity, instanceIndex);
    }

    sceneProject->sceneRender->update(project->getSelectedEntities(sceneProject->id),
        project->getEntities(sceneProject->id), sceneProject->mainCamera, sceneProject->displaySettings);
}


void editor::Properties::beginTable(ComponentType cpType, float firstColSize, std::string nameAddon){
    ImGui::PushItemWidth(-1);
    if (!nameAddon.empty()){
        nameAddon = "_"+nameAddon;
    }
    ImGui::BeginTable(("table_"+Catalog::getComponentName(cpType)+nameAddon).c_str(), 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, firstColSize);
    ImGui::TableSetupColumn("Value");

}

void editor::Properties::endTable(){
    ImGui::EndTable();
}

bool editor::Properties::drawSummaryAddButton(const std::string& label, float trailingReservedWidth, bool enabled, const char* disabledTooltip) {
    ImGui::SameLine();
    float buttonWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingReservedWidth);

    if (!enabled) {
        ImGui::BeginDisabled();
    }

    bool clicked = ImGui::Button(label.c_str(), ImVec2(buttonWidth, 0));

    if (!enabled && disabledTooltip && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", disabledTooltip);
    }

    if (!enabled) {
        ImGui::EndDisabled();
    }

    return clicked;
}

editor::Properties::EntityPickerResult editor::Properties::drawEntityPickerPopup(
        const std::string& popupId, const Signature& filter,
        SceneProject* owningScene, bool includeRelatedScenes,
        Entity currentValue, uint32_t currentValueSceneId,
        const std::vector<Entity>& excludeEntities) {

    EntityPickerResult result;
    if (!ImGui::BeginPopup(popupId.c_str())) return result;

    ImGui::Text("Select Entity");
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##ep_search", entityPickerSearchBuffer, sizeof(entityPickerSearchBuffer));

    if (ImGui::BeginChild("##ep_list", ImVec2(300.0f, 220.0f), true)) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().ItemSpacing.y);

        std::string searchStr = entityPickerSearchBuffer;
        for (char& c : searchStr) c = (char)tolower((unsigned char)c);

        // Build list of (SceneProject*, storedSceneId) to display
        // storedSceneId = 0 for owning scene entities, actual scene ID for related scene entities
        std::vector<std::pair<SceneProject*, uint32_t>> scenesToShow;
        scenesToShow.emplace_back(owningScene, (uint32_t)0);

        if (includeRelatedScenes) {
            for (uint32_t sid : project->getRelatedScenes(owningScene->id)) {
                SceneProject* related = project->getScene(sid);
                if (sid != owningScene->id && related->scene)
                    scenesToShow.emplace_back(related, sid);
            }
        }

        bool anyVisible = false;
        for (auto& [sp, storedSceneId] : scenesToShow) {
            bool sceneMatches = false;
            if (!searchStr.empty() && storedSceneId != 0) {
                std::string slow = sp->name;
                for (char& c : slow) c = (char)tolower((unsigned char)c);
                sceneMatches = slow.find(searchStr) != std::string::npos;
            }

            for (Entity e : sp->entities) {
                if (!sp->scene->isEntityCreated(e)) continue;
                if (filter.any() && (sp->scene->getSignature(e) & filter) != filter) continue;
                if (storedSceneId == 0 &&
                    std::find(excludeEntities.begin(), excludeEntities.end(), e) != excludeEntities.end()) continue;

                std::string ename = sp->scene->getEntityName(e);
                if (ename.empty()) ename = "Entity " + std::to_string(e);

                if (!searchStr.empty() && !sceneMatches) {
                    std::string elow = ename;
                    for (char& c : elow) c = (char)tolower((unsigned char)c);
                    if (elow.find(searchStr) == std::string::npos) continue;
                }

                anyVisible = true;
                bool isSelected = (e == currentValue && storedSceneId == currentValueSceneId);
                std::string eicon = Structure::getObjectIcon(sp->scene->getSignature(e), sp->scene);
                std::string selId = "##ep_ent_" + std::to_string(storedSceneId) + "_" + std::to_string(e);

                if (ImGui::Selectable(selId.c_str(), isSelected)) {
                    result.chosen = true;
                    result.entity = e;
                    result.sceneId = storedSceneId;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (storedSceneId != 0)
                    ImGui::Text("%s  %s  [%s]  (%u)", eicon.c_str(), ename.c_str(), sp->name.c_str(), static_cast<unsigned int>(e));
                else
                    ImGui::Text("%s  %s  (%u)", eicon.c_str(), ename.c_str(), static_cast<unsigned int>(e));
            }
        }

        if (!anyVisible) ImGui::TextDisabled("No entities found");
    }
    ImGui::EndChild();
    ImGui::EndPopup();

    return result;
}

bool editor::Properties::propertyHeader(std::string label, float secondColSize, bool defChanged, bool child){
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
    ImGui::TableNextRow();
    if (child){
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_PopupBg]));
    }
    ImGui::TableNextColumn();
    //ImGui::Dummy(ImVec2(0, 10));
    ImGui::Text("%s", label.c_str());
    ImGui::SameLine();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    bool button = false;
    if (defChanged){
        button = ImGui::Button((ICON_FA_ROTATE_LEFT"##"+label).c_str());
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(secondColSize);

    return button;
}

bool editor::Properties::propertyRow(RowPropertyType type, ComponentType cpType, std::string id, std::string label, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings){
    bool result = true;

    // Script properties and submesh fields are per entity, a selection may not share all of them
    entities.erase(std::remove_if(entities.begin(), entities.end(), [&](Entity entity){
        return !Catalog::findProperty(sceneProject->scene, entity, cpType, id).ref;
    }), entities.end());
    if (entities.empty()){
        return false;
    }

    constexpr float compThreshold = 1e-4;
    constexpr float zeroThreshold = 1e-4;

    if (type == RowPropertyType::Label){
        std::string displayValue;
        bool different = false;

        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            std::string value = formatPropertyLabelValue(prop);
            if (displayValue.empty()) {
                displayValue = value;
            } else if (displayValue != value) {
                different = true;
            }
        }

        propertyHeader(label, settings.secondColSize, false, settings.child);
        if (different) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextUnformatted("---");
            ImGui::PopStyleColor();
        } else {
            ImGui::TextUnformatted(displayValue.c_str());
        }

    }else if (type == RowPropertyType::Vector2){
        Vector2* value = nullptr;
        bool difX = false;
        bool difY = false;
        std::map<Entity, Vector2> eValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector2*>(prop.ref);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
            }
            value = &eValue[entity];
        }

        Vector2 newValue = *value;
    bool clampNonNegativeTilePosition = cpType == ComponentType::TilemapComponent
        && id.rfind("tiles[", 0) == 0
        && id.size() >= 9
        && id.compare(id.size() - 9, 9, ".position") == 0;

        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&newValue, defArr, 2, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector2>(project, sceneProject->id, entity, cpType, id, static_cast<Vector2>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushMultiItemsWidths(2, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[2] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255)    // Green for Y
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                Vector2 nextValue(newValue.x, eValue[entity].y);
                if (clampNonNegativeTilePosition){
                    nextValue.x = std::max(0.0f, nextValue.x);
                    nextValue.y = std::max(0.0f, nextValue.y);
                }
                cmd = new PropertyCmd<Vector2>(project, sceneProject->id, entity, cpType, id, nextValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                Vector2 nextValue(eValue[entity].x, newValue.y);
                if (clampNonNegativeTilePosition){
                    nextValue.x = std::max(0.0f, nextValue.x);
                    nextValue.y = std::max(0.0f, nextValue.y);
                }
                cmd = new PropertyCmd<Vector2>(project, sceneProject->id, entity, cpType, id, nextValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::Vector3 || type == RowPropertyType::Direction){
        Vector3* value = nullptr;
        bool difX = false;
        bool difY = false;
        bool difZ = false;
        std::map<Entity, Vector3> eValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector3*>(prop.ref);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
                if (std::fabs(value->z - eValue[entity].z) > compThreshold)
                    difZ = true;
            }
            value = &eValue[entity];
        }

        Vector3 newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&newValue, defArr, 3, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, static_cast<Vector3>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        float min = 0.0f;
        float max = 0.0f;
        if (type == RowPropertyType::Direction){
            min = -1.0f;
            max = 1.0f;

            Texture dirTexRender = getDirectionPreview(newValue, id);
            float thumbSize = ImGui::GetFrameHeight() * 3;
            ImU32 border_col = IM_COL32(128, 128, 128, 255); // Gray border

            drawImageWithBorderAndRounding(&dirTexRender, ImVec2(thumbSize, thumbSize), 4.0f, border_col);

            static bool draggingDirection = false;

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                draggingDirection = true;

                ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

                float sensitivity = 1.5f;

                float yawAngle = mouseDelta.x * sensitivity;   // Horizontal movement -> roll (around Y)
                float pitchAngle = -mouseDelta.y * sensitivity; // Vertical movement -> pitch (around X)

                Quaternion rollRotation(yawAngle, Vector3(0, 0, 1));     // Roll around world Z-axis
                Quaternion pitchRotation(pitchAngle, Vector3(1, 0, 0)); // Pitch around X-axis
                // Combine rotations (order matters: apply roll first, then pitch)
                Vector3 newDirection =  rollRotation * pitchRotation * newValue;

                // Apply to all entities
                for (Entity& entity : entities) {
                    cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, newDirection, settings.onValueChanged);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                }

                newValue = newDirection;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }

            if (draggingDirection && ImGui::IsMouseReleased(ImGuiMouseButton_Left)){
                finishProperty = true;
            }
        }

        ImGui::SetNextItemWidth(settings.secondColSize);

        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[3] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255),   // Green for Y
            IM_COL32(60, 60, 220, 255)    // Blue for Z
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, min, max, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Vector3(newValue.x, eValue[entity].y, eValue[entity].z), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, min, max, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Vector3(eValue[entity].x, newValue.y, eValue[entity].z), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difZ)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosZ = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_z_"+id).c_str(), &(newValue.z), settings.stepSize, min, max, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Vector3(eValue[entity].x, eValue[entity].y, newValue.z), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw blue bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosZ.x + 2, inputPosZ.y + 2);
            float inputHeightZ = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosZ.x + 2 + colorBarWidth, inputPosZ.y + inputHeightZ - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[2]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Z");
        }

        if (difZ)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::Vector4){
        Vector4* value = nullptr;
        bool difX = false;
        bool difY = false;
        bool difZ = false;
        bool difW = false;
        std::map<Entity, Vector4> eValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector4*>(prop.ref);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
                if (std::fabs(value->z - eValue[entity].z) > compThreshold)
                    difZ = true;
                if (std::fabs(value->w - eValue[entity].w) > compThreshold)
                    difW = true;
            }
            value = &eValue[entity];
        }

        Vector4 newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&newValue, defArr, 4, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, static_cast<Vector4>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushMultiItemsWidths(4, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[4] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255),   // Green for Y
            IM_COL32(60, 60, 220, 255),   // Blue for Z
            IM_COL32(220, 220, 220, 255)  // White/Grey for W
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(newValue.x, eValue[entity].y, eValue[entity].z, eValue[entity].w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(eValue[entity].x, newValue.y, eValue[entity].z, eValue[entity].w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difZ)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosZ = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_z_"+id).c_str(), &(newValue.z), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(eValue[entity].x, eValue[entity].y, newValue.z, eValue[entity].w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw blue bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosZ.x + 2, inputPosZ.y + 2);
            float inputHeightZ = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosZ.x + 2 + colorBarWidth, inputPosZ.y + inputHeightZ - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[2]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Z");
        }

        if (difZ)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difW)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosW = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_w_"+id).c_str(), &(newValue.w), settings.stepSize, 0.0f, 0.0f, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Vector4(eValue[entity].x, eValue[entity].y, eValue[entity].z, newValue.w), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw white/grey bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosW.x + 2, inputPosW.y + 2);
            float inputHeightW = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosW.x + 2 + colorBarWidth, inputPosW.y + inputHeightW - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[3]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("W");
        }

        if (difW)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::Quat){
        RotationOrder order = RotationOrder::ZYX;
        Vector3* value = nullptr;
        bool difX = false;
        bool difY = false;
        bool difZ = false;
        std::map<Entity, Vector3> eValue;
        Quaternion qValue;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            qValue = *static_cast<Quaternion*>(prop.ref);
            eValue[entity] = getDisplayedEulerAngles(qValue, order, zeroThreshold);
            if (value){
                if (std::fabs(value->x - eValue[entity].x) > compThreshold)
                    difX = true;
                if (std::fabs(value->y - eValue[entity].y) > compThreshold)
                    difY = true;
                if (std::fabs(value->z - eValue[entity].z) > compThreshold)
                    difZ = true;
            }
            value = &eValue[entity];
        }

        Vector3 newValue = *value;

        // using 'qValue' to compare quaternions
        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)&qValue, defArr, 4, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, static_cast<Quaternion>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());

        // Define axis colors
        ImU32 axisColors[3] = {
            IM_COL32(220, 60, 60, 255),   // Red for X
            IM_COL32(60, 220, 60, 255),   // Green for Y
            IM_COL32(60, 60, 220, 255)    // Blue for Z
        };

        // Get draw list for drawing inside input fields
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        float colorBarWidth = 4.0f; // Width of the colored bar

        if (difX)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosX = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_x_"+id).c_str(), &(newValue.x), settings.stepSize, 0.0f, 0.0f, (std::string(settings.format) + "°").c_str())){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, Quaternion(newValue.x, eValue[entity].y, eValue[entity].z, order), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw red bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosX.x + 2, inputPosX.y + 2);
            float inputHeightX = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosX.x + 2 + colorBarWidth, inputPosX.y + inputHeightX - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[0]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("X");
        }

        if (difX)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difY)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosY = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_y_"+id).c_str(), &(newValue.y), settings.stepSize, 0.0f, 0.0f, "%.2f°")){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, Quaternion(eValue[entity].x, newValue.y, eValue[entity].z, order), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw green bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosY.x + 2, inputPosY.y + 2);
            float inputHeightY = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosY.x + 2 + colorBarWidth, inputPosY.y + inputHeightY - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[1]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Y");
        }

        if (difY)
            ImGui::PopStyleColor();

        ImGui::SameLine();
        if (difZ)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Store cursor position before drawing the input
        ImVec2 inputPosZ = ImGui::GetCursorScreenPos();

        if (ImGui::DragFloat(("##input_z_"+id).c_str(), &(newValue.z), settings.stepSize, 0.0f, 0.0f, "%.2f°")){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, id, Quaternion(eValue[entity].x, eValue[entity].y, newValue.z, order), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        // Draw blue bar inside the input field
        if (settings.showColors) {
            ImVec2 barMin(inputPosZ.x + 2, inputPosZ.y + 2);
            float inputHeightZ = ImGui::GetItemRectSize().y;
            ImVec2 barMax(inputPosZ.x + 2 + colorBarWidth, inputPosZ.y + inputHeightZ - 2);
            drawList->AddRectFilled(
                barMin,
                barMax,
                axisColors[2]
            );
            if (ImGui::IsMouseHoveringRect(barMin, barMax))
                ImGui::SetTooltip("Z");
        }

        if (difZ)
            ImGui::PopStyleColor();

        ImGui::EndGroup();
        //ImGui::SetItemTooltip("%s in degrees (X, Y, Z)", prop.label.c_str());

    }else if (type == RowPropertyType::String || type == RowPropertyType::MultilineString){
        std::string* value = nullptr;
        std::map<Entity, std::string> eValue;
        bool dif = false;
        std::string* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<std::string*>(prop.def);
            eValue[entity] = *static_cast<std::string*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        std::string newValue = *value;

        std::vector<char> buffer(newValue.begin(), newValue.end());
        buffer.resize(std::max((size_t)1024, newValue.size() + 256));
        buffer[newValue.size()] = '\0';

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        bool inputChanged = false;
        if (type == RowPropertyType::MultilineString){
            inputChanged = ImGui::InputTextMultiline(("##input_string_"+id).c_str(), buffer.data(), buffer.size(), ImVec2(0, ImGui::GetTextLineHeight() * 6));
        }else{
            inputChanged = ImGui::InputText(("##input_string_"+id).c_str(), buffer.data(), buffer.size());
        }

        if (inputChanged){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, id, std::string(buffer.data()), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Bool){
        bool* value = nullptr;
        std::map<Entity, bool> eValue;
        bool dif = false;
        bool* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<bool*>(prop.def);
            eValue[entity] = *static_cast<bool*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        bool newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<bool>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Checkbox(("##checkbox_"+id).c_str(), &newValue)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<bool>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

        if (!settings.help.empty()){
            ImGui::SameLine(); helpMarker(settings.help);
        }

    }else if (type == RowPropertyType::Float || type == RowPropertyType::FloatPositive || type == RowPropertyType::Float_0_1 || type == RowPropertyType::HalfCone){
        float* value = nullptr;
        std::map<Entity, float> eValue;
        bool dif = false;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<float*>(prop.ref);
            if (type == RowPropertyType::HalfCone){
                eValue[entity] = Angle::radToDefault(std::acos(eValue[entity]) * 2);
            }
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        float newValue = *value;

        bool defChanged = false;
        if (defArr){
            if (type == RowPropertyType::HalfCone){
                float def = *defArr;
                defChanged = (newValue != Angle::radToDefault(std::acos(def) * 2));
            }else{
                defChanged = (newValue != *defArr);
            }
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        float v_min = (0.0F);
        float v_max = (0.0F);
        if (type == RowPropertyType::Float_0_1){
            v_min = 0.0F;
            v_max = 1.0F;
        }else if (type == RowPropertyType::FloatPositive){
            v_min = 0.0F;
            v_max = FLT_MAX;
        }

        std::string newFormat = settings.format;
        if (type == RowPropertyType::HalfCone){
            newFormat = std::string(settings.format) + "°";
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::DragFloat(("##input_float_"+id).c_str(), &newValue, settings.stepSize, v_min, v_max, newFormat.c_str())){
            for (Entity& entity : entities){
                if (type == RowPropertyType::HalfCone){
                    cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, id, cos(Angle::defaultToRad(newValue / 2)), settings.onValueChanged);
                }else{
                    cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                }
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

        if (!settings.help.empty()){
            ImGui::SameLine(); helpMarker(settings.help);
        }

    }else if (type == RowPropertyType::Double || type == RowPropertyType::DoublePositive){
        double* value = nullptr;
        std::map<Entity, double> eValue;
        bool dif = false;
        double* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<double*>(prop.def);
            eValue[entity] = *static_cast<double*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        double newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<double>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        double v_min = 0.0;
        double v_max = DBL_MAX;
        const void* minPtr = nullptr;
        const void* maxPtr = nullptr;
        if (type == RowPropertyType::DoublePositive){
            minPtr = &v_min;
            maxPtr = &v_max;
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::DragScalar(("##input_double_"+id).c_str(), ImGuiDataType_Double, &newValue, settings.stepSize, minPtr, maxPtr, settings.format)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<double>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();

        if (!settings.help.empty()){
            ImGui::SameLine(); helpMarker(settings.help);
        }

    }else if (type == RowPropertyType::UInt){
        unsigned int* value = nullptr;
        std::map<Entity, unsigned int> eValue;
        bool dif = false;
        unsigned int* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<unsigned int*>(prop.def);
            eValue[entity] = *static_cast<unsigned int*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        unsigned int newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::DragInt(("##input_uint_"+id).c_str(), (int*)&newValue, static_cast<int>(ceil(settings.stepSize)), 0.0f, INT_MAX)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if (type == RowPropertyType::Int){
        int* value = nullptr;
        std::map<Entity, int> eValue;
        bool dif = false;
        int* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<int*>(prop.def);
            eValue[entity] = *static_cast<int*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        // Clamp (drag and typed) only when a real range is configured; otherwise stay
        // unbounded as before (ImGui treats min == max as "no limit").
        const bool clampInt = settings.intMax > settings.intMin;
        if (ImGui::DragInt(("##input_int_"+id).c_str(), &newValue, static_cast<int>(ceil(settings.stepSize)),
                           clampInt ? settings.intMin : 0, clampInt ? settings.intMax : 0, "%d",
                           clampInt ? ImGuiSliderFlags_AlwaysClamp : 0)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if (type == RowPropertyType::Color3L){
        Vector3* value = nullptr;
        std::map<Entity, Vector3> eValue;
        bool dif = false;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector3*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Vector3 newValue = Color::linearTosRGB(*value);

        // using 'value' beacause it is linear too
        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)value, defArr, 3, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, static_cast<Vector3>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::ColorEdit3((label+"##checkbox_"+id).c_str(), (float*)&newValue.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, id, Color::sRGBToLinear(newValue), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if (type == RowPropertyType::Color4L){
        Vector4* value = nullptr;
        std::map<Entity, Vector4> eValue;
        bool dif = false;
        float* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<float*>(prop.def);
            eValue[entity] = *static_cast<Vector4*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Vector4 newValue = Color::linearTosRGB(*value);

        // using 'value' beacause it is linear too
        bool defChanged = false;
        if (defArr){
            defChanged = compareVectorFloat((float*)value, defArr, 4, compThreshold);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, static_cast<Vector4>(defArr), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::ColorEdit4((label+"##checkbox_"+id).c_str(), (float*)&newValue.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Vector4>(project, sceneProject->id, entity, cpType, id, Color::sRGBToLinear(newValue), settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
        if (dif)
            ImGui::PopStyleColor();
        //ImGui::SetItemTooltip("%s", prop.label.c_str());

    }else if ((type == RowPropertyType::IntSlider || type == RowPropertyType::UIntSlider) && settings.sliderValues){
        int* value = nullptr;
        std::map<Entity, int> eValue;
        bool dif = false;
        void* defArr = nullptr;
        std::vector<int>* sliderValues = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = prop.def;
            sliderValues = settings.sliderValues;
            if (type == RowPropertyType::IntSlider) {
                eValue[entity] = *static_cast<int*>(prop.ref);
            } else {
                eValue[entity] = *static_cast<unsigned int*>(prop.ref);
            }
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int newValue = *value;

        bool defChanged = false;
        if (defArr){
            if (type == RowPropertyType::IntSlider) {
                defChanged = (newValue != *static_cast<int*>(defArr));
            } else {
                defChanged = (newValue != *static_cast<unsigned int*>(defArr));
            }
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                if (type == RowPropertyType::IntSlider) {
                    cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, *static_cast<int*>(defArr), settings.onValueChanged);
                } else {
                    cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *static_cast<unsigned int*>(defArr), settings.onValueChanged);
                }
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        // Find current value index in the slider values array
        int currentIndex = 0;
        for (size_t i = 0; i < sliderValues->size(); ++i) {
            if ((*sliderValues)[i] == newValue) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // Create format string with current value
        char formatStr[32];
        snprintf(formatStr, sizeof(formatStr), "%d", (*sliderValues)[currentIndex]);

        if (ImGui::SliderInt(("##intslider_"+id).c_str(), &currentIndex, 0, static_cast<int>(sliderValues->size() - 1), formatStr)) {
            int newSliderValue = (*sliderValues)[currentIndex];
            for (Entity& entity : entities){
                if (type == RowPropertyType::IntSlider) {
                    cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, newSliderValue, settings.onValueChanged);
                } else {
                    cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, static_cast<unsigned int>(newSliderValue), settings.onValueChanged);
                }
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }

        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Enum && settings.enumEntries) {
        int* value = nullptr;
        std::map<Entity, int> eValue;
        bool dif = false;
        int* defArr = nullptr;
        std::vector<EnumEntry>* enumEntries = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<int*>(prop.def);
            enumEntries = settings.enumEntries;
            eValue[entity] = *static_cast<int*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int item_current = 0;
        // Find current index in enumEntries
        for (size_t i = 0; i < enumEntries->size(); ++i) {
            if ((*enumEntries)[i].value == *value) {
                item_current = static_cast<int>(i);
                break;
            }
        }
        int item_default = item_current;

        bool defChanged = false;
        if (defArr){
            int defValue = *defArr;
            // Find index of default value in enumEntries
            for (size_t i = 0; i < enumEntries->size(); ++i) {
                if ((*enumEntries)[i].value == defValue) {
                    item_default = static_cast<int>(i);
                    break;
                }
            }
            defChanged = (item_current != item_default);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                int defValue = (*enumEntries)[item_default].value;
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, defValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        // Build names array
        std::vector<const char*> names;
        for (const auto& entry : *enumEntries) {
            names.push_back(entry.name);
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Combo(("##combo_" + id).c_str(), &item_current, names.data(), static_cast<int>(names.size()))) {
            int newValue = (*enumEntries)[item_current].value;
            for (Entity& entity : entities){
                cmd = new PropertyCmd<int>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }
        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Ease) {
        Ease* value = nullptr;
        std::map<Entity, Ease> eValue;
        bool dif = false;
        Ease* defValue = nullptr;
        std::vector<EnumEntry>* enumEntries = settings.enumEntries ? settings.enumEntries : &entriesEaseType;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defValue = static_cast<Ease*>(prop.def);
            eValue[entity] = *static_cast<Ease*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        int item_current = 0;
        EaseType currentType = value ? value->getType() : EaseType::LINEAR;
        for (size_t i = 0; i < enumEntries->size(); ++i) {
            if ((*enumEntries)[i].value == static_cast<int>(currentType)) {
                item_current = static_cast<int>(i);
                break;
            }
        }

        int item_default = item_current;
        bool defChanged = false;
        if (defValue){
            EaseType defaultType = defValue->getType();
            for (size_t i = 0; i < enumEntries->size(); ++i) {
                if ((*enumEntries)[i].value == static_cast<int>(defaultType)) {
                    item_default = static_cast<int>(i);
                    break;
                }
            }
            defChanged = (currentType != defaultType);
        }

        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                Ease defaultEase(static_cast<EaseType>((*enumEntries)[item_default].value));
                cmd = new PropertyCmd<Ease>(project, sceneProject->id, entity, cpType, id, defaultEase, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        std::vector<const char*> names;
        for (const auto& entry : *enumEntries) {
            names.push_back(entry.name);
        }

        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Combo(("##combo_" + id).c_str(), &item_current, names.data(), static_cast<int>(names.size()))) {
            EaseType newType = static_cast<EaseType>((*enumEntries)[item_current].value);
            for (Entity& entity : entities){
                Ease newValue = eValue[entity];
                newValue.setType(newType);
                cmd = new PropertyCmd<Ease>(project, sceneProject->id, entity, cpType, id, newValue, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }
        if (dif)
            ImGui::PopStyleColor();

    }else if (type == RowPropertyType::Font){
        FontArray* value = nullptr;
        std::map<Entity, FontArray> eValue;
        bool dif = false;
        FontArray* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<FontArray*>(prop.def);
            eValue[entity] = *static_cast<FontArray*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        FontArray newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<FontArray>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        // the row edits the main font, the fallbacks are in the popup
        bool hasFallbacks = false;
        for (size_t slot = 1; slot < newValue.size(); slot++){
            if (!newValue[slot].empty()){
                hasFallbacks = true;
                break;
            }
        }

        // OpenPopup only reaches BeginPopup from the same id scope, outside drawFontSlot
        bool openFallbacks = false;

        drawFontSlot(cpType, id, 0, dif, newValue, sceneProject, entities, settings.onValueChanged, -1.0f,
            [&openFallbacks, hasFallbacks](const ImVec2& buttonSize){
                if (hasFallbacks)
                    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
                if (ImGui::Button(ICON_FA_LAYER_GROUP, buttonSize)) {
                    openFallbacks = true;
                }
                if (hasFallbacks)
                    ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Additional fonts, used for what the main font misses");
            });

        const std::string fallbacksPopup = "fontfallbackspopup_" + id;
        if (openFallbacks){
            ImGui::OpenPopup(fallbacksPopup.c_str());
        }
        drawFontFallbacksPopup(fallbacksPopup.c_str(), cpType, id, sceneProject, entities, settings.onValueChanged);

    }else if (type == RowPropertyType::Texture){
        Texture* value = nullptr;
        std::map<Entity, Texture> eValue;
        bool dif = false;
        Texture* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<Texture*>(prop.def);
            eValue[entity] = *static_cast<Texture*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Texture newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushID(("texture_"+id).c_str());

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);

        bool isCameraTexture = CameraTextureLink::isCameraTexture(newValue);

        float thumbSize = ImGui::GetFrameHeight() * 3;
        Texture* thumbTexture = findThumbnail(newValue.getPath());
        if (thumbTexture) {
            ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            if (dif){
                border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                Widgets::image(Backend::getImGuiTexture(thumbTexture->getRender()), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                ImGui::EndTooltip();
            }
        } else if (isCameraTexture && newValue.isFramebuffer() && newValue.getFramebuffer()->isCreated()) {
            // live preview of the linked camera framebuffer
            ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            if (dif){
                border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            drawImageWithBorderAndRounding(&newValue, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
        }

        // sampler settings (min/mag filter, wrap) only apply to an actual image texture,
        // not to a camera/framebuffer link or an empty slot
        bool showTexSettings = settings.allowTextureSettings && !isCameraTexture && !newValue.empty();

        ImVec2 texButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
        // reserve room for the action buttons (file + camera, plus a settings button when
        // applicable), with one gap between each item and a trailing gap on the right
        int texButtonCount = settings.allowCameraTexture ? 2 : 1;
        if (showTexSettings) texButtonCount++;
        float texButtonsReserve = texButtonSize.x * texButtonCount + ImGui::GetStyle().ItemSpacing.x * (texButtonCount + 1);
        ImGui::BeginChild("textureframe", ImVec2(-texButtonsReserve, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2),
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        std::string texName = newValue.getId();
        if (isCameraTexture) {
            texName = std::string(ICON_FA_VIDEO) + " " + CameraTextureLink::cameraName(sceneProject->scene, newValue);
        } else {
            // Never show the "?svgScale=" suffix in the slot; display the clean path/name.
            texName = TextureData::parseSvgScalePath(texName);
            if (std::filesystem::exists(texName)) {
                texName = std::filesystem::path(texName).filename().string();
            }
        }
        if (texName.empty()) {
            texName = "< Not set >";
        }

        float textWidth = ImGui::CalcTextSize(texName.c_str()).x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(availWidth - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", texName.c_str());
        if (dif)
            ImGui::PopStyleColor();

        ImGui::EndChild();
        if (!newValue.getId().empty()){
            ImGui::SetItemTooltip("%s", newValue.getId().c_str());
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        // no horizontal frame padding so the icons fit centered in the square buttons
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));

        if (ImGui::Button(ICON_FA_FOLDER_OPEN, texButtonSize)) {
            std::string path = editor::FileDialogs::openFileDialog(project->getAssetsPath().string(), FILE_DIALOG_IMAGE);
            if (!path.empty()) {
                std::filesystem::path assetsPath = project->getAssetsPath();
                std::filesystem::path filePath = std::filesystem::absolute(path);

                // References are stored relative to the assets root
                std::error_code ec;
                auto relative = std::filesystem::relative(filePath, assetsPath, ec);
                if (ec || relative.empty() || *relative.begin() == "..") {
                    ImGui::OpenPopup("File Import Error");
                }else{
                    Texture texture(relative.string());
                    for (Entity& entity : entities){
                        cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                        finishProperty = true;
                    }
                }
            }
        }
        ImGui::SetItemTooltip("Image file");

        if (settings.allowCameraTexture) {
            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_VIDEO, texButtonSize)) {
                ImGui::OpenPopup("cameratexturepopup");
            }
            ImGui::SetItemTooltip("Camera as source (render to texture)");
        }

        if (showTexSettings) {
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_SLIDERS, texButtonSize)) {
                ImGui::OpenPopup("texturesettingspopup");
            }
            ImGui::SetItemTooltip("Texture settings (filter, wrap, SVG scale)");
        }

        ImGui::PopStyleVar();

        drawTextureSettingsPopup("texturesettingspopup", cpType, id, sceneProject, entities, settings.onValueChanged);

        if (settings.allowCameraTexture && ImGui::BeginPopup("cameratexturepopup")) {
            Scene* scene = sceneProject->scene;
            auto camerasArray = scene->getComponentArray<CameraComponent>();
            bool hasCamera = false;
            for (size_t c = 0; c < camerasArray->size(); c++) {
                Entity cameraEntity = camerasArray->getEntity(c);
                // only user scene entities; skips editor-internal cameras
                if (std::find(sceneProject->entities.begin(), sceneProject->entities.end(), cameraEntity) == sceneProject->entities.end())
                    continue;

                hasCamera = true;
                std::string cameraLabel = std::string(ICON_FA_VIDEO) + " " + scene->getEntityName(cameraEntity);
                if (ImGui::MenuItem(cameraLabel.c_str())) {
                    applyCameraTexture(cameraEntity, cpType, id, sceneProject, entities, settings.onValueChanged);
                    finishProperty = true;
                }
            }
            if (!hasCamera) {
                ImGui::TextDisabled("No camera in scene");
            }
            ImGui::EndPopup();
        }

        // Error popup modal
        if (ImGui::BeginPopupModal("File Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the assets directory.");
            ImGui::Separator();

            float buttonWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
        ImGui::EndGroup();

        dragDropResourcesTexture(cpType, id, sceneProject, entities, cpType);

    }else if (type == RowPropertyType::TextureCube){
        Texture* value = nullptr;
        std::map<Entity, Texture> eValue;
        bool dif = false;
        Texture* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<Texture*>(prop.def);
            eValue[entity] = *static_cast<Texture*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }

        Texture newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();
        ImGui::PushID(("texturecube_"+id).c_str());

        auto isSingleFileCube = [](const Texture& t) -> bool {
            for (size_t i = 1; i < 6; i++){
                if (!t.getPath(i).empty())
                    return false;
            }
            return true;
        };

        if (!textureCubeSingleMode.count(id)){
            textureCubeSingleMode[id] = isSingleFileCube(newValue);
        }

        bool& singleMode = textureCubeSingleMode[id];

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Mode");
        ImGui::SameLine();
        {
            static const char* modeItems[] = {"Cubemap file", "Separate files"};
            int modeIndex = singleMode ? 0 : 1;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##cube_mode", &modeIndex, modeItems, IM_ARRAYSIZE(modeItems))){
                singleMode = (modeIndex == 0);

                for (Entity& entity : entities){
                    cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    finishProperty = true;
                }
            }
        }

        if (singleMode){
            std::string cubePath = newValue.getPath(0);
            std::string cubeName = cubePath;
            if (std::filesystem::exists(cubeName)) {
                cubeName = std::filesystem::path(cubeName).filename().string();
            }
            if (cubeName.empty()) {
                cubeName = "< Not set >";
            }

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Cubemap");
            ImGui::PopStyleVar();

            ImVec2 dragMin = ImGui::GetCursorScreenPos();
            ImVec2 dragMax = dragMin;

            float thumbSize = ImGui::GetFrameHeight() * 3;
            Texture* thumbTexture = findThumbnail(cubePath);
            if (thumbTexture) {
                ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                if (dif){
                    border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                }
                drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    Widgets::image(Backend::getImGuiTexture(thumbTexture->getRender()), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                    ImGui::EndTooltip();
                }
                dragMax = ImGui::GetItemRectMax();
            }

            float iconButtonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;

            ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);

            ImGui::BeginChild("textureframe", ImVec2(- iconButtonWidth - ImGui::GetStyle().ItemSpacing.x, rowHeight),
                false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

            float textWidth = ImGui::CalcTextSize(cubeName.c_str()).x;
            float availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(availWidth - textWidth - 2);
            ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
            if (dif)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::Text("%s", cubeName.c_str());
            if (dif)
                ImGui::PopStyleColor();

            ImGui::EndChild();
            if (!cubePath.empty()){
                ImGui::SetItemTooltip("%s", cubePath.c_str());
            }

            ImGui::PopStyleColor();

            dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
            dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

            ImGui::SameLine();

            if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
                std::string path = editor::FileDialogs::openFileDialog(project->getAssetsPath().string(), FILE_DIALOG_IMAGE);
                if (!path.empty()) {
                    std::filesystem::path assetsPath = project->getAssetsPath();
                    std::filesystem::path filePath = std::filesystem::absolute(path);

                    std::error_code ec;
                    auto relative = std::filesystem::relative(filePath, assetsPath, ec);
                    if (ec || relative.empty() || *relative.begin() == "..") {
                        ImGui::OpenPopup("File Import Error##cube");
                    }else{
                        Texture texture = newValue;
                        texture.setCubeMap(relative.string());
                        for (Entity& entity : entities){
                            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, settings.onValueChanged);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                            finishProperty = true;
                        }
                    }
                }
            }

            dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
            dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

            dragDropResourcesTextureCubeSingleFile(cpType, id, dragMin, dragMax, sceneProject, entities, cpType);

            if (ImGui::BeginPopupModal("File Import Error##cube", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Selected file must be within the assets directory.");
                ImGui::Separator();

                float buttonWidth = 120;
                float windowWidth = ImGui::GetWindowSize().x;
                ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                if (ImGui::Button("OK", ImVec2(120, 0))) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }else{
            // Engine cubemap indexing (sokol/OpenGL-style):
            // 0=(+X), 1=(-X), 2=(+Y), 3=(-Y), 4=(+Z), 5=(-Z)
            static const char* faceNames[6] = {"Positive X", "Negative X", "Positive Y", "Negative Y", "Positive Z", "Negative Z"};
            static const size_t faceIndexMap[6] = {0, 1, 2, 3, 4, 5};

            for (size_t uiFace = 0; uiFace < 6; uiFace++){
                const size_t faceIndex = faceIndexMap[uiFace];
                ImGui::PushID(static_cast<int>(uiFace));

                std::string facePath = newValue.getPath(faceIndex);
                std::string faceName = facePath;
                if (std::filesystem::exists(faceName)) {
                    faceName = std::filesystem::path(faceName).filename().string();
                }
                if (faceName.empty()) {
                    faceName = "< Not set >";
                }

                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", faceNames[uiFace]);
                ImGui::PopStyleVar();

                // Drag/drop target rect should cover thumbnail + path + button
                ImVec2 dragMin = ImGui::GetCursorScreenPos();
                ImVec2 dragMax = dragMin;

                float thumbSize = ImGui::GetFrameHeight() * 3;
                Texture* thumbTexture = findThumbnail(facePath);
                if (thumbTexture) {
                    ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                    if (dif){
                        border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    }
                    drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        Widgets::image(Backend::getImGuiTexture(thumbTexture->getRender()), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                        ImGui::EndTooltip();
                    }
                    dragMax = ImGui::GetItemRectMax();
                }

                // Path row under thumbnail
                float iconButtonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float rowHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);

                ImGui::BeginChild("textureframe", ImVec2(- iconButtonWidth - ImGui::GetStyle().ItemSpacing.x, rowHeight),
                    false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                float textWidth = ImGui::CalcTextSize(faceName.c_str()).x;
                float availWidth = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(availWidth - textWidth - 2);
                ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
                if (dif)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::Text("%s", faceName.c_str());
                if (dif)
                    ImGui::PopStyleColor();

                ImGui::EndChild();
                if (!facePath.empty()){
                    ImGui::SetItemTooltip("%s", facePath.c_str());
                }

                ImGui::PopStyleColor();

                dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
                dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

                ImGui::SameLine();

                if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
                    std::string path = editor::FileDialogs::openFileDialog(project->getAssetsPath().string(), FILE_DIALOG_IMAGE);
                    if (!path.empty()) {
                        std::filesystem::path assetsPath = project->getAssetsPath();
                        std::filesystem::path filePath = std::filesystem::absolute(path);

                        // References are stored relative to the assets root
                        std::error_code ec;
                        auto relative = std::filesystem::relative(filePath, assetsPath, ec);
                        if (ec || relative.empty() || *relative.begin() == "..") {
                            ImGui::OpenPopup("File Import Error##cube");
                        }else{
                            Texture texture = newValue;
                            texture.setCubePath(faceIndex, relative.string());
                            for (Entity& entity : entities){
                                cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, id, texture, settings.onValueChanged);
                                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                                finishProperty = true;
                            }
                        }
                    }
                }

                dragMax.x = std::max(dragMax.x, ImGui::GetItemRectMax().x);
                dragMax.y = std::max(dragMax.y, ImGui::GetItemRectMax().y);

                dragDropResourcesTextureCubeFace(cpType, id, faceIndex, dragMin, dragMax, sceneProject, entities, cpType);

                // Error popup modal
                if (ImGui::BeginPopupModal("File Import Error##cube", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::Text("Selected file must be within the assets directory.");
                    ImGui::Separator();

                    float buttonWidth = 120;
                    float windowWidth = ImGui::GetWindowSize().x;
                    ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
                    if (ImGui::Button("OK", ImVec2(120, 0))) {
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }

        ImGui::PopID();
        ImGui::EndGroup();

    }else if (type == RowPropertyType::Material){
        Material* value = nullptr;
        std::map<Entity, Material> eValue;
        bool dif = false;
        Material* defArr = nullptr;
        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defArr = static_cast<Material*>(prop.def);
            eValue[entity] = *static_cast<Material*>(prop.ref);
            if (value){
                if (*value != eValue[entity])
                    dif = true;
            }
            value = &eValue[entity];
        }
        Material newValue = *value;

        bool defChanged = false;
        if (defArr){
            defChanged = (newValue != *defArr);
        }
        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<Material>(project, sceneProject->id, entity, cpType, id, *defArr, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        bool receiveIBL = true;
        if (cpType == ComponentType::MeshComponent && !entities.empty()) {
            receiveIBL = sceneProject->scene->getComponent<MeshComponent>(entities[0]).receiveIBL;
        }

        Texture texRender = getMaterialPreview(newValue, id, receiveIBL);
        float thumbSize = ImGui::GetFrameHeight() * 3;
        Widgets::image(Backend::getImGuiTexture(texRender.getRender()), ImVec2(thumbSize, thumbSize));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            materialButtonGroups[id] = !materialButtonGroups[id];
        }

        unsigned int materialSubmeshIndex = 0;
        bool hasMaterialSubmeshIndex = false;
        auto submeshPos = id.find('[');
        auto submeshEnd = id.find(']');
        if (submeshPos != std::string::npos && submeshEnd != std::string::npos) {
            try {
                materialSubmeshIndex = std::stoul(id.substr(submeshPos + 1, submeshEnd - submeshPos - 1));
                hasMaterialSubmeshIndex = true;
            } catch (const std::exception&) {
                materialSubmeshIndex = 0;
            }
        }

        bool hasLinkedMaterial = false;
        if (sceneProject && hasMaterialSubmeshIndex) {
            for (Entity& entity : entities) {
                if (project->isMaterialFileLinked(sceneProject->id, entity, materialSubmeshIndex)) {
                    hasLinkedMaterial = true;
                    break;
                }
            }
        }

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);

        ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
        ImVec2 unlinkButtonSize = arrowButtonSize;
        float frameWidth = -arrowButtonSize.x - unlinkButtonSize.x - ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::BeginChild("textureframe", ImVec2(frameWidth, ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2), 
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        std::string matName = newValue.name;
        if (!matName.empty()) {
            std::filesystem::path matPath = std::filesystem::path(matName);
            if (!matPath.is_absolute()) {
                matPath = project->getProjectPath() / matPath;
            }
            if (std::filesystem::exists(matPath)) {
                matName = std::filesystem::path(newValue.name).filename().string();
            }
        }
        if (matName.empty()) {
            matName = "< Not defined >";
        }

        float textWidth = ImGui::CalcTextSize(matName.c_str()).x;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(availWidth - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (dif)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", matName.c_str());
        if (dif)
            ImGui::PopStyleColor();

        ImGui::EndChild();
        if (!newValue.name.empty()){
            ImGui::SetItemTooltip("%s", newValue.name.c_str());
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::ArrowButton("##toggle_mesh", materialButtonGroups[id] ? ImGuiDir_Up : ImGuiDir_Down)){
            materialButtonGroups[id] = !materialButtonGroups[id];
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!hasLinkedMaterial);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));

        float currentScale = ImGui::GetFont()->Scale;
        ImGui::SetWindowFontScale(currentScale * 0.75f);

        if (ImGui::Button(ICON_FA_LINK_SLASH "##unlink_material", unlinkButtonSize)){
            if (sceneProject && hasMaterialSubmeshIndex) {
                auto* unlinkCmd = new UnlinkMaterialCmd(project, sceneProject->id, cpType, id, materialSubmeshIndex, entities, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(unlinkCmd);
                finishProperty = true;
            }
        }

        ImGui::SetWindowFontScale(currentScale);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(hasLinkedMaterial ? "Unlink material file" : "Material is not linked to a file");
        }

        ImGui::EndGroup();

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            std::string materialStr = YAML::Dump(Stream::encodeMaterial(newValue));

            std::vector<char> materialPayload;
            MaterialPayload header{0x4D54524C, NULL_PROJECT_SCENE, NULL_ENTITY, 0};

            if (cpType == ComponentType::MeshComponent && !entities.empty()) {
                header.sceneId = sceneProject->id;
                header.entity = entities[0];

                auto p = id.find('[');
                auto e = id.find(']');
                if (p != std::string::npos && e != std::string::npos) {
                    try {
                        header.submeshIndex = std::stoul(id.substr(p + 1, e - p - 1));
                    } catch (const std::exception&) {
                        header.submeshIndex = 0;
                    }
                }
            }

            materialPayload.resize(sizeof(MaterialPayload) + materialStr.size());
            std::memcpy(materialPayload.data(), &header, sizeof(MaterialPayload));
            std::memcpy(materialPayload.data() + sizeof(MaterialPayload), materialStr.data(), materialStr.size());

            ImGui::SetDragDropPayload("material", materialPayload.data(), materialPayload.size());
            ImGui::Text("Moving material");
            float imageDragSize = 32;
            float availWidth = ImGui::GetCurrentWindow()->Size.x;
            float xPos = (availWidth - imageDragSize) * 0.5f;
            ImGui::SetCursorPosX(xPos);
            Widgets::image(Backend::getImGuiTexture(texRender.getRender()), ImVec2(imageDragSize, imageDragSize));
            ImGui::EndDragDropSource();
        }

        // Material file drop preview state (must be outside drag-drop blocks for restore access)
        static std::string cachedMatDropPath;
        static Material cachedMatDropMaterial;
        static std::map<Entity, Material> matDropOriginals;
        static bool matDropPreviewing = false;
        static std::string matDropPropertyId;

        auto restoreMatDropPreview = [&]() {
            if (matDropPreviewing) {
                for (auto& [ent, origMat] : matDropOriginals) {
                    PropertyData prop = Catalog::findProperty(sceneProject->scene, ent, cpType, matDropPropertyId);
                    if (prop.ref) {
                        Material* matRef = static_cast<Material*>(prop.ref);
                        *matRef = origMat;
                        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(ent);
                        if (mesh) {
                            auto p = matDropPropertyId.find('[');
                            auto e = matDropPropertyId.find(']');
                            if (p != std::string::npos && e != std::string::npos) {
                                unsigned int sIdx = std::stoul(matDropPropertyId.substr(p + 1, e - p - 1));
                                if (sIdx < mesh->numSubmeshes) {
                                    mesh->submeshes[sIdx].needUpdateTexture = true;
                                    Catalog::updateSpriteBatch(sceneProject->scene, ent);
                                }
                            }
                        }
                    }
                }
                matDropPreviewing = false;
                matDropOriginals.clear();
                cachedMatDropPath.clear();
            }
        };

        if (!(ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                std::vector<std::string> receivedStrings = Util::getStringsFromPayload(payload);

                if (!receivedStrings.empty()) {
                    std::error_code ec;
                    std::filesystem::path relativePath = std::filesystem::relative(receivedStrings[0], project->getProjectPath(), ec);

                    if (ec) {
                        if (payload->IsDelivery()) {
                            restoreMatDropPreview();
                            ImGui::OpenPopup("File Import Error##material");
                        }
                    } else {
                        std::string droppedRelativePath = relativePath.lexically_normal().generic_string();
                        // Material files are project files; the image below is an asset
                        const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                        const std::string droppedImagePath = droppedAssetPath(project, receivedStrings[0], insideAssets);

                        if (Util::isMaterialFile(droppedRelativePath)) {
                            try {
                                if (cachedMatDropPath != droppedRelativePath) {
                                    cachedMatDropPath = droppedRelativePath;
                                    YAML::Node materialNode = YAML::LoadFile((project->getProjectPath() / relativePath).string());
                                    cachedMatDropMaterial = Stream::decodeMaterial(materialNode);
                                    cachedMatDropMaterial.name = droppedRelativePath;
                                }

                                if (!payload->IsDelivery()) {
                                    // Preview: save originals and apply
                                    if (!matDropPreviewing) {
                                        matDropOriginals.clear();
                                        matDropPropertyId = id;
                                        for (Entity& entity : entities) {
                                            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
                                            matDropOriginals[entity] = *static_cast<Material*>(prop.ref);
                                        }
                                        matDropPreviewing = true;
                                    }
                                    for (Entity& entity : entities) {
                                        PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
                                        Material* matRef = static_cast<Material*>(prop.ref);
                                        if (*matRef != cachedMatDropMaterial) {
                                            *matRef = cachedMatDropMaterial;
                                            MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
                                            if (mesh) {
                                                auto pos = id.find('[');
                                                auto end = id.find(']');
                                                if (pos != std::string::npos && end != std::string::npos) {
                                                    unsigned int sIdx = std::stoul(id.substr(pos + 1, end - pos - 1));
                                                    if (sIdx < mesh->numSubmeshes) {
                                                        mesh->submeshes[sIdx].needUpdateTexture = true;
                                                        Catalog::updateSpriteBatch(sceneProject->scene, entity);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    // Delivery: restore originals, then issue commands
                                    restoreMatDropPreview();

                                    for (Entity& entity : entities) {
                                        auto pos = id.find('[');
                                        auto end = id.find(']');
                                        if (pos != std::string::npos && end != std::string::npos) {
                                            unsigned int sIdx = std::stoul(id.substr(pos + 1, end - pos - 1));
                                            cmd = new LinkMaterialCmd(project, sceneProject->id, entity, cpType, id, sIdx, cachedMatDropMaterial, settings.onValueChanged);
                                        } else {
                                            cmd = new PropertyCmd<Material>(project, sceneProject->id, entity, cpType, id, cachedMatDropMaterial, settings.onValueChanged);
                                        }
                                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                                        finishProperty = true;
                                    }

                                    cachedMatDropPath.clear();
                                    ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                                }
                            } catch (const std::exception& e) {
                                Out::error("Error loading material file '%s': %s", droppedRelativePath.c_str(), e.what());
                            }
                        } else if (Util::isImageFile(droppedRelativePath)) {
                            std::string baseTexId = id + ".baseColorTexture";

                            if (!payload->IsDelivery()) {
                                // Preview: save originals and apply texture
                                if (!matDropPreviewing) {
                                    matDropOriginals.clear();
                                    matDropPropertyId = id;
                                    for (Entity& entity : entities) {
                                        PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
                                        matDropOriginals[entity] = *static_cast<Material*>(prop.ref);
                                    }
                                    matDropPreviewing = true;
                                }
                                for (Entity& entity : entities) {
                                    PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
                                    Material* matRef = static_cast<Material*>(prop.ref);
                                    if (matRef->baseColorTexture != Texture(droppedImagePath)) {
                                        matRef->baseColorTexture = Texture(droppedImagePath);
                                        MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
                                        if (mesh) {
                                            auto pos = id.find('[');
                                            auto end = id.find(']');
                                            if (pos != std::string::npos && end != std::string::npos) {
                                                unsigned int sIdx = std::stoul(id.substr(pos + 1, end - pos - 1));
                                                if (sIdx < mesh->numSubmeshes) {
                                                    mesh->submeshes[sIdx].needUpdateTexture = true;
                                                    Catalog::updateSpriteBatch(sceneProject->scene, entity);
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                // Delivery: restore originals, then issue texture command
                                restoreMatDropPreview();

                                if (insideAssets) {
                                    Texture texture(droppedImagePath);
                                    for (Entity& entity : entities) {
                                        cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, baseTexId, texture, settings.onValueChanged);
                                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                                        finishProperty = true;
                                    }
                                } else {
                                    Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                                }

                                cachedMatDropPath.clear();
                                ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        } else if (matDropPreviewing && matDropPropertyId == id) {
            // Drag ended without delivery — restore preview
            restoreMatDropPreview();
        }

        if (ImGui::BeginPopupModal("File Import Error##material", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the project directory.");
            ImGui::Separator();

            float buttonWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        result = materialButtonGroups[id];

    }else if (type == RowPropertyType::LocalEntity){
        Entity* value = nullptr;
        std::map<Entity, Entity> eValue;
        bool different = false;
        unsigned int* defVal = nullptr;

        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defVal = static_cast<unsigned int*>(prop.def);
            eValue[entity] = *static_cast<unsigned int*>(prop.ref);
            if (value && *value != eValue[entity]){
                different = true;
            }
            value = &eValue[entity];
        }

        Entity newValue = value ? *value : NULL_ENTITY;
        bool defChanged = (defVal && newValue != *defVal);

        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, *defVal, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        std::string entityName = "None";
        if (newValue != NULL_ENTITY && sceneProject->scene->isEntityCreated(newValue)) {
            entityName = sceneProject->scene->getEntityName(newValue);
            if (entityName.empty()) {
                entityName = "Entity " + std::to_string(newValue);
            }
        }

        bool invalidSelection = false;

        if (different || invalidSelection) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (different) {
                entityName = "---";
            }
        }

        std::string buttonLabel = ICON_FA_CIRCLE_DOT " " + entityName + "##local_entity_" + id;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x;
        float availableWidth = settings.secondColSize > 0 ? settings.secondColSize : ImGui::GetContentRegionAvail().x;
        ImVec2 inputSize = ImVec2(std::max(0.0f, availableWidth - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2), 0);

        std::string pickerPopupId = "##ep_" + id;

        if (ImGui::Button(buttonLabel.c_str(), inputSize)) {
            if (newValue != NULL_ENTITY && sceneProject->scene->isEntityCreated(newValue)) {
                project->clearSelectedEntities(sceneProject->id);
                project->addSelectedEntity(sceneProject->id, newValue);
            } else if (!different) {
                ImGui::OpenPopup(pickerPopupId.c_str());
                memset(entityPickerSearchBuffer, 0, sizeof(entityPickerSearchBuffer));
            }
        }

        if (ImGui::IsItemHovered() && !different) {
            if (newValue != NULL_ENTITY) {
                ImGui::SetTooltip("Entity: %s (ID: %u)", entityName.c_str(), static_cast<unsigned int>(newValue));
            } else {
                ImGui::SetTooltip("Click to select entity");
            }
        }

        // Entity picker popup
        auto pickerResult = drawEntityPickerPopup(pickerPopupId, settings.entityFilter, sceneProject, false, newValue, 0, settings.excludeEntities);
        if (pickerResult.chosen) {
            for (Entity& entity : entities) {
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, pickerResult.entity, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                const EntityPayload* entityPayload = static_cast<const EntityPayload*>(payload->Data);
                Entity droppedEntity = entityPayload->entity;
                bool sameScene = (entityPayload->entitySceneId == 0 || entityPayload->entitySceneId == sceneProject->id);
                bool excluded = std::find(settings.excludeEntities.begin(), settings.excludeEntities.end(), droppedEntity) != settings.excludeEntities.end();
                bool valid = sameScene && sceneProject->scene->isEntityCreated(droppedEntity) && !excluded;

                if (!sameScene){
                    ImGui::SetTooltip("Entity belongs to another scene");
                }else if (!valid){
                    ImGui::SetTooltip(excluded ? "Cannot reference this entity here" : "Invalid entity");
                }

                if (payload->IsDelivery() && valid) {
                    for (Entity& entity : entities) {
                        cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, droppedEntity, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    finishProperty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (different || invalidSelection) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(newValue == NULL_ENTITY && !different);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button((ICON_FA_XMARK "##clear_local_entity_" + id).c_str())) {
            for (Entity& entity : entities) {
                cmd = new PropertyCmd<unsigned int>(project, sceneProject->id, entity, cpType, id, NULL_ENTITY, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear entity reference");
        }

        ImGui::EndGroup();

    }else if (type == RowPropertyType::ExternalEntity){
        Entity* value = nullptr;
        std::map<Entity, Entity> eValue;
        std::map<Entity, uint32_t> eSceneId; // sceneId per selected entity
        bool different = false;
        unsigned int* defVal = nullptr;

        // Parse script index and property name from id (format: "scripts[N].propName")
        size_t scriptIdx = 0;
        std::string propName;
        {
            size_t bracket = id.find('[');
            size_t closeBracket = id.find(']');
            size_t dot = id.find('.', closeBracket != std::string::npos ? closeBracket : 0);
            if (bracket != std::string::npos && closeBracket != std::string::npos && dot != std::string::npos) {
                scriptIdx = std::stoul(id.substr(bracket + 1, closeBracket - bracket - 1));
                propName = id.substr(dot + 1);
            }
        }

        for (Entity& entity : entities){
            PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, id);
            defVal = static_cast<unsigned int*>(prop.def);
            eValue[entity] = *static_cast<unsigned int*>(prop.ref);

            // Read sceneId from ScriptProperty
            uint32_t sid = 0;
            ScriptComponent* sc = sceneProject->scene->findComponent<ScriptComponent>(entity);
            if (sc && scriptIdx < sc->scripts.size()) {
                for (auto& sp : sc->scripts[scriptIdx].properties) {
                    if (sp.name == propName && std::holds_alternative<EntityReference>(sp.value)) {
                        sid = std::get<EntityReference>(sp.value).sceneId;
                        break;
                    }
                }
            }
            eSceneId[entity] = sid;

            if (value && (*value != eValue[entity] || eSceneId[entities[0]] != sid)){
                different = true;
            }
            value = &eValue[entity];
        }

        Entity newValue = value ? *value : NULL_ENTITY;
        uint32_t currentSceneId = !entities.empty() ? eSceneId[entities[0]] : 0;
        bool defChanged = (defVal && newValue != *defVal);

        if (propertyHeader(label, settings.secondColSize, defChanged, settings.child)){
            for (Entity& entity : entities){
                // Write entity and sceneId as one command so bundle propagation sees the
                // complete reference (a split write would propagate a stale sceneId).
                cmd = new PropertyCmd<EntityReference>(project, sceneProject->id, entity, cpType, id, EntityReference{*defVal, 0}, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                finishProperty = true;
            }
        }

        ImGui::BeginGroup();

        // Resolve entity name, potentially from a different scene
        std::string entityName = "None";
        std::string sceneSuffix = "";
        Scene* resolvedScene = nullptr;

        if (newValue != NULL_ENTITY) {
            if (currentSceneId != 0) {
                SceneProject* targetSceneProject = project->getScene(currentSceneId);
                if (targetSceneProject && targetSceneProject->scene && targetSceneProject->scene->isEntityCreated(newValue)) {
                    resolvedScene = targetSceneProject->scene;
                    entityName = resolvedScene->getEntityName(newValue);
                    if (entityName.empty()) {
                        entityName = "Entity " + std::to_string(newValue);
                    }
                    sceneSuffix = " (" + targetSceneProject->name + ")";
                }
            } else if (sceneProject->scene->isEntityCreated(newValue)) {
                resolvedScene = sceneProject->scene;
                entityName = resolvedScene->getEntityName(newValue);
                if (entityName.empty()) {
                    entityName = "Entity " + std::to_string(newValue);
                }
            }
        }

        bool invalidSelection = (newValue != NULL_ENTITY && !resolvedScene);

        if (different || invalidSelection) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (different) {
                entityName = "---";
                sceneSuffix = "";
            } else if (invalidSelection) {
                entityName = "Entity " + std::to_string(newValue);
                SceneProject* storedScene = (currentSceneId != 0) ? project->getScene(currentSceneId) : sceneProject;
                sceneSuffix = storedScene ? " (" + storedScene->name + ")" : "";
            }
        }

        std::string displayLabel = entityName + sceneSuffix;
        std::string buttonLabel = ICON_FA_CIRCLE_DOT " " + displayLabel + "##ext_entity_" + id;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x;
        ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);

        // Tint button color to differentiate ExternalEntity from LocalEntity
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::ExtEntityButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Colors::ExtEntityButtonHovered);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Colors::ExtEntityButtonActive);

        std::string extPickerPopupId = "##ep_ext_" + id;

        if (ImGui::Button(buttonLabel.c_str(), inputSize)) {
            if (resolvedScene && newValue != NULL_ENTITY) {
                uint32_t selectSceneId = (currentSceneId != 0) ? currentSceneId : sceneProject->id;
                project->clearSelectedEntities(selectSceneId);
                project->addSelectedEntity(selectSceneId, newValue);
            } else if (!different) {
                ImGui::OpenPopup(extPickerPopupId.c_str());
                memset(entityPickerSearchBuffer, 0, sizeof(entityPickerSearchBuffer));
            }
        }
        ImGui::PopStyleColor(3);

        if (ImGui::IsItemHovered() && !different) {
            if (newValue != NULL_ENTITY) {
                std::string tipSceneName = sceneProject->name;
                uint32_t tipSceneId = sceneProject->id;
                if (currentSceneId != 0) {
                    SceneProject* tipScene = project->getScene(currentSceneId);
                    if (tipScene) {
                        tipSceneName = tipScene->name;
                        tipSceneId = currentSceneId;
                    }
                }
                ImGui::SetTooltip("Entity: %s (ID: %u)\nScene: %s (ID: %u)",
                    entityName.c_str(), static_cast<unsigned int>(newValue),
                    tipSceneName.c_str(), static_cast<unsigned int>(tipSceneId));
            } else {
                ImGui::SetTooltip("Click to select entity");
            }
        }

        // Entity picker popup
        auto extPickerResult = drawEntityPickerPopup(extPickerPopupId, settings.entityFilter, sceneProject, true, newValue, currentSceneId);
        if (extPickerResult.chosen) {
            for (Entity& entity : entities) {
                cmd = new PropertyCmd<EntityReference>(project, sceneProject->id, entity, cpType, id, EntityReference{extPickerResult.entity, extPickerResult.sceneId}, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                const EntityPayload* entityPayload = static_cast<const EntityPayload*>(payload->Data);
                Entity droppedEntity = entityPayload->entity;
                uint32_t droppedSceneId = entityPayload->entitySceneId;

                bool valid = false;
                SceneProject* sourceScene = project->getScene(droppedSceneId);
                if (sourceScene && sourceScene->scene) {
                    valid = sourceScene->scene->isEntityCreated(droppedEntity);
                }

                // Determine sceneId to store: 0 if same scene, source scene ID if cross-scene
                uint32_t storedSceneId = (droppedSceneId != sceneProject->id) ? droppedSceneId : 0;

                if (!valid && ImGui::IsItemHovered()){
                    ImGui::SetTooltip("Invalid entity");
                }

                if (payload->IsDelivery() && valid) {
                    for (Entity& entity : entities) {
                        cmd = new PropertyCmd<EntityReference>(project, sceneProject->id, entity, cpType, id, EntityReference{droppedEntity, storedSceneId}, settings.onValueChanged);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    finishProperty = true;
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (different || invalidSelection) {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(newValue == NULL_ENTITY && !different);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button((ICON_FA_XMARK "##clear_ext_entity_" + id).c_str())) {
            for (Entity& entity : entities) {
                cmd = new PropertyCmd<EntityReference>(project, sceneProject->id, entity, cpType, id, EntityReference{NULL_ENTITY, 0}, settings.onValueChanged);
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
            finishProperty = true;
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Clear entity reference");
        }

        ImGui::EndGroup();

    }

    if (ImGui::IsItemDeactivatedAfterEdit() || finishProperty) {
        if (cmd){
            cmd->setNoMerge();
            cmd = nullptr;
        }
        finishProperty = false;
    }

    return result;
}

bool editor::Properties::propertyRowWithAutoButton(RowPropertyType propType, ComponentType cpType, std::string id, std::string label, std::string autoId, std::string autoLabel, SceneProject* sceneProject, std::vector<Entity> entities, RowSettings settings) {
    auto onValueChanged = settings.onValueChanged;
    settings.onValueChanged = [this, sceneProject, entities, cpType, autoId, onValueChanged]() {
        if (onValueChanged) {
            onValueChanged();
        }
        for (auto& entity : entities){
            PropertyData autoProp = Catalog::findProperty(sceneProject->scene, entity, cpType, autoId);
            bool* autoVal = static_cast<bool*>(autoProp.ref);
            if (autoVal && *autoVal) {
                editor::MultiPropertyCmd* cmd = new editor::MultiPropertyCmd();
                cmd->addPropertyCmd<bool>(project, sceneProject->id, entity, cpType, autoId, false);
                cmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            }
        }
    };

    std::string helpText = settings.help;
    settings.help = "";

    bool rowChanged = propertyRow(propType, cpType, id, label, sceneProject, entities, settings);

    ImGui::SameLine();
    bool allAuto = true;
    bool anyAuto = false;
    for (auto& entity : entities){
        PropertyData autoProp = Catalog::findProperty(sceneProject->scene, entity, cpType, autoId);
        bool val = autoProp.ref && *static_cast<bool*>(autoProp.ref);
        if (val) anyAuto = true;
        else allAuto = false;
    }

    if (allAuto) {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::ButtonActivated);
    } else if (anyAuto) {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::Colors::ButtonActivated);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
    }

    float btnPaddingY = std::max(0.0f, (ImGui::GetFrameHeight() - ImGui::GetFontSize()) / 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 4.0f, btnPaddingY));

    std::string autoBtnId = ICON_FA_WAND_MAGIC_SPARKLES "##auto_" + id;
    if (ImGui::Button(autoBtnId.c_str())) {
        bool targetValue = !allAuto;
        editor::MultiPropertyCmd* cmdAuto = new editor::MultiPropertyCmd();
        for (auto& entity : entities) {
            cmdAuto->addPropertyCmd<bool>(project, sceneProject->id, entity, cpType, autoId, targetValue);
        }
        cmdAuto->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmdAuto);
    }

    ImGui::PopStyleVar();

    if (anyAuto) {
        ImGui::PopStyleColor(allAuto ? 1 : 2);
    } else {
        ImGui::PopStyleColor(1);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", autoLabel.c_str());
    }

    if (!helpText.empty()) {
        ImGui::SameLine(); helpMarker(helpText);
    }

    return rowChanged;
}

void editor::Properties::setParticleFrames(ComponentType cpType, const std::string& propertyId, SceneProject* sceneProject, Entity entity, const std::vector<int>& frames) {
    editor::PropertyCmd<std::vector<int>>* frameCmd = new editor::PropertyCmd<std::vector<int>>(project, sceneProject->id, entity, cpType, propertyId, frames);
    frameCmd->setNoMerge();
    CommandHandle::get(project->getSelectedSceneId())->addCommand(frameCmd);
}

void editor::Properties::setParticleBursts(ComponentType cpType, SceneProject* sceneProject, Entity entity, const std::vector<ParticleBurst>& bursts) {
    editor::PropertyCmd<std::vector<ParticleBurst>>* burstCmd = new editor::PropertyCmd<std::vector<ParticleBurst>>(project, sceneProject->id, entity, cpType, "bursts", bursts);
    burstCmd->setNoMerge();
    CommandHandle::get(project->getSelectedSceneId())->addCommand(burstCmd);
}

void editor::Properties::setParticleColorGradient(ComponentType cpType, SceneProject* sceneProject, Entity entity, const ParticleColorGradient& gradient) {
    ParticleColorGradient normalizedGradient = gradient;
    normalizedGradient.normalize();
    editor::PropertyCmd<ParticleColorGradient>* gradCmd = new editor::PropertyCmd<ParticleColorGradient>(project, sceneProject->id, entity, cpType, "colorGradient", normalizedGradient);
    gradCmd->setNoMerge();
    CommandHandle::get(project->getSelectedSceneId())->addCommand(gradCmd);
}

void editor::Properties::drawParticleFrameList(ComponentType cpType, const std::string& propertyId, const std::string& tableId, SceneProject* sceneProject, const std::vector<Entity>& entities) {
    if (entities.size() != 1) {
        ImGui::TextDisabled("Select a single entity to edit sprite frames");
        return;
    }

    Entity entity = entities[0];
    PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, propertyId);
    std::vector<int>* frames = static_cast<std::vector<int>*>(prop.ref);
    std::vector<int>* defFrames = static_cast<std::vector<int>*>(prop.def);
    if (!frames) {
        return;
    }

    bool defChanged = defFrames && *frames != *defFrames;
    beginTable(cpType, getLabelSize("Frames"), tableId + "_header");
    if (propertyHeader("Frames", -1, defChanged, false) && defFrames) {
        setParticleFrames(cpType, propertyId, sceneProject, entity, *defFrames);
        finishProperty = true;
    }
    ImGui::Text("%zu", frames->size());
    std::string addFrameLabel = ICON_FA_PLUS " Add Frame##" + tableId;
    ImGui::SameLine();
    float addFrameButtonWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::Button(addFrameLabel.c_str(), ImVec2(addFrameButtonWidth, 0))) {
        std::vector<int> newFrames = *frames;
        newFrames.push_back(newFrames.empty() ? 0 : newFrames.back() + 1);
        setParticleFrames(cpType, propertyId, sceneProject, entity, newFrames);
    }
    endTable();

    if (frames->empty()) {
        return;
    }

    const float frameInputWidth = 6.0f * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Frame 000"), tableId + "_values");
    for (size_t i = 0; i < frames->size(); i++) {
        ImGui::PushID(static_cast<int>(i));
        propertyHeader("Frame " + std::to_string(i), -1, false, false);

        float deleteButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetNextItemWidth(frameInputWidth);
        int newValue = (*frames)[i];
        if (ImGui::InputInt(("##particle_frame_value_" + tableId).c_str(), &newValue)) {
            std::vector<int> newFrames = *frames;
            newFrames[i] = newValue;
            setParticleFrames(cpType, propertyId, sceneProject, entity, newFrames);
        }

        ImGui::SameLine();
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - deleteButtonWidth);
        ImGui::SetCursorPosX(targetX);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Button((ICON_FA_TRASH_CAN "##particle_frame_delete_" + tableId).c_str())) {
            std::vector<int> newFrames = *frames;
            newFrames.erase(newFrames.begin() + static_cast<std::ptrdiff_t>(i));
            setParticleFrames(cpType, propertyId, sceneProject, entity, newFrames);
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    endTable();
}

void editor::Properties::drawTransform(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    // Add this code to calculate appropriate step size based on selected scene
    RowSettings settingsPos;
    if (sceneProject && sceneProject->sceneRender) {
        Camera* camera = sceneProject->sceneRender->getCamera();
        if (sceneProject->sceneType == SceneType::SCENE_3D) {
            // For 3D scenes, scale step based on distance from target
            float distanceFromTarget = camera->getDistanceFromTarget();
            settingsPos.stepSize = std::max(0.01f, distanceFromTarget / 200.0f);
        } else {
            // For 2D scenes, use the zoom level
            SceneRender2D* sceneRender2D = static_cast<SceneRender2D*>(sceneProject->sceneRender);
            float zoom = sceneRender2D->getZoom();
            settingsPos.stepSize = std::max(0.01f, zoom * 1.0f);
        }
    }

    beginTable(cpType, getLabelSize("billboard"));

    propertyRow(RowPropertyType::Vector3, cpType, "position", "Position", sceneProject, entities, settingsPos);
    propertyRow(RowPropertyType::Quat, cpType, "rotation", "Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "scale", "Scale", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "visible", "Visible", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "billboard", "Billboard", sceneProject, entities);

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GEAR)){
        ImGui::OpenPopup("menusettings_billboard");
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(19 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_billboard")){
        ImGui::Text("Billboard settings");
        ImGui::Separator();

        RowSettings settingsRotation;
        settingsRotation.secondColSize = 12 * ImGui::GetFontSize();

        beginTable(cpType, getLabelSize("cylindrical"));
        propertyRow(RowPropertyType::Bool, cpType, "fakeBillboard", "Fake", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "cylindricalBillboard", "Cylindrical", sceneProject, entities);
        propertyRow(RowPropertyType::Quat, cpType, "billboardRotation", "Rotation", sceneProject, entities, settingsRotation);
        endTable();

        ImGui::EndPopup();
    }

    endTable();
}

void editor::Properties::commitShaderFork(uint32_t sceneId, std::unique_ptr<ForkShaderCmd> forkCmd){
    if (!forkCmd->isValid()) {
        std::string error = forkCmd->getError();
        Backend::getApp().registerAlert("Error", error.empty() ? "Could not prepare the shader fork." : error);
        return;
    }

    std::filesystem::path vert = project->getProjectPath() / (forkCmd->getBase() + ".vert");
    std::filesystem::path frag = project->getProjectPath() / (forkCmd->getBase() + ".frag");
    CommandHandle::get(sceneId)->addCommandNoMerge(forkCmd.release());

    // execute() runs inside addCommandNoMerge; a failed write leaves nothing behind.
    if (!std::filesystem::exists(vert) || !std::filesystem::exists(frag)) {
        Backend::getApp().registerAlert("Error", "The shader files could not be created.");
        return;
    }

    if (CodeEditor* codeEditor = Backend::getApp().getCodeEditor()) {
        codeEditor->openFile(vert.string(), true);
        codeEditor->openFile(frag.string(), true);
    }
    if (ResourcesWindow* resources = Backend::getApp().getResourcesWindow())
        resources->refreshCurrentDirectory();
}

void editor::Properties::drawCustomShaderRow(ComponentType cpType, ShaderType shaderType, SceneProject* sceneProject, std::vector<Entity> entities){
    // The custom shader is per-component. The fork dialog chooses its project-relative
    // destination; engine includes still resolve through the embedded library unless the
    // user creates private copies. Shown only for a single selection.
    if (entities.size() != 1)
        return;

    Entity shaderEntity = entities[0];
    if (!Catalog::getPropertyRef<std::string>(sceneProject->scene, shaderEntity, cpType, "customShader"))
        return;

    ShaderUniformRows uniformRows = resolveShaderUniformRows(cpType, shaderType, sceneProject, shaderEntity);
    const char* longestLabel = "Shader";
    if (cpType == ComponentType::MeshComponent)
        longestLabel = "Depth Shader";
    else if (cpType == ComponentType::WaterComponent)
        longestLabel = "Underwater Shader";
    float labelSize = getLabelSize(longestLabel, false);

    ImGui::SeparatorText("Shaders");
    beginTable(cpType, labelSize, "custom_shader_table");
    propertyHeader("Shader");
    drawComponentShaderRow(cpType, shaderType, sceneProject, shaderEntity, "customShader", "custom_shader");

    // shadow maps and the depth pre-pass have their own fork, so a mesh fork that displaces
    // vertices or discards can keep its shadows in step
    if (cpType == ComponentType::MeshComponent) {
        propertyHeader("Depth Shader");
        drawComponentShaderRow(cpType, ShaderType::DEPTH, sceneProject, shaderEntity, "customDepthShader", "custom_depth_shader");
        ImGui::SameLine(); helpMarker("Shader of the shadow maps, and of the depth pre-pass that feeds SSAO and post-process depth while SSR is off (with SSR on they read the G-buffer, which keeps the built-in shader). Fork it when the mesh shader moves vertices or discards fragments, so shadows follow.");
        if (Catalog::isCustomDepthShaderBuildFailed(sceneProject->scene, shaderEntity)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Depth shader failed to build");
        }
    }

    // the fullscreen fade of a camera inside the water, a shader of its own
    if (cpType == ComponentType::WaterComponent) {
        propertyHeader("Underwater Shader");
        drawComponentShaderRow(cpType, ShaderType::UNDERWATER, sceneProject, shaderEntity, "customUnderwaterShader", "custom_underwater_shader");
        ImGui::SameLine(); helpMarker("Shader of the fade a camera below the surface sees over the scene. Needs Underwater on.");
        if (Catalog::isCustomUnderwaterShaderBuildFailed(sceneProject->scene, shaderEntity)) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("Underwater shader failed to build");
        }
    }

    drawShaderUniformRows(cpType, sceneProject, shaderEntity, uniformRows, true);

    endTable();

    if (!uniformRows.members.empty()) {
        // named after the color fork, own or inherited, else the depth or underwater one
        std::string forkShader = *Catalog::getPropertyRef<std::string>(sceneProject->scene, shaderEntity, cpType, "customShader");
        if (forkShader.empty())
            forkShader = sceneProject->scene->getDefaultCustomShader(shaderType);
        if (forkShader.empty() && cpType == ComponentType::MeshComponent)
            forkShader = *Catalog::getPropertyRef<std::string>(sceneProject->scene, shaderEntity, cpType, "customDepthShader");
        if (forkShader.empty() && cpType == ComponentType::WaterComponent)
            forkShader = *Catalog::getPropertyRef<std::string>(sceneProject->scene, shaderEntity, cpType, "customUnderwaterShader");

        shaderUniformsSeparator(forkShader);
        // inherited rows can show the reset arrow
        beginTable(cpType, getShaderUniformsLabelSize(uniformRows.members, uniformRows.inherited != nullptr), "custom_shader_uniforms_table");
        drawShaderUniformRows(cpType, sceneProject, shaderEntity, uniformRows, false);
        endTable();
    }
}

void editor::Properties::drawComponentShaderRow(ComponentType cpType, ShaderType shaderType, SceneProject* sceneProject, Entity entity, const char* propertyName, const std::string& idSuffix){
    const std::string currentShader = *Catalog::getPropertyRef<std::string>(sceneProject->scene, entity, cpType, propertyName);

    auto setShader = [&](const std::string& value) {
        Command* shaderCmd = new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, propertyName, value);
        CommandHandle::get(sceneProject->id)->addCommand(shaderCmd);
    };
    const uint32_t sceneId = sceneProject->id;
    std::string defaultName = sceneProject->scene->getEntityName(entity);
    if (shaderType == ShaderType::DEPTH)
        defaultName += "_depth";
    else if (shaderType == ShaderType::UNDERWATER)
        defaultName += "_underwater";
    auto onFork = [this, sceneId, entity, cpType, shaderType, propertyName, defaultName]() {
        shaderForkDialog.open(project, shaderType, defaultName,
            [this, sceneId, entity, cpType, shaderType, propertyName](const std::filesystem::path& directory,
                                                                     const std::string& name,
                                                                     bool forkIncludes) {
                // The dialog answers frames later, so the scene is resolved again here.
                SceneProject* currentScene = project->getScene(sceneId);
                if (!currentScene || !currentScene->scene) {
                    Backend::getApp().registerAlert("Error", "The scene is no longer available.");
                    return;
                }
                std::string* current = Catalog::getPropertyRef<std::string>(
                    currentScene->scene, entity, cpType, propertyName);
                if (!current || !current->empty()) {
                    Backend::getApp().registerAlert("Error", "The component no longer uses the built-in shader.");
                    return;
                }
                commitShaderFork(sceneId, std::make_unique<ForkShaderCmd>(
                    project, sceneId, entity, cpType, shaderType, directory, name, forkIncludes));
            });
    };

    // only the main shader has a scene default
    drawShaderRowContents(shaderType, currentShader, setShader, onFork, idSuffix,
                          currentShader.empty() ? sceneProject->scene->getDefaultCustomShader(shaderType) : std::string());
}

editor::Properties::ShaderUniformRows editor::Properties::resolveShaderUniformRows(ComponentType cpType, ShaderType shaderType, SceneProject* sceneProject, Entity entity){
    ShaderUniformRows rows;
    Scene* scene = sceneProject->scene;

    std::vector<const CustomUniformBlock*> blocks = Catalog::getShaderUniformBlocks(scene, entity, cpType);
    appendShaderUniformMembers(rows.members, blocks);
    rows.warnings = Catalog::getShaderUniformWarnings(blocks);
    rows.buildFailed = Catalog::isCustomShaderBuildFailed(scene, entity, cpType);

    if (const std::string* customShader = Catalog::getPropertyRef<std::string>(scene, entity, cpType, "customShader"))
        rows.inherited = scene->getInheritedShaderUniforms(shaderType, *customShader);

    return rows;
}

editor::Properties::ShaderUniformRows editor::Properties::resolveSceneShaderUniformRows(SceneProject* sceneProject, ShaderType shaderType, bool& used){
    ShaderUniformRows rows;
    used = false;

    DefaultShaderUse use;
    switch (shaderType) {
        case ShaderType::MESH:   use = findDefaultShaderUse<MeshComponent>(sceneProject->scene, ComponentType::MeshComponent);     break;
        case ShaderType::UI:     use = findDefaultShaderUse<UIComponent>(sceneProject->scene, ComponentType::UIComponent);         break;
        case ShaderType::SKYBOX: use = findDefaultShaderUse<SkyComponent>(sceneProject->scene, ComponentType::SkyComponent);       break;
        case ShaderType::POINTS: use = findDefaultShaderUse<PointsComponent>(sceneProject->scene, ComponentType::PointsComponent); break;
        case ShaderType::LINES:  use = findDefaultShaderUse<LinesComponent>(sceneProject->scene, ComponentType::LinesComponent);   break;
        case ShaderType::WATER:  use = findDefaultShaderUse<WaterComponent>(sceneProject->scene, ComponentType::WaterComponent);   break;
        default: return rows;
    }

    used = use.used;
    appendShaderUniformMembers(rows.members, use.blocks);
    rows.warnings = Catalog::getShaderUniformWarnings(use.blocks);
    rows.buildFailed = !use.used && use.failed;

    return rows;
}

void editor::Properties::drawShaderUniformWarnings(const std::vector<std::string>& warnings){
    for (const std::string& warning : warnings) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
        ImGui::TextWrapped("%s", warning.c_str());
        ImGui::PopStyleColor();
    }
}

editor::Properties::ShaderUniformEdit editor::Properties::drawShaderUniformRow(const ShaderUniform& uniform, const ShaderUniformValues& values,
                                                                              const ShaderUniformValues* inherited, const std::string& idPrefix, Vector4& newValue){
    const std::string& name = uniform.name;
    // RenderSystem rewrites these every frame
    const bool reserved = ShaderUniforms::isReserved(name);
    const bool editable = !reserved && ShaderUniforms::isEditable(uniform);
    const bool hasOwn = ShaderUniforms::has(values, name);

    bool reset = false;
    if (editable && inherited){
        // the arrow drops a value set over the inherited one
        reset = propertyHeader(name, -1, hasOwn, false);
    }else{
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (reserved)
            ImGui::TextDisabled("%s", name.c_str());
        else
            ImGui::Text("%s", name.c_str());
        ImGui::TableSetColumnIndex(1);
    }

    // text rows as tall as the drag rows
    if (reserved){
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Set by the engine");
        ImGui::SameLine();
        helpMarker(name == "resolution"
            ? "Reserved uniform, written every frame with the size of the render target: xy = width and height, zw = 1 / width and 1 / height.\n\nRename it to edit a value of your own here."
            : "Reserved uniform, written every frame with the seconds elapsed since startup.\n\nRename it to edit a value of your own here.");
        return ShaderUniformEdit::NONE;
    }

    if (!editable){
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Not editable");
        ImGui::SameLine();
        helpMarker(uniform.arrayCount > 1
            ? "Array members take no value and stay zero. Declare one scalar or vector member per value instead."
            : "Only float and int scalars and vectors take a value; this member stays zero.");
        return ShaderUniformEdit::NONE;
    }

    int components = 0;
    bool isInt = false;
    switch (uniform.type){
        case ShaderUniformType::FLOAT:  components = 1; break;
        case ShaderUniformType::FLOAT2: components = 2; break;
        case ShaderUniformType::FLOAT3: components = 3; break;
        case ShaderUniformType::FLOAT4: components = 4; break;
        case ShaderUniformType::INT:    components = 1; isInt = true; break;
        case ShaderUniformType::INT2:   components = 2; isInt = true; break;
        case ShaderUniformType::INT3:   components = 3; isInt = true; break;
        case ShaderUniformType::INT4:   components = 4; isInt = true; break;
        default: return ShaderUniformEdit::NONE;
    }

    Vector4 value = ShaderUniforms::get(inherited && !hasOwn ? *inherited : values, name);
    float fields[4] = {value.x, value.y, value.z, value.w};
    bool changed = false;
    std::string uniformId = idPrefix + name;
    ImGui::SetNextItemWidth(-1);
    if (isInt){
        int intFields[4] = {(int)fields[0], (int)fields[1], (int)fields[2], (int)fields[3]};
        changed = ImGui::DragScalarN(uniformId.c_str(), ImGuiDataType_S32, intFields, components, 1.0f);
        for (int c = 0; c < 4; c++)
            fields[c] = (float)intFields[c];
    }else{
        changed = ImGui::DragScalarN(uniformId.c_str(), ImGuiDataType_Float, fields, components, 0.01f);
    }

    if (reset)
        return ShaderUniformEdit::RESET;
    if (!changed)
        return ShaderUniformEdit::NONE;

    newValue = Vector4(fields[0], fields[1], fields[2], fields[3]);
    return ShaderUniformEdit::CHANGED;
}

void editor::Properties::drawShaderUniformRows(ComponentType cpType, SceneProject* sceneProject, Entity entity, const ShaderUniformRows& rows, bool status){
    // the build state and block warnings go under the Shader row
    if (status) {
        if (rows.buildFailed) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            // a failed fork keeps rendering, the runtime falls back to the built-in
            // (members left are the depth fork's, when a mesh has one)
            ImGui::TextDisabled("Shader failed to build");
        }
        drawShaderUniformWarnings(rows.warnings);
        return;
    }

    if (rows.members.empty())
        return;

    ShaderUniformValues* valuesRef = Catalog::getPropertyRef<ShaderUniformValues>(sceneProject->scene, entity, cpType, "shaderUniforms");
    if (!valuesRef)
        return;
    const ShaderUniformValues values = *valuesRef;
    const uint32_t sceneId = sceneProject->id;

    // the engine-written members first
    for (const ShaderUniform& uniform : orderShaderUniforms(rows.members)) {
        Vector4 newValue;
        ShaderUniformEdit edit = drawShaderUniformRow(uniform, values, rows.inherited, "##shader_uniform_", newValue);
        if (edit == ShaderUniformEdit::CHANGED) {
            ShaderUniformValues newValues = values;
            ShaderUniforms::set(newValues, uniform.name, newValue);

            // a drag merges into one undo step, closed when the field lets go
            cmd = new PropertyCmd<ShaderUniformValues>(project, sceneId, entity, cpType, "shaderUniforms", newValues);
            CommandHandle::get(sceneId)->addCommand(cmd);
        } else if (edit == ShaderUniformEdit::RESET) {
            // back to the scene value
            ShaderUniformValues newValues = values;
            ShaderUniforms::remove(newValues, uniform.name);
            CommandHandle::get(sceneId)->addCommandNoMerge(
                new PropertyCmd<ShaderUniformValues>(project, sceneId, entity, cpType, "shaderUniforms", newValues));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && cmd) {
            cmd->setNoMerge();
            cmd = nullptr;
        }
    }
}

void editor::Properties::drawSceneShaderRow(SceneProject* sceneProject, ShaderType shaderType, const char* scenePropertyName, const char* label){
    // Scene default shader: used by every component of this type whose customShader is
    // empty (priority: component shader > scene default > built-in).
    const std::string currentShader = Catalog::getSceneProperty<std::string>(sceneProject->scene, scenePropertyName);

    auto setShader = [&](const std::string& value) {
        Command* shaderCmd = new ScenePropertyCmd<std::string>(project, sceneProject->id, scenePropertyName, value);
        CommandHandle::get(sceneProject->id)->addCommand(shaderCmd);
    };
    const uint32_t sceneId = sceneProject->id;
    const std::string propertyName = scenePropertyName;
    const std::string defaultName = sceneProject->name + " " + label;
    auto onFork = [this, sceneId, shaderType, propertyName, defaultName]() {
        shaderForkDialog.open(project, shaderType, defaultName,
            [this, sceneId, shaderType, propertyName](const std::filesystem::path& directory,
                                                      const std::string& name,
                                                      bool forkIncludes) {
                SceneProject* currentScene = project->getScene(sceneId);
                if (!currentScene || !currentScene->scene) {
                    Backend::getApp().registerAlert("Error", "The scene is no longer available.");
                    return;
                }
                if (!Catalog::getSceneProperty<std::string>(currentScene->scene, propertyName).empty()) {
                    Backend::getApp().registerAlert("Error", "The scene no longer uses the built-in shader for this type.");
                    return;
                }
                commitShaderFork(sceneId, std::make_unique<ForkShaderCmd>(
                    project, currentScene, shaderType, propertyName, directory, name, forkIncludes));
            });
    };

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("%s", label);
    ImGui::TableSetColumnIndex(1);

    drawShaderRowContents(shaderType, currentShader, setShader, onFork, scenePropertyName);
}

void editor::Properties::drawSceneShaderUniforms(SceneProject* sceneProject, ShaderType shaderType, const ShaderUniformRows& rows, bool used, bool status){
    if (status){
        if (used){
            drawShaderUniformWarnings(rows.warnings);
            return;
        }
        // no component renders with the fork: it failed, or nothing inherits it yet
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        if (rows.buildFailed){
            ImGui::TextDisabled("Shader failed to build");
        }else{
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("Uniforms show once a component uses it");
            ImGui::PopStyleColor();
        }
        return;
    }

    const char* propertyName = Catalog::getDefaultShaderUniformsProperty(shaderType);
    const ShaderUniformValues values = Catalog::getSceneProperty<ShaderUniformValues>(sceneProject->scene, propertyName);
    const uint32_t sceneId = sceneProject->id;

    for (const ShaderUniform& uniform : orderShaderUniforms(rows.members)){
        Vector4 newValue;
        if (drawShaderUniformRow(uniform, values, nullptr, "##scene_uniform_", newValue) == ShaderUniformEdit::CHANGED){
            ShaderUniformValues newValues = values;
            ShaderUniforms::set(newValues, uniform.name, newValue);

            // a drag merges into one undo step, closed when the field lets go
            cmd = new ScenePropertyCmd<ShaderUniformValues>(project, sceneId, propertyName, newValues);
            CommandHandle::get(sceneId)->addCommand(cmd);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && cmd){
            cmd->setNoMerge();
            cmd = nullptr;
        }
    }
}

editor::Properties::PostProcessShader editor::Properties::resolvePostProcessShader(const PostProcessPass& pass){
    PostProcessShader resolved;

    uint16_t customId = ShaderPool::registerCustomShader(pass.shader);
    resolved.buildFailed = customId != 0 && ShaderPool::isShaderBuildFailed(ShaderType::POSTPROCESS, 0, customId);
    resolved.shader = ShaderPool::get(ShaderType::POSTPROCESS, 0, customId);

    if (!resolved.buildFailed && resolved.shader && resolved.shader->isCreated()){
        resolved.block.resolve(resolved.shader->shaderData, "u_fs_postParams");
    }

    return resolved;
}

float editor::Properties::getShaderUniformsLabelSize(const std::vector<ShaderUniform>& members, bool resetArrow){
    float width = 0.0f;
    for (const ShaderUniform& uniform : members)
        width = std::max(width, getLabelSize(uniform.name, resetArrow));

    return width;
}

void editor::Properties::drawPostProcessUniforms(uint32_t sceneId, const std::vector<PostProcessPass>& passes, size_t index,
                                                const PostProcessShader& resolved, bool status){
    if (resolved.buildFailed || !resolved.shader || !resolved.shader->isCreated()){
        if (status && !passes[index].shader.empty()){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(1);
            // a failed fork keeps rendering, the runtime falls back to the built-in
            ImGui::TextDisabled("%s", resolved.buildFailed ? "Shader failed to build" : "Building shader...");
        }
        return;
    }

    // block warnings go under the Shader row
    if (status){
        drawShaderUniformWarnings(Catalog::getShaderUniformWarnings({&resolved.block}));
        return;
    }

    // the engine-written members first
    for (const ShaderUniform& uniform : orderShaderUniforms(resolved.block.members)){
        Vector4 newValue;
        if (drawShaderUniformRow(uniform, passes[index].uniforms, nullptr, "##pp_uniform_", newValue) == ShaderUniformEdit::CHANGED){
            std::vector<PostProcessPass> newPasses = passes;
            newPasses[index].setUniform(uniform.name, newValue);

            // a drag merges into one undo step, closed when the field lets go
            cmd = new ScenePropertyCmd<std::vector<PostProcessPass>>(project, sceneId, "post_process", newPasses);
            CommandHandle::get(sceneId)->addCommand(cmd);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && cmd){
            cmd->setNoMerge();
            cmd = nullptr;
        }
    }
}

void editor::Properties::drawScenePostProcess(SceneProject* sceneProject){
    const std::vector<PostProcessPass> passes = Catalog::getSceneProperty<std::vector<PostProcessPass>>(sceneProject->scene, "post_process");
    uint32_t sceneId = sceneProject->id;

    // the chain is one scene property, so every edit here commits a modified copy
    auto setPasses = [this, sceneId](const std::vector<PostProcessPass>& value){
        CommandHandle::get(sceneId)->addCommandNoMerge(
            new ScenePropertyCmd<std::vector<PostProcessPass>>(project, sceneId, "post_process", value));
    };

    ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame;

    for (size_t i = 0; i < passes.size(); i++){
        ImGui::PushID((int)i);

        // a nested header like a script's, the reorder and remove buttons over its right end
        const float indentation = 10.0f;
        ImGui::Indent(indentation);

        const float framePadX = ImGui::GetStyle().FramePadding.x / 4.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x / 2.0f;
        auto buttonWidth = [&](const char* icon){ return ImGui::CalcTextSize(icon).x + framePadX * 2.0f; };
        const float buttonsWidth = buttonWidth(ICON_FA_ARROW_UP) + buttonWidth(ICON_FA_ARROW_DOWN) +
                                   buttonWidth(ICON_FA_TRASH_CAN) + spacing * 2.0f;

        // a narrow panel drops the position, so the name stays clear of the buttons
        const std::string passName = passes[i].shader.empty() ? "Built-in" : shaderForkName(passes[i].shader);
        std::string label = ICON_FA_WAND_MAGIC_SPARKLES " " + passName;
        const std::string position = " [Pass " + std::to_string(i + 1) + "]";
        const float labelRoom = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() - ImGui::GetStyle().FramePadding.x * 3.0f - spacing - buttonsWidth;
        if (ImGui::CalcTextSize((label + position).c_str()).x <= labelRoom)
            label += position;
        // ### keeps the open state when the label changes
        const std::string headerText = label + "###pp_header";

        ImGui::PushStyleColor(ImGuiCol_Header, Theme::Colors::NestedHeader);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::Colors::NestedHeaderHovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::Colors::NestedHeaderActive);
        if (!passes[i].enabled)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);

        // AllowOverlap so the trailing buttons can receive clicks
        const bool headerOpen = ImGui::CollapsingHeader(headerText.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        if (!passes[i].enabled)
            ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const ImVec2 cursorAfterHeader = ImGui::GetCursorPos();

        auto movePass = [&](size_t to){
            std::vector<PostProcessPass> newPasses = passes;
            std::swap(newPasses[i], newPasses[to]);
            setPasses(newPasses);
        };
        bool removed = false;
        auto removePass = [&](){
            std::vector<PostProcessPass> newPasses = passes;
            newPasses.erase(newPasses.begin() + i);
            setPasses(newPasses);
            removed = true;
        };

        // the context menu must run while the header is still the last item
        if (ImGui::BeginPopupContextItem("pp_options_menu")){
            ImGui::TextDisabled("Pass options");
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_ARROW_UP " Move Up", nullptr, false, i > 0))
                movePass(i - 1);
            if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " Move Down", nullptr, false, i + 1 < passes.size()))
                movePass(i + 1);
            if (ImGui::MenuItem(ICON_FA_TRASH " Remove"))
                removePass();
            ImGui::EndPopup();
        }

        if (!removed){
            ImGui::SetCursorScreenPos(ImVec2(
                headerMax.x - buttonsWidth,
                headerMin.y + (headerMax.y - headerMin.y - ImGui::GetFrameHeight()) * 0.5f));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadX, ImGui::GetStyle().FramePadding.y));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, ImGui::GetStyle().ItemSpacing.y));

            ImGui::BeginDisabled(i == 0);
            if (ImGui::Button(ICON_FA_ARROW_UP "##pp_up"))
                movePass(i - 1);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Move the pass earlier in the chain");

            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 == passes.size());
            if (ImGui::Button(ICON_FA_ARROW_DOWN "##pp_down"))
                movePass(i + 1);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Move the pass later in the chain");

            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH_CAN "##pp_remove"))
                removePass();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove the pass");

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        ImGui::SetCursorPos(cursorAfterHeader);
        ImGui::Unindent(indentation);

        if (removed){
            ImGui::PopID();
            break;
        }

        if (!headerOpen){
            ImGui::PopID();
            continue;
        }

        PostProcessShader resolved = resolvePostProcessShader(passes[i]);

        if (ImGui::BeginTable("scene_post_process_pass_table", 2, tableFlags)){
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Shader", false));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Shader");
            ImGui::TableSetColumnIndex(1);

            // before the shader name, like a script's checkbox
            bool enabled = passes[i].enabled;
            if (ImGui::Checkbox("##pp_enabled", &enabled)){
                std::vector<PostProcessPass> newPasses = passes;
                newPasses[i].enabled = enabled;
                setPasses(newPasses);
            }
            ImGui::SameLine();

            auto setShader = [&](const std::string& value){
                std::vector<PostProcessPass> newPasses = passes;
                newPasses[i].shader = value;
                newPasses[i].uniforms.clear(); // another shader has other members
                setPasses(newPasses);
            };
            std::string defaultName = sceneProject->name + " Post " + std::to_string(i + 1);
            auto onFork = [this, sceneId, i, defaultName](){
                shaderForkDialog.open(project, ShaderType::POSTPROCESS, defaultName,
                    [this, sceneId, i](const std::filesystem::path& directory, const std::string& name, bool forkIncludes){
                        SceneProject* currentScene = project->getScene(sceneId);
                        if (!currentScene || !currentScene->scene){
                            Backend::getApp().registerAlert("Error", "The scene is no longer available.");
                            return;
                        }
                        std::vector<PostProcessPass> newPasses = Catalog::getSceneProperty<std::vector<PostProcessPass>>(currentScene->scene, "post_process");
                        if (i >= newPasses.size() || !newPasses[i].shader.empty()){
                            Backend::getApp().registerAlert("Error", "The pass no longer uses the built-in shader.");
                            return;
                        }
                        // the fork writes the files and points the pass at them in one step
                        auto makePropertyCmd = [this, sceneId, i, newPasses](const std::string& forkBase) mutable {
                            newPasses[i].shader = forkBase;
                            return std::make_unique<ScenePropertyCmd<std::vector<PostProcessPass>>>(
                                project, sceneId, "post_process", newPasses);
                        };
                        commitShaderFork(sceneId, std::make_unique<ForkShaderCmd>(
                            project, ShaderType::POSTPROCESS, makePropertyCmd, directory, name, forkIncludes));
                    });
            };

            drawShaderRowContents(ShaderType::POSTPROCESS, passes[i].shader, setShader, onFork, "post_process_" + std::to_string(i));

            drawPostProcessUniforms(sceneId, passes, i, resolved, true);

            ImGui::EndTable();
        }

        // only drawn once the shader is built, so the first width of its column fits the names
        if (!resolved.block.members.empty()){
            shaderUniformsSeparator(passes[i].shader);
            if (ImGui::BeginTable("scene_post_process_uniforms_table", 2, tableFlags)){
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getShaderUniformsLabelSize(resolved.block.members, false));
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                drawPostProcessUniforms(sceneId, passes, i, resolved, false);

                ImGui::EndTable();
            }
        }

        ImGui::PopID();
    }

    std::string addLabel = ICON_FA_PLUS " Add Pass##scene_post_process";
    if (ImGui::Button(addLabel.c_str(), ImVec2(std::max(0.0f, ImGui::GetContentRegionAvail().x), 0))){
        std::vector<PostProcessPass> newPasses = passes;
        newPasses.push_back(PostProcessPass());
        setPasses(newPasses);
    }
}

void editor::Properties::drawShaderRowContents(ShaderType shaderType, const std::string& currentShader,
                                               const std::function<void(const std::string&)>& setShader,
                                               const std::function<void()>& onFork,
                                               const std::string& idSuffix,
                                               const std::string& inheritedShader){
    const std::filesystem::path projectPath = project->getProjectPath();

    // The .vert/.frag entry points either share a base name (stored as "shaders/mesh")
    // or live in separate files (stored as "shaders/mesh.vert|shaders/other.frag").
    const Util::CustomShaderPaths paths = Util::resolveCustomShaderPaths(currentShader);
    const bool hasCustom = !currentShader.empty();
    const bool separate = Util::isSeparateCustomShader(currentShader);
    const bool vertExists = hasCustom && std::filesystem::exists(projectPath / paths.vert);
    const bool fragExists = hasCustom && std::filesystem::exists(projectPath / paths.frag);
    const std::string popupName = "Edit Shader Files##" + idSuffix;

    auto openRelative = [&](const std::string& rel) {
        if (CodeEditor* codeEditor = Backend::getApp().getCodeEditor())
            codeEditor->openFile((projectPath / rel).string(), true);
    };
    // Both .vert and .frag (sharing a base name) are required; store the base path.
    auto useExistingBase = [&](const std::string& base) {
        bool hasVert = std::filesystem::exists(projectPath / (base + ".vert"));
        bool hasFrag = std::filesystem::exists(projectPath / (base + ".frag"));
        if (!hasVert || !hasFrag) {
            Backend::getApp().registerAlert("Error", "A custom shader needs both a .vert and a .frag file with the same base name.");
            return;
        }
        setShader(base);
    };

    if (!hasCustom && !inheritedShader.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s (scene default)", inheritedShader.c_str());
        ImGui::PopStyleColor();
    }else if (!hasCustom)
        ImGui::TextDisabled("Built-in");
    else if (separate)
        ImGui::TextWrapped("%s, %s", paths.vert.c_str(), paths.frag.c_str());
    else
        ImGui::TextWrapped("%s", currentShader.c_str());

    // Icon row mirrors the ScriptComponent open-file buttons: fork, edit files,
    // open .vert, open .frag, reset to built-in.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 3.0, ImGui::GetStyle().FramePadding.y / 2.0));

    ImGui::BeginDisabled(hasCustom);
    if (ImGui::Button((std::string(ICON_FA_CODE_FORK "##fork_") + idSuffix).c_str()) && onFork)
        onFork();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Fork the built-in shader into this project");
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button((std::string(ICON_FA_PEN_TO_SQUARE "##edit_") + idSuffix).c_str()))
        ImGui::OpenPopup(popupName.c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick the .vert and .frag files (they can have different names)");

    ImGui::SameLine();
    ImGui::BeginDisabled(!vertExists);
    if (ImGui::Button((std::string(ICON_FA_FILE_CODE "##open_vert_") + idSuffix).c_str()))
        openRelative(paths.vert);
    if (ImGui::IsItemHovered()) {
        if (vertExists)
            ImGui::SetTooltip("Open %s", paths.vert.c_str());
        else if (hasCustom)
            ImGui::SetTooltip("Vertex shader file not found");
        else
            ImGui::SetTooltip("Open vertex shader");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!fragExists);
    if (ImGui::Button((std::string(ICON_FA_FILE_LINES "##open_frag_") + idSuffix).c_str()))
        openRelative(paths.frag);
    if (ImGui::IsItemHovered()) {
        if (fragExists)
            ImGui::SetTooltip("Open %s", paths.frag.c_str());
        else if (hasCustom)
            ImGui::SetTooltip("Fragment shader file not found");
        else
            ImGui::SetTooltip("Open fragment shader");
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!hasCustom);
    if (ImGui::Button((std::string(ICON_FA_XMARK "##reset_") + idSuffix).c_str()))
        setShader("");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Reset to the built-in shader");
    ImGui::EndDisabled();

    ImGui::PopStyleVar();

    drawShaderFilesPopup(popupName, setShader, paths.vert, paths.frag);

    // Drag-drop an existing .vert/.frag from the resources window.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
            std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
            if (!receivedStrings.empty() && Util::isShaderFile(receivedStrings[0])) {
                std::error_code ec;
                std::filesystem::path relPath = std::filesystem::relative(receivedStrings[0], projectPath, ec);
                // store the base path without the .vert/.frag extension
                if (!ec && !relPath.empty() && *relPath.begin() != "..") {
                    useExistingBase((relPath.parent_path() / relPath.stem()).generic_string());
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}

// Popup that lets the user pick the .vert and .frag entry points independently (so they
// can use differently-named files). Applying stores the result via Util::makeCustomShader,
// collapsing to the shared-base shorthand when both files share a <base>.vert/.frag.
// popupName must be unique within the window (several shader rows can share it).
void editor::Properties::drawShaderFilesPopup(const std::string& popupName, const std::function<void(const std::string&)>& setShader, const std::string& currentVert, const std::string& currentFrag){
    const std::filesystem::path projectPath = project->getProjectPath();

    ImGui::SetNextWindowSizeConstraints(ImVec2(25 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopup(popupName.c_str()))
        return;

    static std::string vertEdit;
    static std::string fragEdit;
    if (ImGui::IsWindowAppearing()) {
        vertEdit = currentVert;
        fragEdit = currentFrag;
    }

    ImGui::Text("Edit Shader Files");
    ImGui::Separator();

    float secondColSize = 18 * ImGui::GetFontSize();
    float openBtnWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float clearBtnWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    float btnSpacing = ImGui::GetStyle().ItemSpacing.x;

    bool changed = false;

    // Picks a shader file of the required extension into the given slot.
    auto pickSlot = [&](std::string& slotRel, const char* requiredExt) {
        std::filesystem::path startDir = projectPath;
        const std::string nearby = !slotRel.empty() ? slotRel
                                  : (!currentVert.empty() ? currentVert : currentFrag);
        if (!nearby.empty()) {
            const std::filesystem::path parent = (projectPath / nearby).parent_path();
            if (std::filesystem::is_directory(parent))
                startDir = parent;
        }
        std::string selected = FileDialogs::openFileDialog(startDir.string(), FILE_DIALOG_SHADER);
        if (selected.empty())
            return;

        std::string ext = std::filesystem::path(selected).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != requiredExt) {
            Backend::getApp().registerAlert("Error", std::string("Select a ") + requiredExt + " file.");
            return;
        }
        std::error_code ec;
        std::filesystem::path rel = std::filesystem::relative(selected, projectPath, ec);
        if (ec || rel.empty() || *rel.begin() == "..") {
            Backend::getApp().registerAlert("Error", "Shader file must be inside the project directory.");
            return;
        }
        slotRel = rel.generic_string();
        changed = true;
    };

    auto slotRow = [&](const char* label, std::string& slotRel, const char* requiredExt, const char* idSuffix) {
        propertyHeader(label, secondColSize);
        ImGui::SetNextItemWidth(secondColSize - openBtnWidth - clearBtnWidth - btnSpacing * 2.0f);

        char buffer[256];
        strncpy(buffer, slotRel.c_str(), sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::InputText((std::string("##shader_slot_") + idSuffix).c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
        if (!slotRel.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", slotRel.c_str());

        ImGui::SameLine();
        if (ImGui::Button((std::string(ICON_FA_FOLDER_OPEN "##pick_") + idSuffix).c_str()))
            pickSlot(slotRel, requiredExt);
        ImGui::SameLine();
        ImGui::BeginDisabled(slotRel.empty());
        if (ImGui::Button((std::string(ICON_FA_XMARK "##clear_") + idSuffix).c_str())) {
            slotRel.clear();
            changed = true;
        }
        ImGui::EndDisabled();
    };

    // the popup is its own window, so a fixed table name cannot collide
    ImGui::PushItemWidth(-1);
    ImGui::BeginTable("table_edit_shader_files", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Fragment File"));
    ImGui::TableSetupColumn("Value");
    slotRow("Vertex File", vertEdit, ".vert", "vert");
    slotRow("Fragment File", fragEdit, ".frag", "frag");
    endTable();

    bool bothEmpty = vertEdit.empty() && fragEdit.empty();
    bool bothSet = !vertEdit.empty() && !fragEdit.empty();
    if (!bothEmpty && !bothSet)
        ImGui::TextDisabled("Set both a vertex and a fragment file.");

    // A shader needs both entry points, so a half-filled pair waits for the other slot.
    if (changed && (bothEmpty || bothSet)) {
        std::string newValue = bothEmpty ? std::string() : Util::makeCustomShader(vertEdit, fragEdit);
        std::string currentValue = (currentVert.empty() || currentFrag.empty()) ? std::string() : Util::makeCustomShader(currentVert, currentFrag);
        if (newValue != currentValue) {
            setShader(newValue);
        }
    }

    ImGui::EndPopup();
}

void editor::Properties::drawMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Receive Shadows"));

    // Static variables for shape parameters
    static ShapeParameters shapeParams;

    // Add New Geometry property row
    propertyHeader("Geometry");
    if (ImGui::Button("Create Shape")){
        ImGui::OpenPopup("menusettings_shape_geometry");

        updateShapePreview(shapeParams);
    }

    // Geometry creation popup
    ImGui::SetNextWindowSizeConstraints(ImVec2(17 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_shape_geometry")){
        ImGui::Text("Create Shape");
        ImGui::Separator();

        const char* geometryTypes[] = { "Plane", "Box", "Sphere", "Cylinder", "Capsule", "Torus", "Wall" };

        beginTable(cpType, getLabelSize("Geometry Type"), "geometry_popup");

        // Geometry type selection
        propertyHeader("Geometry Type");

        float secondColSize = 8 * ImGui::GetFontSize();
        bool updatedPreview = false;

        Texture texRender = shapePreviewRender.getTexture();
        Widgets::image(Backend::getImGuiTexture(texRender.getRender()), ImVec2(secondColSize, secondColSize));
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##geometry_type", &shapeParams.geometryType, geometryTypes, IM_ARRAYSIZE(geometryTypes))) {
            updatedPreview = true;
        }

        // Show parameters based on selected geometry type
        switch (shapeParams.geometryType) {
            case 0: // Plane
                propertyHeader("Width", secondColSize);
                if (ImGui::DragFloat("##plane_width", &shapeParams.planeWidth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Depth", secondColSize);
                if (ImGui::DragFloat("##plane_depth", &shapeParams.planeDepth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Tiles", secondColSize);
                if (ImGui::DragInt("##plane_tiles", (int*)&shapeParams.planeTiles, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 1: // Box
                propertyHeader("Width", secondColSize);
                if (ImGui::DragFloat("##box_width", &shapeParams.boxWidth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##box_height", &shapeParams.boxHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Depth", secondColSize);
                if (ImGui::DragFloat("##box_depth", &shapeParams.boxDepth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Tiles", secondColSize);
                if (ImGui::DragInt("##box_tiles", (int*)&shapeParams.boxTiles, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 2: // Sphere
                propertyHeader("Radius", secondColSize);
                if (ImGui::DragFloat("##sphere_radius", &shapeParams.sphereRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Slices", secondColSize);
                if (ImGui::DragInt("##sphere_slices", (int*)&shapeParams.sphereSlices, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Stacks", secondColSize);
                if (ImGui::DragInt("##sphere_stacks", (int*)&shapeParams.sphereStacks, 1, 3, 100)) {
                    updatedPreview = true;
                }
                break;

            case 3: // Cylinder
                propertyHeader("Base Radius", secondColSize);
                if (ImGui::DragFloat("##cylinder_base_radius", &shapeParams.cylinderBaseRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Top Radius", secondColSize);
                if (ImGui::DragFloat("##cylinder_top_radius", &shapeParams.cylinderTopRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##cylinder_height", &shapeParams.cylinderHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Slices", secondColSize);
                if (ImGui::DragInt("##cylinder_slices", (int*)&shapeParams.cylinderSlices, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Stacks", secondColSize);
                if (ImGui::DragInt("##cylinder_stacks", (int*)&shapeParams.cylinderStacks, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 4: // Capsule
                propertyHeader("Base Radius", secondColSize);
                if (ImGui::DragFloat("##capsule_base_radius", &shapeParams.capsuleBaseRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Top Radius", secondColSize);
                if (ImGui::DragFloat("##capsule_top_radius", &shapeParams.capsuleTopRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##capsule_height", &shapeParams.capsuleHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Slices", secondColSize);
                if (ImGui::DragInt("##capsule_slices", (int*)&shapeParams.capsuleSlices, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Stacks", secondColSize);
                if (ImGui::DragInt("##capsule_stacks", (int*)&shapeParams.capsuleStacks, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;

            case 5: // Torus
                propertyHeader("Radius", secondColSize);
                if (ImGui::DragFloat("##torus_radius", &shapeParams.torusRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Ring Radius", secondColSize);
                if (ImGui::DragFloat("##torus_ring_radius", &shapeParams.torusRingRadius, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Sides", secondColSize);
                if (ImGui::DragInt("##torus_sides", (int*)&shapeParams.torusSides, 1, 3, 100)) {
                    updatedPreview = true;
                }

                propertyHeader("Rings", secondColSize);
                if (ImGui::DragInt("##torus_rings", (int*)&shapeParams.torusRings, 1, 3, 100)) {
                    updatedPreview = true;
                }
                break;

            case 6: // Wall
                propertyHeader("Width", secondColSize);
                if (ImGui::DragFloat("##wall_width", &shapeParams.wallWidth, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Height", secondColSize);
                if (ImGui::DragFloat("##wall_height", &shapeParams.wallHeight, 0.1f, 0.1f, 100.0f, "%.2f")) {
                    updatedPreview = true;
                }

                propertyHeader("Tiles", secondColSize);
                if (ImGui::DragInt("##wall_tiles", (int*)&shapeParams.wallTiles, 1, 1, 100)) {
                    updatedPreview = true;
                }
                break;
        }

        if (updatedPreview){
            updateShapePreview(shapeParams);
        }

        endTable();

        ImGui::Separator();

        // Create geometry button
        if (ImGui::Button("Apply", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            for (Entity& entity : entities) {
                std::shared_ptr<doriax::MeshSystem> meshSys = sceneProject->scene->getSystem<MeshSystem>();
                MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);

                updateMeshShape(meshComp, meshSys.get(), shapeParams);

                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new MeshChangeCmd(project, sceneProject->id, entities[0], meshComp));
            }

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    RowSettings lightsSettings;
    lightsSettings.help = "Light this mesh with the scene lights. When off, it is unlit and shows its colors as they are.";
    propertyRow(RowPropertyType::Bool, cpType, "receiveLights", "Receive Lights", sceneProject, entities, lightsSettings);

    RowSettings iblSettings;
    iblSettings.help = "Light this mesh with image-based lighting, from the scene's Sky environment and from Reflection Probes. Sky lighting requires a Sky entity with a texture.";
    propertyRow(RowPropertyType::Bool, cpType, "receiveIBL", "Receive IBL", sceneProject, entities, iblSettings);

    propertyRow(RowPropertyType::Bool, cpType, "castShadows", "Cast Shadows", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "receiveShadows", "Receive Shadows", sceneProject, entities);

    RowSettings lodSettings;
    lodSettings.help = "Let this mesh use simplified detail levels while the scene's Mesh Detail (LOD) is on. Turn off to always keep it at full detail.";
    propertyRow(RowPropertyType::Bool, cpType, "lodEnabled", "Detail Levels", sceneProject, entities, lodSettings);
    if (sceneProject->scene->isMeshLodEnabled() && sceneProject->scene->getComponent<MeshComponent>(entities[0]).lodEnabled) {
        RowSettings lodBiasSettings;
        lodBiasSettings.help = "Scales how far detail is kept: above 1 keeps it longer, below 1 drops it sooner.";
        lodBiasSettings.secondColSize = 6 * ImGui::GetFontSize();
        propertyRow(RowPropertyType::FloatPositive, cpType, "lodBias", "Detail Bias", sceneProject, entities, lodBiasSettings);
    }

    RowSettings reflectionProbeSettings;
    reflectionProbeSettings.help = "Draw this mesh into Reflection Probe captures. Turn off to keep an object out of its own reflection.";
    propertyRow(RowPropertyType::Bool, cpType, "renderInReflectionProbes", "Render in Probes", sceneProject, entities, reflectionProbeSettings);

    RowSettings transparencySettings;
    transparencySettings.help = "Just for render ordering";
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "transparent", "Transparent", "autoTransparency", "Auto Transparency", sceneProject, entities, transparencySettings);

    endTable();

    drawCustomShaderRow(cpType, ShaderType::MESH, sceneProject, entities);

    unsigned int numSubmeshes = sceneProject->scene->getComponent<MeshComponent>(entities[0]).numSubmeshes;
    for (Entity& entity : entities){
        numSubmeshes = std::min(numSubmeshes, sceneProject->scene->getComponent<MeshComponent>(entity).numSubmeshes);
    }
    bool hasGeneratedSubmeshes = false;
    for (const Entity& entity : entities) {
        const MeshComponent& checkMesh = sceneProject->scene->getComponent<MeshComponent>(entity);
        for (unsigned int gs = 0; gs < checkMesh.numSubmeshes; gs++) {
            if (checkMesh.submeshes[gs].generated) {
                hasGeneratedSubmeshes = true;
                break;
            }
        }
        if (hasGeneratedSubmeshes) break;
    }

    if (!hasGeneratedSubmeshes && ImGui::Button(ICON_FA_PLUS " Add Submesh", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
        for (Entity& entity : entities) {
            MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);
            const unsigned int newSubmeshIndex = meshComp.numSubmeshes;
            meshComp.numSubmeshes++;
            meshComp.submeshes[newSubmeshIndex] = Submesh();
            meshComp.submeshes[newSubmeshIndex].needUpdateTexture = true;

            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(
                new MeshChangeCmd(project, sceneProject->id, entity, meshComp));
        }
    }

    for (int s = 0; s < numSubmeshes; s++){
        ImGui::PushID(s);
        bool submeshGenerated = false;
        for (const Entity& entity : entities) {
            const MeshComponent& checkMesh = sceneProject->scene->getComponent<MeshComponent>(entity);
            if ((unsigned int)s < checkMesh.numSubmeshes && checkMesh.submeshes[s].generated) {
                submeshGenerated = true;
                break;
            }
        }
        std::string submeshLabel = submeshGenerated ? ("Submesh " + std::to_string(s) + " (Internal)") : ("Submesh " + std::to_string(s));
        ImGui::SeparatorText(submeshLabel.c_str());

        ImVec2 separatorMin = ImGui::GetItemRectMin();
        ImVec2 separatorMax = ImGui::GetItemRectMax();
        ImVec2 cursorAfterSeparator = ImGui::GetCursorPos();

        {
            float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
            float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
            ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);

            float buttonX = separatorMax.x - deleteButtonSize.x;
            float buttonY = separatorMin.y + (separatorMax.y - separatorMin.y - ImGui::GetFrameHeight()) * 0.5f;

            ImDrawList* separatorDrawList = ImGui::GetWindowDrawList();
            float separatorGap = ImGui::GetStyle().ItemSpacing.x;
            separatorDrawList->AddRectFilled(
                ImVec2(buttonX - separatorGap, separatorMin.y),
                separatorMax,
                ImGui::GetColorU32(ImGuiCol_WindowBg));

            ImGui::SetCursorScreenPos(ImVec2(buttonX, buttonY));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            if (ImGui::Button((std::string(ICON_FA_TRASH_CAN) + "##delete_submesh_" + std::to_string(s)).c_str(), deleteButtonSize)) {
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                ImGui::SetCursorPos(cursorAfterSeparator);

                for (Entity& entity : entities) {
                    MeshComponent meshComp = sceneProject->scene->getComponent<MeshComponent>(entity);
                    if ((unsigned int)s >= meshComp.numSubmeshes) {
                        continue;
                    }

                    for (unsigned int j = (unsigned int)s; j + 1 < meshComp.numSubmeshes; j++) {
                        meshComp.submeshes[j] = meshComp.submeshes[j + 1];
                    }

                    if (meshComp.numSubmeshes > 0) {
                        meshComp.numSubmeshes--;
                        meshComp.submeshes[meshComp.numSubmeshes] = Submesh();
                    }

                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(
                        new MeshChangeCmd(project, sceneProject->id, entity, meshComp));
                }

                ImGui::PopID();
                return;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove submesh");
            }
        }
        ImGui::SetCursorPos(cursorAfterSeparator);
        if (submeshGenerated) ImGui::BeginDisabled(true);

        beginTable(cpType, getLabelSize("Material"), "submeshes_material");

        bool materialOpened = false;
        if (propertyRow(RowPropertyType::Material, cpType, "submeshes["+std::to_string(s)+"].material", "Material", sceneProject, entities)){
            materialOpened = true;
        }
        endTable();

        if (materialOpened){
            RowSettings settingsFactor;
            settingsFactor.stepSize = 0.01f;
            settingsFactor.secondColSize = 4 * ImGui::GetFontSize();
            settingsFactor.child = true;

            RowSettings settingsMaterial;
            settingsMaterial.child = true;
            settingsMaterial.onValueChanged = [this, sceneProject, entities, s]() {
                for (const Entity& entity : entities) {
                    MeshComponent* mesh = sceneProject->scene->findComponent<MeshComponent>(entity);
                    if (!mesh || s >= mesh->numSubmeshes) {
                        continue;
                    }

                    Material& material = mesh->submeshes[s].material;
                    if (material.name.empty()) {
                        continue;
                    }

                    std::string relativePath = std::filesystem::path(material.name).lexically_normal().generic_string();
                    markMaterialDirty(sceneProject->id, entity, s, relativePath);
                }
            };

            settingsFactor.onValueChanged = settingsMaterial.onValueChanged;

            RowSettings settingsAlphaMode = settingsMaterial;
            settingsAlphaMode.enumEntries = &entriesMaterialAlphaMode;

            beginTable(cpType, getLabelSize("Met. Roug. Texture"), "submeshes_material_options");
            propertyRow(RowPropertyType::Color4L, cpType, "submeshes["+std::to_string(s)+"].material.baseColorFactor", "Base Color", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.baseColorTexture", "Base Texture", sceneProject, entities,settingsMaterial);
            propertyRow(RowPropertyType::Enum, cpType, "submeshes["+std::to_string(s)+"].material.alphaMode", "Alpha Mode", sceneProject, entities, settingsAlphaMode);
            propertyRow(RowPropertyType::Float_0_1, cpType, "submeshes["+std::to_string(s)+"].material.alphaCutoff", "Alpha Cutoff", sceneProject, entities, settingsFactor);
            propertyRow(RowPropertyType::Float_0_1, cpType, "submeshes["+std::to_string(s)+"].material.foliageTransmission", "Leaf Transmission", sceneProject, entities, settingsFactor);
            propertyRow(RowPropertyType::Float_0_1, cpType, "submeshes["+std::to_string(s)+"].material.metallicFactor", "Metallic Factor", sceneProject, entities, settingsFactor);
            propertyRow(RowPropertyType::Float_0_1, cpType, "submeshes["+std::to_string(s)+"].material.roughnessFactor", "Roughness Factor", sceneProject, entities, settingsFactor);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.metallicRoughnessTexture", "Met. Roug. Texture", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Color3L, cpType, "submeshes["+std::to_string(s)+"].material.emissiveFactor", "Emissive Factor", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.emissiveTexture", "Emissive Texture", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.occlusionTexture", "Occlusion Texture", sceneProject, entities, settingsMaterial);
            propertyRow(RowPropertyType::Texture, cpType, "submeshes["+std::to_string(s)+"].material.normalTexture", "Normal Texture", sceneProject, entities, settingsMaterial);
            endTable();
        }

        beginTable(cpType, getLabelSize("Texture Shadow"), "submeshes_settings");
        RowSettings settingsPrimitive;
        settingsPrimitive.enumEntries = &entriesPrimitiveType;

        propertyRow(RowPropertyType::Bool, cpType, "submeshes["+std::to_string(s)+"].faceCulling", "Face Culling", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "submeshes["+std::to_string(s)+"].textureShadow", "Texture Shadow", sceneProject, entities);
        propertyRow(RowPropertyType::Enum, cpType, "submeshes["+std::to_string(s)+"].primitiveType", "Primitive", sceneProject, entities, settingsPrimitive);
        RowSettings settingsTextureRect;
        settingsTextureRect.showColors = false;
        propertyRow(RowPropertyType::Vector4, cpType, "submeshes["+std::to_string(s)+"].textureRect", "Texture Rect", sceneProject, entities, settingsTextureRect);

        endTable();

        if (submeshGenerated) ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void editor::Properties::drawModelComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Model File"));

    propertyHeader("Model File");

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ModelComponent& model = sceneProject->scene->getComponent<ModelComponent>(entity);

        std::string currentPath = model.filename;
        std::string displayName = currentPath.empty() ? "< Not set >" : std::filesystem::path(currentPath).filename().string();

        ImGui::BeginGroup();

        float thumbSize = ImGui::GetFrameHeight() * 3;
        Texture* thumbTexture = findThumbnail(currentPath);
        if (thumbTexture) {
            ImU32 border_col = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            drawImageWithBorderAndRounding(thumbTexture, ImVec2(thumbSize, thumbSize), ImGui::GetStyle().FrameRounding, border_col);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                Widgets::image(Backend::getImGuiTexture(thumbTexture->getRender()), ImVec2(thumbTexture->getWidth(), thumbTexture->getHeight()));
                ImGui::EndTooltip();
            }
        }

        float availWidth = ImGui::GetContentRegionAvail().x;
        float buttonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);

        ImGui::BeginChild("modelfilename", ImVec2(availWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x, ImGui::GetFrameHeight()),
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
        float childAvail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(childAvail - textWidth - 2);
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (currentPath.empty())
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", displayName.c_str());
        if (currentPath.empty())
            ImGui::PopStyleColor();
        ImGui::EndChild();
        if (!currentPath.empty()){
            ImGui::SetItemTooltip("%s", currentPath.c_str());
        }

        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##model_load")) {
            std::string path = editor::FileDialogs::openFileDialog(project->getAssetsPath().string(), FILE_DIALOG_MODEL);
            if (!path.empty()) {
                std::filesystem::path assetsPath = project->getAssetsPath();
                std::filesystem::path filePath = std::filesystem::absolute(path);

                std::error_code ec;
                auto relative = std::filesystem::relative(filePath, assetsPath, ec);
                if (ec || relative.empty() || *relative.begin() == "..") {
                    ImGui::OpenPopup("Model Import Error");
                }else{
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new ModelLoadCmd(project, sceneProject->id, entity, relative.string()));
                }
            }
        }

        if (ImGui::BeginPopupModal("Model Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be within the assets directory.");
            ImGui::Separator();
            float bw = 120;
            float ww = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((ww - bw) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::EndGroup();

        // Drag and drop model files from Resources
        if (sceneProject && sceneProject->playState == ScenePlayState::STOPPED) {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
                    std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
                    if (!receivedStrings.empty()) {
                        const bool insideAssets = project->isInsideAssetsPath(receivedStrings[0]);
                        const std::string droppedRelativePath = droppedAssetPath(project, receivedStrings[0], insideAssets);
                        if (Util::isModelFile(droppedRelativePath)) {
                            if (insideAssets) {
                                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new ModelLoadCmd(project, sceneProject->id, entity, droppedRelativePath));
                            } else {
                                Backend::getApp().registerOutsideAssetsAlert(receivedStrings[0]);
                            }
                            ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

    }else{
        ImGui::TextDisabled("Select single entity");
    }

    propertyRow(RowPropertyType::LocalEntity, cpType, "skeleton", "Skeleton", sceneProject, entities);

    endTable();

    if (entities.size() != 1) {
        ImGui::SeparatorText("Animations");
        ImGui::TextDisabled("Select a single entity to edit animations");
        return;
    }

    ModelComponent& model = sceneProject->scene->getComponent<ModelComponent>(entities[0]);

    ImGui::SeparatorText("Animations");

    beginTable(cpType, getLabelSize("Animation 00"), "model_animations_header");
    propertyHeader("Animations", -1, false, false);
    ImGui::Text("%zu", model.animations.size());
    if (drawSummaryAddButton("Add Animation##model_animations_add")) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities) {
            if (ModelComponent* modelComp = sceneProject->scene->findComponent<ModelComponent>(entity)) {
                std::vector<Entity> newAnimations = modelComp->animations;
                newAnimations.push_back(NULL_ENTITY);
                multiCmd->addPropertyCmd<std::vector<Entity>>(project, sceneProject->id, entity, cpType, "animations", newAnimations, nullptr);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    endTable();

    if (model.animations.empty()) {
        return;
    }

    const float animationFirstColSize = getLabelSize("Animation 00");
    const float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
    const float deleteButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + clearButtonFramePadding * 2.0f;
    const float rowSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float valueColWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - animationFirstColSize - ImGui::GetStyle().CellPadding.x * 2.0f);

    beginTable(cpType, animationFirstColSize, "model_animations");

    for (size_t i = 0; i < model.animations.size(); i++) {
        ImGui::PushID((int)i);

        std::string propId = "animations[" + std::to_string(i) + "]";
        Entity animEntity = model.animations[i];
        std::string animationLabel = "Animation " + std::to_string(i);
        if (animEntity != NULL_ENTITY && sceneProject->scene->isEntityCreated(animEntity)) {
            if (sceneProject->scene->findComponent<AnimationComponent>(animEntity)) {
                std::string entityName = sceneProject->scene->getEntityName(animEntity);
                if (!entityName.empty()) {
                    animationLabel = entityName;
                }
            }
        }

        RowSettings settingsAnimation;
        settingsAnimation.entityFilter.set(sceneProject->scene->getComponentId<AnimationComponent>());
        settingsAnimation.secondColSize = std::max(0.0f, valueColWidth - deleteButtonWidth - rowSpacing);

        propertyRow(RowPropertyType::LocalEntity, cpType, propId, animationLabel, sceneProject, entities, settingsAnimation);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        const size_t animationIndex = i;
        if (ImGui::Button((std::string(ICON_FA_TRASH_CAN) + "##remove_animation").c_str(), ImVec2(deleteButtonWidth, 0))) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (Entity entity : entities) {
                if (ModelComponent* modelComp = sceneProject->scene->findComponent<ModelComponent>(entity)) {
                    if (animationIndex < modelComp->animations.size()) {
                        std::vector<Entity> newAnimations = modelComp->animations;
                        newAnimations.erase(newAnimations.begin() + (long int)animationIndex);
                        multiCmd->addPropertyCmd<std::vector<Entity>>(project, sceneProject->id, entity, cpType, "animations", newAnimations, nullptr);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove animation");
        }

        ImGui::PopID();
    }

    endTable();
}

void editor::Properties::drawUIComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Texture"));
    propertyRow(RowPropertyType::Color4L, cpType, "color", "Color", sceneProject, entities);
    propertyRow(RowPropertyType::Texture, cpType, "texture", "Texture", sceneProject, entities);
    endTable();

    drawCustomShaderRow(cpType, ShaderType::UI, sceneProject, entities);
}

void editor::Properties::drawButtonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settings;
    settings.onValueChanged = [sceneProject, entities](){
        for (const Entity& entity : entities){
            if (ButtonComponent* button = sceneProject->scene->findComponent<ButtonComponent>(entity)){
                button->needUpdateButton = true;
            }
        }
    };

    RowSettings settingsScale;
    settingsScale.stepSize = 0.01f;
    settingsScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsScale.onValueChanged = settings.onValueChanged;

    beginTable(cpType, getLabelSize("Texture Disabled"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "label", "Label", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Bool, cpType, "disabled", "Disabled", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "textureNormal", "Texture Normal", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "textureHovered", "Texture Hovered", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "texturePressed", "Texture Pressed", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Texture, cpType, "textureDisabled", "Texture Disabled", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorNormal", "Color Normal", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorHovered", "Color Hovered", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorPressed", "Color Pressed", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "colorDisabled", "Color Disabled", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Float, cpType, "scaleHovered", "Scale Hovered", sceneProject, entities, settingsScale);
    propertyRow(RowPropertyType::Float, cpType, "scalePressed", "Scale Pressed", sceneProject, entities, settingsScale);
    propertyRow(RowPropertyType::FloatPositive, cpType, "transitionTime", "Transition Time", sceneProject, entities, settingsScale);
    endTable();
}

void editor::Properties::drawScrollbarComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    static std::vector<editor::EnumEntry> entriesScrollbarType = {
        { (int)ScrollbarType::VERTICAL, "Vertical" },
        { (int)ScrollbarType::HORIZONTAL, "Horizontal" }
    };

    RowSettings settings;
    settings.onValueChanged = [sceneProject, entities](){
        for (const Entity& entity : entities){
            if (ScrollbarComponent* scrollbar = sceneProject->scene->findComponent<ScrollbarComponent>(entity)){
                scrollbar->needUpdateScrollbar = true;
            }
        }
    };

    RowSettings settingsEnum;
    settingsEnum.enumEntries = &entriesScrollbarType;
    settingsEnum.onValueChanged = settings.onValueChanged;

    RowSettings settingsFloat;
    settingsFloat.stepSize = 0.01f;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();
    settingsFloat.onValueChanged = settings.onValueChanged;

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();
    settingsInt.onValueChanged = settings.onValueChanged;

    beginTable(cpType, getLabelSize("Bar Size"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "bar", "Bar", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsEnum);
    propertyRow(RowPropertyType::Float_0_1, cpType, "barSize", "Bar Size", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float_0_1, cpType, "step", "Step", sceneProject, entities, settingsFloat);
    endTable();

    ImGui::SeparatorText("Bar margin");
    beginTable(cpType, getLabelSize("Margin Bottom"), "scrollbar_bar_margin");
    propertyRow(RowPropertyType::Int, cpType, "barMarginLeft", "Margin Left", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "barMarginRight", "Margin Right", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "barMarginTop", "Margin Top", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "barMarginBottom", "Margin Bottom", sceneProject, entities, settingsInt);
    endTable();
}

void editor::Properties::drawProgressbarComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    static std::vector<editor::EnumEntry> entriesProgressbarType = {
        { (int)ProgressbarType::VERTICAL, "Vertical" },
        { (int)ProgressbarType::HORIZONTAL, "Horizontal" }
    };

    RowSettings settings;
    settings.onValueChanged = [sceneProject, entities](){
        for (const Entity& entity : entities){
            if (ProgressbarComponent* progressbar = sceneProject->scene->findComponent<ProgressbarComponent>(entity)){
                progressbar->needUpdateProgressbar = true;
            }
        }
    };

    RowSettings settingsEnum;
    settingsEnum.enumEntries = &entriesProgressbarType;
    settingsEnum.onValueChanged = settings.onValueChanged;

    RowSettings settingsFloat;
    settingsFloat.stepSize = 0.01f;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();
    settingsFloat.onValueChanged = settings.onValueChanged;

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();
    settingsInt.onValueChanged = settings.onValueChanged;

    beginTable(cpType, getLabelSize("Value"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "fill", "Fill", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsEnum);
    propertyRow(RowPropertyType::Float_0_1, cpType, "value", "Value", sceneProject, entities, settingsFloat);
    endTable();

    ImGui::SeparatorText("Fill margin");
    beginTable(cpType, getLabelSize("Margin Bottom"), "progressbar_fill_margin");
    propertyRow(RowPropertyType::Int, cpType, "fillMarginLeft", "Margin Left", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "fillMarginRight", "Margin Right", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "fillMarginTop", "Margin Top", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "fillMarginBottom", "Margin Bottom", sceneProject, entities, settingsInt);
    endTable();
}

void editor::Properties::drawTextEditComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settings;
    settings.onValueChanged = [sceneProject, entities](){
        for (const Entity& entity : entities){
            if (TextEditComponent* textedit = sceneProject->scene->findComponent<TextEditComponent>(entity)){
                textedit->needUpdateTextEdit = true;
            }
        }
    };

    RowSettings settingsFloat;
    settingsFloat.onValueChanged = settings.onValueChanged;

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();
    settingsInt.onValueChanged = settings.onValueChanged;

    beginTable(cpType, getLabelSize("Selection"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "text", "Text", sceneProject, entities, settings);
    propertyRow(RowPropertyType::LocalEntity, cpType, "selection", "Selection", sceneProject, entities, settings);
    propertyRow(RowPropertyType::LocalEntity, cpType, "cursor", "Cursor", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Bool, cpType, "disabled", "Disabled", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Bool, cpType, "password", "Password", sceneProject, entities, settings);
    endTable();

    ImGui::SeparatorText("Content");
    beginTable(cpType, getLabelSize("Placeholder"), "textedit_content");
    propertyRow(RowPropertyType::String, cpType, "placeholder", "Placeholder", sceneProject, entities, settings);
    endTable();

    ImGui::SeparatorText("Cursor");
    beginTable(cpType, getLabelSize("Cursor Width"), "textedit_cursor");
    propertyRow(RowPropertyType::Float, cpType, "cursorBlink", "Blink", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "cursorWidth", "Width", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Color4L, cpType, "cursorColor", "Color", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "selectionColor", "Selection Color", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Color4L, cpType, "placeholderColor", "Placeholder Color", sceneProject, entities, settings);
    endTable();

    ImGui::SeparatorText("Selection");
    beginTable(cpType, getLabelSize("Selection Anchor"), "textedit_selection");
    propertyRow(RowPropertyType::Int, cpType, "cursorIndex", "Cursor Index", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "selectionAnchor", "Selection Anchor", sceneProject, entities, settingsInt);
    endTable();

    if (entities.size() == 1){
        TextEditComponent* textedit = sceneProject->scene->findComponent<TextEditComponent>(entities[0]);
        if (textedit){
            beginTable(cpType, getLabelSize("Password Char"), "textedit_password_char");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Password Char");
            ImGui::TableSetColumnIndex(1);
            char passwordCharBuffer[2] = { textedit->passwordChar ? textedit->passwordChar : '*', '\0' };
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##textedit_password_char", passwordCharBuffer, sizeof(passwordCharBuffer))){
                textedit->passwordChar = passwordCharBuffer[0] ? passwordCharBuffer[0] : '*';
                settings.onValueChanged();
            }
            endTable();
        }
    }
}

void editor::Properties::drawPanelComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settings;
    settings.onValueChanged = [sceneProject, entities](){
        for (const Entity& entity : entities){
            if (PanelComponent* panel = sceneProject->scene->findComponent<PanelComponent>(entity)){
                panel->needUpdatePanel = true;
            }
        }
    };

    RowSettings settingsEnum;
    settingsEnum.enumEntries = &entriesAnchorPreset;
    settingsEnum.onValueChanged = settings.onValueChanged;

    RowSettings settingsUInt;
    settingsUInt.stepSize = 1.0f;
    settingsUInt.secondColSize = 6 * ImGui::GetFontSize();
    settingsUInt.onValueChanged = settings.onValueChanged;

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();
    settingsInt.onValueChanged = settings.onValueChanged;

    beginTable(cpType, getLabelSize("Can Bring To Front"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "headerimage", "Header Image", sceneProject, entities, settings);
    propertyRow(RowPropertyType::LocalEntity, cpType, "headercontainer", "Header Container", sceneProject, entities, settings);
    propertyRow(RowPropertyType::LocalEntity, cpType, "headertext", "Header Text", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Enum, cpType, "titleAnchorPreset", "Title Anchor", sceneProject, entities, settingsEnum);
    propertyRow(RowPropertyType::UInt, cpType, "minWidth", "Min Width", sceneProject, entities, settingsUInt);
    propertyRow(RowPropertyType::UInt, cpType, "minHeight", "Min Height", sceneProject, entities, settingsUInt);
    propertyRow(RowPropertyType::Int, cpType, "resizeMargin", "Resize Margin", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "canMove", "Can Move", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Bool, cpType, "canResize", "Can Resize", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Bool, cpType, "canBringToFront", "Can Bring To Front", sceneProject, entities, settings);
    endTable();

    ImGui::SeparatorText("Header margin");
    beginTable(cpType, getLabelSize("Margin Bottom"), "panel_header_margin");
    propertyRow(RowPropertyType::Bool, cpType, "defaultHeaderMargin", "Default Margin", sceneProject, entities, settings);
    propertyRow(RowPropertyType::Int, cpType, "headerMarginLeft", "Margin Left", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "headerMarginRight", "Margin Right", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "headerMarginTop", "Margin Top", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "headerMarginBottom", "Margin Bottom", sceneProject, entities, settingsInt);
    endTable();
}

void editor::Properties::drawTextComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Multiline"));
    propertyRow(RowPropertyType::MultilineString, cpType, "text", "Text", sceneProject, entities);
    propertyRow(RowPropertyType::Font, cpType, "font", "Font", sceneProject, entities);
    propertyRow(RowPropertyType::UInt, cpType, "fontSize", "FontSize", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "multiline", "Multiline", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Fixed size");
    beginTable(cpType, getLabelSize("Height"), "text_settings_fixed");
    propertyRow(RowPropertyType::Bool, cpType, "fixedWidth", "Width", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "fixedHeight", "Height", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Pivot");
    beginTable(cpType, getLabelSize("Baseline"), "text_settings_pivot");
    propertyRow(RowPropertyType::Bool, cpType, "pivotBaseline", "Baseline", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "pivotCentered", "Center", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Advanced settings");
    beginTable(cpType, getLabelSize("Max. text size"), "text_settings_size");
    propertyRow(RowPropertyType::UInt, cpType, "maxTextSize", "Max. text size", sceneProject, entities, settingsInt);
    endTable();
}

void editor::Properties::drawUILayoutComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsAnchorPreset;
    settingsAnchorPreset.enumEntries = &entriesAnchorPreset;

    RowSettings settingsAnchorPoint;
    settingsAnchorPoint.stepSize = 0.01f;
    settingsAnchorPoint.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsOffset;
    settingsOffset.stepSize = 1.0f;
    settingsOffset.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsPositionOffset;
    settingsPositionOffset.stepSize = 1.0f;
    settingsPositionOffset.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsPivot;
    settingsPivot.stepSize = 0.01f;
    settingsPivot.secondColSize = 6 * ImGui::GetFontSize();
    settingsPivot.help = "Scale and rotation center: (0, 0) is the top-left corner, (0.5, 0.5) the center";

    beginTable(cpType, getLabelSize("Position Offset"));
    propertyRow(RowPropertyType::UInt, cpType, "width", "Width", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "height", "Height", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "usingAnchors", "Use Anchors", sceneProject, entities);
    bool showAnchorProperties = true;
    bool disableAnchorDetails = false;
    for (const Entity& entity : entities){
        const UILayoutComponent& layout = sceneProject->scene->getComponent<UILayoutComponent>(entity);
        if (layout.usingAnchors){
            showAnchorProperties = false;
        }
        if (layout.anchorPreset != AnchorPreset::NONE){
            disableAnchorDetails = true;
        }
    }

    if (!showAnchorProperties){
        propertyRow(RowPropertyType::Enum, cpType, "anchorPreset", "Preset", sceneProject, entities, settingsAnchorPreset);
        ImGui::BeginDisabled(disableAnchorDetails);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointLeft", "Anchor Left", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointTop", "Anchor Top", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointRight", "Anchor Right", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Float_0_1, cpType, "anchorPointBottom", "Anchor Bottom", sceneProject, entities, settingsAnchorPoint);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetLeft", "Offset Left", sceneProject, entities, settingsOffset);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetTop", "Offset Top", sceneProject, entities, settingsOffset);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetRight", "Offset Right", sceneProject, entities, settingsOffset);
        propertyRow(RowPropertyType::Int, cpType, "anchorOffsetBottom", "Offset Bottom", sceneProject, entities, settingsOffset);
        ImGui::EndDisabled();
    }
    propertyRow(RowPropertyType::Vector2, cpType, "positionOffset", "Position Offset", sceneProject, entities, settingsPositionOffset);
    propertyRow(RowPropertyType::Vector2, cpType, "pivot", "Pivot", sceneProject, entities, settingsPivot);
    propertyRow(RowPropertyType::Bool, cpType, "ignoreScissor", "Ignore Scissor", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "ignoreEvents", "Ignore Events", sceneProject, entities);
    endTable();
}

void editor::Properties::drawUIContainerComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    static std::vector<editor::EnumEntry> entriesContainerType = {
        { (int)ContainerType::VERTICAL, "Vertical" },
        { (int)ContainerType::HORIZONTAL, "Horizontal" },
        { (int)ContainerType::VERTICAL_WRAP, "Vertical Wrap" },
        { (int)ContainerType::HORIZONTAL_WRAP, "Horizontal Wrap" }
    };

    RowSettings settingsContainerType;
    settingsContainerType.enumEntries = &entriesContainerType;

    RowSettings settingsCellSize;
    settingsCellSize.stepSize = 1.0f;
    settingsCellSize.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsRect;
    settingsRect.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsLayout;
    settingsLayout.stepSize = 1.0f;
    settingsLayout.secondColSize = 6 * ImGui::GetFontSize();

    unsigned int numBoxes = sceneProject->scene->getComponent<UIContainerComponent>(entities[0]).numBoxes;
    for (Entity& entity : entities){
        numBoxes = std::min(numBoxes, sceneProject->scene->getComponent<UIContainerComponent>(entity).numBoxes);
    }

    beginTable(cpType, getLabelSize("Num Boxes"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsContainerType);
    propertyRow(RowPropertyType::Label, cpType, "numBoxes", "Num Boxes", sceneProject, entities, settingsLayout);

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_GEAR)){
        ImGui::OpenPopup("menusettings_container_boxes");
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(10 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_container_boxes")){

        if (numBoxes == 0){
            ImGui::TextDisabled("No boxes available");
        }

        for (unsigned int b = 0; b < numBoxes; b++){
            ImGui::SeparatorText(("Box " + std::to_string(b + 1)).c_str());

            beginTable(cpType, getLabelSize("Rect Height"), "box_popup_" + std::to_string(b));

            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].layout", "Layout Entity", sceneProject, entities, settingsLayout);
            propertyRow(RowPropertyType::Bool, cpType, "boxes["+std::to_string(b)+"].expand", "Expand", sceneProject, entities);

            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.x", "Rect X", sceneProject, entities, settingsRect);
            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.y", "Rect Y", sceneProject, entities, settingsRect);
            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.width", "Rect Width", sceneProject, entities, settingsRect);
            propertyRow(RowPropertyType::Label, cpType, "boxes["+std::to_string(b)+"].rect.height", "Rect Height", sceneProject, entities, settingsRect);

            endTable();
        }

        ImGui::EndPopup();
    }
    endTable();

    bool showWrapSettings = false;
    for (const Entity& entity : entities){
        const UIContainerComponent& container = sceneProject->scene->getComponent<UIContainerComponent>(entity);
        if (container.type == ContainerType::VERTICAL_WRAP || container.type == ContainerType::HORIZONTAL_WRAP){
            showWrapSettings = true;
        }
    }

    if (showWrapSettings) {
        ImGui::SeparatorText("Wrap settings");
        beginTable(cpType, getLabelSize("Use All Space"), "wrap_settings");
        propertyRow(RowPropertyType::Bool, cpType, "useAllWrapSpace", "Use All Space", sceneProject, entities);
        propertyRow(RowPropertyType::UInt, cpType, "wrapCellWidth", "Cell Width", sceneProject, entities, settingsCellSize);
        propertyRow(RowPropertyType::UInt, cpType, "wrapCellHeight", "Cell Height", sceneProject, entities, settingsCellSize);
        endTable();
    }

}

void editor::Properties::drawImageComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    ImGui::SeparatorText("Nine-patch rect");

    if (entities.size() == 1) {
        if (UIComponent* ui = sceneProject->scene->findComponent<UIComponent>(entities[0])){
            Texture* thumbTexture = findThumbnail(ui->texture.getPath());
            if (thumbTexture) {
                drawNinePatchesPreview(sceneProject->scene->getComponent<ImageComponent>(entities[0]), &ui->texture, thumbTexture, ImVec2(THUMBNAIL_SIZE, THUMBNAIL_SIZE));
            }
        }
    }

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsTexScale;
    settingsTexScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsTexScale.help = "Increase or decrease texture area by a factor";

    beginTable(cpType, getLabelSize("Margin Bottom"), "nine_margin_table");
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginLeft", "Margin Left", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginRight", "Margin Right", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginTop", "Margin Top", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "patchMarginBottom", "Margin Bottom", sceneProject, entities, settingsInt);
    endTable();

    beginTable(cpType, getLabelSize("Texture Scale"));
    propertyRow(RowPropertyType::Float, cpType, "textureScaleFactor", "Texture Scale", sceneProject, entities, settingsTexScale);
    endTable();
}

void editor::Properties::drawSpriteComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsTexScale;
    settingsTexScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsTexScale.help = "Increase or decrease texture area by a factor";

    RowSettings settingsPivot;
    settingsPivot.enumEntries = &entriesPivotPreset;

    beginTable(cpType, getLabelSize("Texture Scale"));
    propertyRow(RowPropertyType::UInt, cpType, "width", "Width", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "height", "Height", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Enum, cpType, "pivotPreset", "Pivot", sceneProject, entities, settingsPivot);
    propertyRow(RowPropertyType::Float, cpType, "textureScaleFactor", "Texture Scale", sceneProject, entities, settingsTexScale);
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "flipY", "Flip Y", "automaticFlipY", "Automatic Flip Y", sceneProject, entities);

    endTable();

    // --- Frames Rect Section ---
    if (entities.size() == 1) {
        SpriteComponent& sprite = sceneProject->scene->getComponent<SpriteComponent>(entities[0]);
        MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entities[0]);
        Texture previewTexture;
        if (meshComp) {
            if (meshComp->numSubmeshes > 0) {
                previewTexture = meshComp->submeshes[0].material.baseColorTexture;
            }
        }

        ImGui::SeparatorText("Sprite Frames");

        // Frame count from numFramesRect
        int activeFrameCount = (int)sprite.numFramesRect;

        // Sprite slicer tool button
        if (ImGui::Button(ICON_FA_GRIP " Slicer Tool", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            textureSlicerToolDialog.open(
                previewTexture,
                static_cast<int>(sprite.width),
                static_cast<int>(sprite.height),
                [this, sceneProject, entities, cpType, previewTexture](const TextureSlicerToolDialog::SliceResult& result) {
                    editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                    // Clear existing frames beyond new count
                    SpriteComponent& sprite = sceneProject->scene->getComponent<SpriteComponent>(entities[0]);
                    for (unsigned int i = (unsigned int)result.rects.size(); i < sprite.numFramesRect; i++) {
                        std::string prefix = "framesRect[" + std::to_string(i) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(0, 0, 0, 0));
                    }

                    // Set new frames from slicer result
                    for (size_t i = 0; i < result.rects.size(); i++) {
                        std::string prefix = "framesRect[" + std::to_string(i) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", result.rects[i].name);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(result.rects[i].rect.getX(), result.rects[i].rect.getY(),
                                result.rects[i].rect.getWidth(), result.rects[i].rect.getHeight()));
                    }

                    // Update numFramesRect
                    unsigned int newCount = (unsigned int)result.rects.size();
                    multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                        "numFramesRect", newCount);

                    // Apply first frame to sprite mesh
                    MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entities[0]);
                    int sheetWidth = !previewTexture.empty() ? previewTexture.getWidth() : (int)sprite.width;
                    int sheetHeight = !previewTexture.empty() ? previewTexture.getHeight() : (int)sprite.height;
                    if (meshComp && meshComp->numSubmeshes > 0 && !result.rects.empty() && sheetWidth > 0 && sheetHeight > 0) {
                        const Rect& firstFrameRect = result.rects[0].rect;
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], ComponentType::MeshComponent,
                            "submeshes[0].textureRect", Vector4(
                                firstFrameRect.getX() / (float)sheetWidth,
                                firstFrameRect.getY() / (float)sheetHeight,
                                firstFrameRect.getWidth() / (float)sheetWidth,
                                firstFrameRect.getHeight() / (float)sheetHeight));
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }
            );
        }

        beginTable(cpType, getLabelSize("Frames"), "sprite_frames_header");
        propertyHeader("Frames", -1, false, false);
        ImGui::Text("%d", activeFrameCount);
        float spriteFramesArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        if (drawSummaryAddButton(ICON_FA_PLUS " Add Frame##sprite_frames_add", spriteFramesArrowWidth)) {
            int freeSlot = (int)sprite.numFramesRect;
            if (sprite.framesRect.validIndex(freeSlot)) {
                editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                std::string prefix = "framesRect[" + std::to_string(freeSlot) + "]";
                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".name", "frame_" + std::to_string(freeSlot));
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                    "numFramesRect", (unsigned int)(freeSlot + 1));
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##toggle_all_frames", spriteFramesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
            spriteFramesExpanded = !spriteFramesExpanded;
        }
        endTable();

        if (spriteFramesExpanded) {
            // Draw each frame
            RowSettings settingsFrameRect;
            settingsFrameRect.stepSize = 1.0f;
            settingsFrameRect.format = "%.0f";
            settingsFrameRect.showColors = false;

            for (unsigned int i = 0; i < sprite.numFramesRect; i++) {

                ImGui::PushID(i);

                std::string frameLabel = "[" + std::to_string(i) + "] " + sprite.framesRect[i].name;
                std::string frameGroupStr = "frame_" + std::to_string(i);
                std::string prefix = "framesRect[" + std::to_string(i) + "]";

                ImGui::SeparatorText(frameLabel.c_str());

                beginTable(cpType, getLabelSize("Frame"), frameGroupStr);

                // Frame row: Preview + Trash button
                propertyHeader("Frame", -1, false, false);

                {
                    float previewSize = ImGui::GetFrameHeight() * 2.2f;
                    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                    ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
                    ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());
                    ImVec2 applyButtonSize = arrowButtonSize;
                    ImVec2 resizeButtonSize = arrowButtonSize;

                    bool hasFramePreview = !previewTexture.empty() && sprite.framesRect[i].rect.getWidth() > 0.0f && sprite.framesRect[i].rect.getHeight() > 0.0f;
                    bool hasValidMeshTextureRect = meshComp && meshComp->numSubmeshes > 0;
                    bool canApplyFrame = hasValidMeshTextureRect && !previewTexture.empty() && previewTexture.getWidth() > 0 && previewTexture.getHeight() > 0;
                    bool canResizeToFrame = sprite.framesRect[i].rect.getWidth() > 0.0f && sprite.framesRect[i].rect.getHeight() > 0.0f;
                    unsigned int frameWidth = canResizeToFrame ? static_cast<unsigned int>(sprite.framesRect[i].rect.getWidth()) : 0;
                    unsigned int frameHeight = canResizeToFrame ? static_cast<unsigned int>(sprite.framesRect[i].rect.getHeight()) : 0;
                    Rect normalizedFrameRect = sprite.framesRect[i].rect;
                    if (canApplyFrame) {
                        normalizedFrameRect = Rect(
                            sprite.framesRect[i].rect.getX() / (float)previewTexture.getWidth(),
                            sprite.framesRect[i].rect.getY() / (float)previewTexture.getHeight(),
                            sprite.framesRect[i].rect.getWidth() / (float)previewTexture.getWidth(),
                            sprite.framesRect[i].rect.getHeight() / (float)previewTexture.getHeight()
                        );
                    }
                    bool isAppliedFrame = false;
                    if (hasValidMeshTextureRect) {
                        const Rect& appliedRect = meshComp->submeshes[0].textureRect;
                        isAppliedFrame = appliedRect.getX() == normalizedFrameRect.getX()
                                && appliedRect.getY() == normalizedFrameRect.getY()
                                && appliedRect.getWidth() == normalizedFrameRect.getWidth()
                                && appliedRect.getHeight() == normalizedFrameRect.getHeight();
                    }
                        bool isSizedToFrame = canResizeToFrame && sprite.width == frameWidth && sprite.height == frameHeight;

                    float trailingWidth = arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + applyButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + resizeButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + deleteButtonSize.x;
                    if (hasFramePreview) {
                        trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
                    }

                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
                    ImGui::SetCursorPosX(targetX);

                    if (hasFramePreview) {
                        drawSpriteFramePreview(&previewTexture, sprite.framesRect[i].rect, ImVec2(previewSize, previewSize), "##frame_preview");
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", frameLabel.c_str());

                            float tooltipMaxSize = Theme::dpi(200.0f);
                            float scale = std::min(tooltipMaxSize / std::max(1.0f, sprite.framesRect[i].rect.getWidth()),
                                                   tooltipMaxSize / std::max(1.0f, sprite.framesRect[i].rect.getHeight()));
                            scale = std::min(scale, 1.0f);
                            ImVec2 tooltipSize(std::max(Theme::dpi(32.0f), sprite.framesRect[i].rect.getWidth() * scale),
                                               std::max(Theme::dpi(32.0f), sprite.framesRect[i].rect.getHeight() * scale));

                            drawSpriteFramePreview(&previewTexture, sprite.framesRect[i].rect, tooltipSize, "##frame_preview_tooltip");
                            ImGui::EndTooltip();
                        }

                        ImGui::SameLine();
                    }

                    if (ImGui::ArrowButton("##toggle_frame", spriteFramesButtonGroups[frameGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
                        spriteFramesButtonGroups[frameGroupStr] = !spriteFramesButtonGroups[frameGroupStr];
                    }
                    ImGui::SameLine();

                    bool deleted = false;
                    ImGui::BeginDisabled(!canApplyFrame || isAppliedFrame);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_CHECK "##apply_frame", applyButtonSize)) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], ComponentType::MeshComponent,
                            "submeshes[0].textureRect", Vector4(normalizedFrameRect.getX(), normalizedFrameRect.getY(), normalizedFrameRect.getWidth(), normalizedFrameRect.getHeight()));
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    if (ImGui::IsItemHovered()) {
                        if (!meshComp) {
                            ImGui::SetTooltip("Sprite entity has no MeshComponent");
                        } else if (meshComp->numSubmeshes == 0) {
                            ImGui::SetTooltip("Sprite mesh has no submeshes");
                        } else if (!canApplyFrame) {
                            ImGui::SetTooltip("Sprite needs a valid texture to apply a frame");
                        } else {
                            ImGui::SetTooltip("Apply frame to sprite");
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndDisabled();

                    ImGui::SameLine();

                    ImGui::BeginDisabled(!canResizeToFrame || isSizedToFrame);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_EXPAND "##resize_to_frame", resizeButtonSize)) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "width", frameWidth);
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "height", frameHeight);
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    if (ImGui::IsItemHovered()) {
                        if (!canResizeToFrame) {
                            ImGui::SetTooltip("Frame needs a valid size to resize the sprite");
                        } else if (isSizedToFrame) {
                            ImGui::SetTooltip("Sprite already matches this frame size");
                        } else {
                            ImGui::SetTooltip("Resize sprite to this frame size");
                        }
                    }
                    ImGui::PopStyleVar();
                    ImGui::EndDisabled();

                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_TRASH_CAN"##delete_frame")) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                        // Shift frames after deleted one
                        for (unsigned int j = i; j + 1 < sprite.numFramesRect; j++) {
                            std::string srcPrefix = "framesRect[" + std::to_string(j + 1) + "]";
                            std::string dstPrefix = "framesRect[" + std::to_string(j) + "]";
                            multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".name", sprite.framesRect[j + 1].name);
                            multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".rect", Vector4(sprite.framesRect[j + 1].rect.getX(), sprite.framesRect[j + 1].rect.getY(),
                                    sprite.framesRect[j + 1].rect.getWidth(), sprite.framesRect[j + 1].rect.getHeight()));
                        }

                        // Clear last slot
                        unsigned int lastIdx = sprite.numFramesRect - 1;
                        std::string lastPrefix = "framesRect[" + std::to_string(lastIdx) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType, lastPrefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType, lastPrefix + ".rect", Vector4(0, 0, 0, 0));

                        // Decrement count
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "numFramesRect", sprite.numFramesRect - 1);

                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                        deleted = true;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);

                    // Name and Rect rows
                    if (!deleted && spriteFramesButtonGroups[frameGroupStr]) {
                        propertyRow(RowPropertyType::String, cpType, prefix + ".name", "Name", sceneProject, entities);
                        propertyRow(RowPropertyType::Vector4, cpType, prefix + ".rect", "Rect", sceneProject, entities, settingsFrameRect);
                    }
                }

                endTable();

                ImGui::PopID();
            }
        }
    } else {
        ImGui::SeparatorText("Sprite Frames");
        ImGui::TextDisabled("Select a single entity to edit sprite frames");
    }
}

void editor::Properties::drawCameraComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;
    CameraComponent& camera = scene->getComponent<CameraComponent>(entities[0]);

    float firstColSize = getLabelSize("Auto Resize");

    beginTable(cpType, firstColSize);

    RowSettings enumSettings;
    enumSettings.enumEntries = &entriesCameraType;
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, enumSettings);

    RowSettings defaultSettings;

    propertyRow(RowPropertyType::Bool, cpType, "useTarget", "Use Target", sceneProject, entities, defaultSettings);

    if (camera.useTarget){
        propertyRow(RowPropertyType::Vector3, cpType, "target", "Target", sceneProject, entities, defaultSettings);
    }
    propertyRow(RowPropertyType::Direction, cpType, "up", "Up", sceneProject, entities, defaultSettings);

    if (camera.type == CameraType::CAMERA_PERSPECTIVE) {
        RowSettings fovSettings;
        fovSettings.format = "%.3f";
        fovSettings.stepSize = 0.01f;
        propertyRow(RowPropertyType::Float, cpType, "yfov", "Y FOV", sceneProject, entities, fovSettings);

        propertyRow(RowPropertyType::Float, cpType, "aspect", "Aspect", sceneProject, entities, defaultSettings);
    }

    if (camera.type == CameraType::CAMERA_ORTHO || camera.type == CameraType::CAMERA_UI) {
        propertyRow(RowPropertyType::Float, cpType, "left", "Left", sceneProject, entities, defaultSettings);
        propertyRow(RowPropertyType::Float, cpType, "right", "Right", sceneProject, entities, defaultSettings);
        propertyRow(RowPropertyType::Float, cpType, "bottom", "Bottom", sceneProject, entities, defaultSettings);
        propertyRow(RowPropertyType::Float, cpType, "top", "Top", sceneProject, entities, defaultSettings);
    }

    propertyRow(RowPropertyType::Float, cpType, "near", "Near", sceneProject, entities, defaultSettings);
    propertyRow(RowPropertyType::Float, cpType, "far", "Far", sceneProject, entities, defaultSettings);

    //propertyRow(RowPropertyType::Bool, cpType, "renderToTexture", "Render To Texture", sceneProject, entities, defaultSettings);
    //propertyRow(RowPropertyType::Bool, cpType, "transparentSort", "Transparent Sort", sceneProject, entities, defaultSettings);
    propertyRow(RowPropertyType::Bool, cpType, "depthTest", "Depth Test", sceneProject, entities, defaultSettings);
    propertyRow(RowPropertyType::Bool, cpType, "autoResize", "Auto Resize", sceneProject, entities, defaultSettings);

    if (entities.size() == 1 && sceneProject->sceneRender && sceneWindow) {
        Entity cameraEntity = entities[0];
        bool previewingThisCamera = sceneProject->sceneRender->isPreviewCameraActive()
            && sceneProject->sceneRender->getPreviewCameraEntity() == cameraEntity;
        bool canPreviewCamera = sceneProject->playState == ScenePlayState::STOPPED
            && scene->findComponent<Transform>(cameraEntity);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("Preview");
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled(!canPreviewCamera);
        if (previewingThisCamera) {
            if (ImGui::Button(ICON_FA_XMARK" Exit")) {
                sceneWindow->stopViewingCamera(sceneProject->id);
            }
        } else {
            if (ImGui::Button(ICON_FA_VIDEO" View")) {
                sceneWindow->viewThroughCamera(sceneProject->id, cameraEntity);
            }
        }
        ImGui::EndDisabled();
        if (!canPreviewCamera && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(sceneProject->playState == ScenePlayState::STOPPED ? "Camera transform unavailable" : "Stop the scene to preview cameras");
        }
    }

    endTable();

    // framebuffer settings only matter when the camera renders into a texture
    // (bind a camera to a texture by dragging it onto a texture slot)
    if (camera.renderToTexture){
        RowSettings sizeSettings;
        sizeSettings.stepSize = 1.0f;

        RowSettings filterSettings;
        filterSettings.enumEntries = &entriesTextureFilter;

        ImGui::SeparatorText("Render Target");
        beginTable(cpType, getLabelSize("Height"), "camera_framebuffer");
        propertyRow(RowPropertyType::UInt, cpType, "framebufferWidth", "Width", sceneProject, entities, sizeSettings);
        propertyRow(RowPropertyType::UInt, cpType, "framebufferHeight", "Height", sceneProject, entities, sizeSettings);
        propertyRow(RowPropertyType::Enum, cpType, "framebufferFilter", "Filter", sceneProject, entities, filterSettings);
        endTable();
    }
}

void editor::Properties::drawAudioComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings compactSettings;
    compactSettings.secondColSize = 7 * ImGui::GetFontSize();

    RowSettings floatSettings = compactSettings;
    floatSettings.format = "%.3f";
    floatSettings.stepSize = 0.01f;

    RowSettings doubleSettings = floatSettings;

    beginTable(cpType, getLabelSize("Sound File"));

    if (entities.size() == 1) {
        Entity entity = entities[0];
        SoundComponent& audio = sceneProject->scene->getComponent<SoundComponent>(entity);

        auto setAudioFilename = [&](const std::filesystem::path& selectedPath) -> bool {
            std::filesystem::path assetsPath = std::filesystem::absolute(project->getAssetsPath()).lexically_normal();
            std::filesystem::path filePath = project->resolveAssetPath(selectedPath);
            filePath = std::filesystem::absolute(filePath).lexically_normal();

            std::error_code errorCode;
            std::filesystem::path relative = std::filesystem::relative(filePath, assetsPath, errorCode);
            std::string relativePath = relative.generic_string();
            if (errorCode || relative.empty() || *relative.begin() == "..") {
                ImGui::OpenPopup("Sound Import Error");
                return false;
            }

            if (!Util::isAudioFile(relativePath)) {
                return false;
            }

            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(
                new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, "filename", relativePath)
            );
            return true;
        };

        std::string currentPath = audio.filename;

        if (propertyHeader("Sound File", -1, !currentPath.empty())) {
            CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(
                new PropertyCmd<std::string>(project, sceneProject->id, entity, cpType, "filename", "")
            );
            currentPath = "";
        }

        std::string displayName = currentPath.empty() ? "< Not set >" : std::filesystem::path(currentPath).filename().string();
        float availWidth = ImGui::GetContentRegionAvail().x;
        float buttonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2;

        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Colors::filenameLabel);
        ImGui::BeginChild("audiofilename", ImVec2(availWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x, ImGui::GetFrameHeight()),
            false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        float textWidth = ImGui::CalcTextSize(displayName.c_str()).x;
        float childAvail = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(std::max(0.0f, childAvail - textWidth - 2));
        ImGui::SetCursorPosY(ImGui::GetStyle().FramePadding.y);
        if (currentPath.empty())
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::Text("%s", displayName.c_str());
        if (currentPath.empty())
            ImGui::PopStyleColor();
        ImGui::EndChild();
        if (!currentPath.empty()){
            ImGui::SetItemTooltip("%s", currentPath.c_str());
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##audio_load")) {
            std::string path = editor::FileDialogs::openFileDialog(project->getAssetsPath().string(), FILE_DIALOG_AUDIO);
            if (!path.empty()) {
                setAudioFilename(path);
            }
        }

        ImGui::EndGroup();

        if (sceneProject && sceneProject->playState == ScenePlayState::STOPPED) {
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files", ImGuiDragDropFlags_AcceptBeforeDelivery)) {
                    std::vector<std::string> receivedStrings = editor::Util::getStringsFromPayload(payload);
                    if (!receivedStrings.empty()) {
                        std::filesystem::path droppedPath(receivedStrings[0]);
                        std::filesystem::path filePath = project->resolveAssetPath(droppedPath);
                        std::error_code errorCode;
                        std::filesystem::path relative = std::filesystem::relative(std::filesystem::absolute(filePath), std::filesystem::absolute(project->getAssetsPath()), errorCode);
                        if (!errorCode && Util::isAudioFile(relative.generic_string()) && payload->IsDelivery()) {
                            if (setAudioFilename(droppedPath)) {
                                ImGui::SetWindowFocus(Properties::WINDOW_NAME);
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        if (ImGui::BeginPopupModal("Sound Import Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Selected file must be an audio file within the project directory.");
            ImGui::Separator();
            float buttonModalWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonModalWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }else{
        propertyRow(RowPropertyType::String, cpType, "filename", "Sound File", sceneProject, entities);
    }

    RowSettings stateSettings = compactSettings;
    stateSettings.enumEntries = &entriesAudioState;
    propertyRow(RowPropertyType::Enum, cpType, "state", "State", sceneProject, entities, stateSettings);

    endTable();

    if (entities.size() == 1) {
        Entity entity = entities[0];
        SoundComponent& audio = sceneProject->scene->getComponent<SoundComponent>(entity);
        bool sceneIsStopped = sceneProject->playState == ScenePlayState::STOPPED;
        SoundPreviewRuntime& ap = soundPreview();
        bool isThisPreview = ap.active && ap.sceneId == sceneProject->id && ap.entity == entity;

        if (ap.active && (!isThisPreview || !sceneIsStopped || ap.filename != audio.filename)) {
            stopSoundPreview(!isThisPreview || ap.filename != audio.filename);
            isThisPreview = false;
        }

        if (isThisPreview) {
            updateSoundPreview(audio);
            isThisPreview = ap.active && ap.sceneId == sceneProject->id && ap.entity == entity;
        }

        ImGui::Separator();
        ImGui::BeginDisabled(!sceneIsStopped || audio.filename.empty());

        if (isThisPreview && ap.playing) {
            if (ImGui::Button(ICON_FA_PAUSE "##audio_pause")) {
                pauseSoundPreview();
            }
        } else {
            if (ImGui::Button(ICON_FA_PLAY "##audio_play")) {
                if (!startSoundPreview(sceneProject, entity, audio)) {
                    ImGui::OpenPopup("Sound Preview Error");
                } else {
                    isThisPreview = true;
                }
            }
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(!isThisPreview);
        if (ImGui::Button(ICON_FA_STOP "##audio_stop")) {
            stopSoundPreview();
            isThisPreview = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        double previewTime = isThisPreview ? ap.time : 0.0;
        double previewLength = (ap.loaded && ap.sceneId == sceneProject->id && ap.entity == entity && ap.filename == audio.filename) ? ap.length : audio.length;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.2fs", previewTime);
        ImGui::SameLine();

        float timelineWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
        float frameHeight = ImGui::GetFrameHeight();
        float timelineHeight = 6.0f;
        float rounding = timelineHeight * 0.5f;
        ImVec2 timelinePos = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##audio_timeline_strip", ImVec2(timelineWidth, frameHeight));
        bool timelineHovered = ImGui::IsItemHovered();
        bool timelineClicked = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);

        if (sceneIsStopped && previewLength > 0.0 && timelineClicked) {
            float mouseX = ImGui::GetIO().MousePos.x;
            float clickFraction = std::clamp((mouseX - timelinePos.x) / timelineWidth, 0.0f, 1.0f);
            double seekTime = clickFraction * previewLength;
            if (!seekSoundPreview(sceneProject, entity, audio, seekTime)) {
                ImGui::OpenPopup("Sound Preview Error");
            } else {
                isThisPreview = true;
            }
        }

        if (timelineHovered && previewLength > 0.0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImGuiStyle& style = ImGui::GetStyle();
        ImVec4 trackColor = style.Colors[ImGuiCol_TextDisabled];
        trackColor.w = 0.22f;
        ImVec4 fillColor = style.Colors[ImGuiCol_PlotHistogram];
        float fraction = (previewLength > 0.0) ? std::clamp(static_cast<float>(previewTime / previewLength), 0.0f, 1.0f) : 0.0f;

        ImVec2 barMin(timelinePos.x, timelinePos.y + (frameHeight - timelineHeight) * 0.5f);
        ImVec2 barMax(timelinePos.x + timelineWidth, barMin.y + timelineHeight);
        drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(trackColor), rounding);

        if (fraction > 0.0f) {
            ImVec2 fillMax(barMin.x + timelineWidth * fraction, barMax.y);
            drawList->AddRectFilled(barMin, fillMax, ImGui::GetColorU32(fillColor), rounding);
        }

        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("Sound Preview Error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s", ap.error.c_str());
            ImGui::Separator();
            float buttonModalWidth = 120;
            float windowWidth = ImGui::GetWindowSize().x;
            ImGui::SetCursorPosX((windowWidth - buttonModalWidth) * 0.5f);
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SeparatorText("Playback");

    beginTable(cpType, getLabelSize("Inaudible Must Tick"), "audio_playback_settings");

    propertyRow(RowPropertyType::DoublePositive, cpType, "volume", "Volume", sceneProject, entities, doubleSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "speed", "Speed", sceneProject, entities, floatSettings);

    RowSettings panSettings = floatSettings;
    panSettings.help = "-1 left, 0 center, 1 right";
    propertyRow(RowPropertyType::Float, cpType, "pan", "Pan", sceneProject, entities, panSettings);

    propertyRow(RowPropertyType::Bool, cpType, "looping", "Looping", sceneProject, entities, compactSettings);
    propertyRow(RowPropertyType::DoublePositive, cpType, "loopingPoint", "Loop Point", sceneProject, entities, doubleSettings);
    propertyRow(RowPropertyType::Bool, cpType, "protectVoice", "Protect Voice", sceneProject, entities, compactSettings);
    propertyRow(RowPropertyType::Bool, cpType, "enableClocked", "Clocked", sceneProject, entities, compactSettings);
    propertyRow(RowPropertyType::Bool, cpType, "inaudibleBehaviorMustTick", "Inaudible Must Tick", sceneProject, entities, compactSettings);
    propertyRow(RowPropertyType::Bool, cpType, "inaudibleBehaviorKill", "Inaudible Kill", sceneProject, entities, compactSettings);

    bool show3DSettings = false;
    for (Entity entity : entities){
        Signature signature = sceneProject->scene->getSignature(entity);
        show3DSettings = show3DSettings || signature.test(sceneProject->scene->getComponentId<Transform>());
    }

    endTable();

    if (show3DSettings){
        ImGui::SeparatorText("3D Sound");

        beginTable(cpType, getLabelSize("Max Distance"), "audio_3d_settings");

        RowSettings attenuationSettings;
        attenuationSettings.enumEntries = &entriesAudioAttenuation;
        propertyRow(RowPropertyType::FloatPositive, cpType, "minDistance", "Min Distance", sceneProject, entities, floatSettings);
        propertyRow(RowPropertyType::FloatPositive, cpType, "maxDistance", "Max Distance", sceneProject, entities, floatSettings);
        propertyRow(RowPropertyType::Enum, cpType, "attenuationModel", "Attenuation", sceneProject, entities, attenuationSettings);
        propertyRow(RowPropertyType::FloatPositive, cpType, "attenuationRolloffFactor", "Rolloff", sceneProject, entities, floatSettings);
        propertyRow(RowPropertyType::FloatPositive, cpType, "dopplerFactor", "Doppler", sceneProject, entities, floatSettings);

        endTable();
    }

}

void editor::Properties::drawTilemapComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsTexScale;
    settingsTexScale.secondColSize = 6 * ImGui::GetFontSize();
    settingsTexScale.help = "Increase or decrease texture area by a factor";

    beginTable(cpType, getLabelSize("Reserve Tiles"));
    propertyRow(RowPropertyType::UInt, cpType, "width", "Width", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::UInt, cpType, "height", "Height", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Float, cpType, "textureScaleFactor", "Texture Scale", sceneProject, entities, settingsTexScale);
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "flipY", "Flip Y", "automaticFlipY", "Automatic Flip Y", sceneProject, entities);
    propertyRow(RowPropertyType::UInt, cpType, "reserveTiles", "Reserve Tiles", sceneProject, entities, settingsInt);
    endTable();

    if (entities.size() == 1) {
        TilemapComponent& tilemap = sceneProject->scene->getComponent<TilemapComponent>(entities[0]);
        MeshComponent* meshComp = sceneProject->scene->findComponent<MeshComponent>(entities[0]);

        // Build submesh list for tileset slicer
        std::vector<TextureSlicerToolDialog::SubmeshInfo> submeshInfos;
        if (meshComp) {
            for (unsigned int s = 0; s < meshComp->numSubmeshes; s++) {
                TextureSlicerToolDialog::SubmeshInfo info;
                info.name = "Submesh " + std::to_string(s);
                info.texture = meshComp->submeshes[s].material.baseColorTexture;
                submeshInfos.push_back(info);
            }
        }

        // --- Tile Rects Section ---
        ImGui::SeparatorText("Tile Rects");

        int activeTileRectCount = (int)tilemap.numTilesRect;

        // Slicer tool button
        if (ImGui::Button(ICON_FA_TABLE_CELLS " Slicer Tool", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            textureSlicerToolDialog.openTileset(
                submeshInfos,
                static_cast<int>(tilemap.width),
                static_cast<int>(tilemap.height),
                [this, sceneProject, entities, cpType](const TextureSlicerToolDialog::SliceResult& result) {
                    editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                    TilemapComponent& tilemap = sceneProject->scene->getComponent<TilemapComponent>(entities[0]);

                    // Collect existing rects from other submeshes (keep them)
                    struct KeptRect {
                        std::string name;
                        int submeshId;
                        Vector4 rect;
                    };
                    std::vector<KeptRect> kept;
                    for (unsigned int i = 0; i < tilemap.numTilesRect; i++) {
                        if (tilemap.tilesRect[i].submeshId != result.submeshId) {
                            KeptRect kr;
                            kr.name = tilemap.tilesRect[i].name;
                            kr.submeshId = tilemap.tilesRect[i].submeshId;
                            kr.rect = Vector4(tilemap.tilesRect[i].rect.getX(), tilemap.tilesRect[i].rect.getY(),
                                              tilemap.tilesRect[i].rect.getWidth(), tilemap.tilesRect[i].rect.getHeight());
                            kept.push_back(kr);
                        }
                    }

                    unsigned int newTotal = (unsigned int)(kept.size() + result.rects.size());
                    unsigned int writeIndex = 0;

                    // Write kept rects (from other submeshes)
                    for (size_t i = 0; i < kept.size(); i++, writeIndex++) {
                        std::string prefix = "tilesRect[" + std::to_string(writeIndex) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", kept[i].name);
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".submeshId", kept[i].submeshId);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", kept[i].rect);
                    }

                    // Write new rects from slicer result
                    for (size_t i = 0; i < result.rects.size(); i++, writeIndex++) {
                        std::string prefix = "tilesRect[" + std::to_string(writeIndex) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", result.rects[i].name);
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".submeshId", result.submeshId);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(result.rects[i].rect.getX(), result.rects[i].rect.getY(),
                                result.rects[i].rect.getWidth(), result.rects[i].rect.getHeight()));
                    }

                    // Clear leftover slots beyond new total
                    for (unsigned int i = newTotal; i < tilemap.numTilesRect; i++) {
                        std::string prefix = "tilesRect[" + std::to_string(i) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".submeshId", -1);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            prefix + ".rect", Vector4(0, 0, 0, 0));
                    }

                    // Update numTilesRect
                    multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                        "numTilesRect", newTotal);

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }
            );
        }

        ImGui::TextDisabled(ICON_FA_GRIP_VERTICAL " Drag rect preview to Scene to place tile");

        beginTable(cpType, getLabelSize("Tile Rects"), "tilemap_rects_header");
        propertyHeader("Tile Rects", -1, false, false);
        ImGui::Text("%d", activeTileRectCount);
        float tileRectsArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        if (drawSummaryAddButton(ICON_FA_PLUS " Add Tile Rect##tile_rects_add", tileRectsArrowWidth)) {
            int freeSlot = (int)tilemap.numTilesRect;
            if (tilemap.tilesRect.validIndex(freeSlot)) {
                editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                std::string prefix = "tilesRect[" + std::to_string(freeSlot) + "]";
                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".name", "rect_" + std::to_string(freeSlot));
                multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".submeshId", 0);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                    "numTilesRect", (unsigned int)(freeSlot + 1));
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##toggle_all_rects", tilemapRectsExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
            tilemapRectsExpanded = !tilemapRectsExpanded;
        }
        endTable();

        if (tilemapRectsExpanded) {
            // Draw each tile rect
            RowSettings settingsRect;
            settingsRect.stepSize = 1.0f;
            settingsRect.format = "%.0f";
            settingsRect.showColors = false;

            for (unsigned int i = 0; i < tilemap.numTilesRect; i++) {
                ImGui::PushID(i);

                std::string label = "[" + std::to_string(i) + "] " + tilemap.tilesRect[i].name;
                std::string groupStr = "tilerect_" + std::to_string(i);
                std::string prefix = "tilesRect[" + std::to_string(i) + "]";

                ImGui::SeparatorText(label.c_str());

                beginTable(cpType, getLabelSize("Name"), groupStr);

                propertyHeader("Rect", -1, false, false);
                {
                    float previewSize = ImGui::GetFrameHeight() * 2.2f;
                    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                    ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
                    ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

                    int submeshId = tilemap.tilesRect[i].submeshId;
                    Texture* rectTex = nullptr;
                    if (meshComp && submeshId >= 0 && (unsigned int)submeshId < meshComp->numSubmeshes) {
                        rectTex = &meshComp->submeshes[submeshId].material.baseColorTexture;
                        if (rectTex->empty()) rectTex = nullptr;
                    }

                    bool hasFramePreview = rectTex && tilemap.tilesRect[i].rect.getWidth() > 0.0f && tilemap.tilesRect[i].rect.getHeight() > 0.0f;

                    float trailingWidth = arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + deleteButtonSize.x;
                    if (hasFramePreview) {
                        trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
                    }

                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
                    ImGui::SetCursorPosX(targetX);

                    if (hasFramePreview) {
                        drawSpriteFramePreview(rectTex, tilemap.tilesRect[i].rect, ImVec2(previewSize, previewSize), "##rect_preview");
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            TileRectPayload payload{(uint32_t)sceneProject->id, entities[0], (int)i};
                            ImGui::SetDragDropPayload("tile_rect", &payload, sizeof(payload));
                            drawSpriteFramePreview(rectTex, tilemap.tilesRect[i].rect, Theme::dpi(ImVec2(48.0f, 48.0f)), "##rect_drag_preview");
                            ImGui::EndDragDropSource();
                        } else if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", label.c_str());

                            float tooltipMaxSize = Theme::dpi(200.0f);
                            float scale = std::min(tooltipMaxSize / std::max(1.0f, tilemap.tilesRect[i].rect.getWidth()),
                                                   tooltipMaxSize / std::max(1.0f, tilemap.tilesRect[i].rect.getHeight()));
                            scale = std::min(scale, 1.0f);
                            ImVec2 tooltipSize(std::max(Theme::dpi(32.0f), tilemap.tilesRect[i].rect.getWidth() * scale),
                                               std::max(Theme::dpi(32.0f), tilemap.tilesRect[i].rect.getHeight() * scale));

                            drawSpriteFramePreview(rectTex, tilemap.tilesRect[i].rect, tooltipSize, "##rect_preview_tooltip");
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                    }

                    if (ImGui::ArrowButton("##toggle_rect", tilemapRectsButtonGroups[groupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
                        tilemapRectsButtonGroups[groupStr] = !tilemapRectsButtonGroups[groupStr];
                    }
                    ImGui::SameLine();

                    {
                    ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_SQUARE_PLUS);
                    ImVec2 centerPad((arrowButtonSize.x - iconSize.x) * 0.5f, (arrowButtonSize.y - iconSize.y) * 0.5f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, centerPad);
                    }
                    if (ImGui::Button(ICON_FA_SQUARE_PLUS "##add_tile_from_rect")) {
                        int freeSlot = (int)tilemap.numTiles;
                        if (tilemap.tiles.validIndex(freeSlot)) {
                            editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                            std::string tilePrefix = "tiles[" + std::to_string(freeSlot) + "]";
                            multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".name", tilemap.tilesRect[i].name);
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".rectId", (int)i);
                            multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".width", tilemap.tilesRect[i].rect.getWidth());
                            multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                                tilePrefix + ".height", tilemap.tilesRect[i].rect.getHeight());
                            multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                                "numTiles", (unsigned int)(freeSlot + 1));
                            multiCmd->setNoMerge();
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                        }
                    }
                    ImGui::PopStyleVar();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Create tile from this rect");
                    }
                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_rect", deleteButtonSize)) {
                        editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                        // Shift subsequent rects down
                        for (unsigned int j = i; j < tilemap.numTilesRect - 1; j++) {
                            std::string dstPrefix = "tilesRect[" + std::to_string(j) + "]";
                            multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".name", tilemap.tilesRect[j + 1].name);
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".submeshId", tilemap.tilesRect[j + 1].submeshId);
                            multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                                dstPrefix + ".rect", Vector4(tilemap.tilesRect[j + 1].rect.getX(), tilemap.tilesRect[j + 1].rect.getY(),
                                    tilemap.tilesRect[j + 1].rect.getWidth(), tilemap.tilesRect[j + 1].rect.getHeight()));
                        }
                        // Clear last slot
                        unsigned int lastIdx = tilemap.numTilesRect - 1;
                        std::string lastPrefix = "tilesRect[" + std::to_string(lastIdx) + "]";
                        multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                            lastPrefix + ".name", std::string(""));
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                            lastPrefix + ".submeshId", -1);
                        multiCmd->addPropertyCmd<Vector4>(project, sceneProject->id, entities[0], cpType,
                            lastPrefix + ".rect", Vector4(0, 0, 0, 0));
                        for (unsigned int j = 0; j < tilemap.numTiles; j++) {
                            int rectId = tilemap.tiles[j].rectId;
                            if (rectId == (int)i) {
                                rectId = -1;
                            } else if (rectId > (int)i) {
                                rectId--;
                            }

                            if (rectId != tilemap.tiles[j].rectId) {
                                multiCmd->addPropertyCmd<int>(project, sceneProject->id, entities[0], cpType,
                                    "tiles[" + std::to_string(j) + "].rectId", rectId);
                            }
                        }
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                            "numTilesRect", (unsigned int)(tilemap.numTilesRect - 1));
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                        endTable();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                }

                if (tilemapRectsButtonGroups[groupStr]) {
                    propertyRow(RowPropertyType::String, cpType, prefix + ".name", "Name", sceneProject, entities);
                    propertyRow(RowPropertyType::Vector4, cpType, prefix + ".rect", "Rect", sceneProject, entities, settingsRect);

                    // Submesh selector combo
                    {
                        unsigned int numSubmeshes = meshComp ? meshComp->numSubmeshes : 1;
                        int currentSubmeshId = tilemap.tilesRect[i].submeshId;

                        propertyHeader("Submesh");
                        ImGui::SetNextItemWidth(-1);

                        std::string comboLabel = "##submeshId_" + std::to_string(i);
                        std::string previewStr = (currentSubmeshId >= 0 && (unsigned int)currentSubmeshId < numSubmeshes)
                            ? "Submesh " + std::to_string(currentSubmeshId)
                            : "None";

                        if (ImGui::BeginCombo(comboLabel.c_str(), previewStr.c_str())) {
                            for (unsigned int s = 0; s < numSubmeshes; s++) {
                                bool isSelected = ((int)s == currentSubmeshId);
                                std::string itemLabel = "Submesh " + std::to_string(s);
                                if (ImGui::Selectable(itemLabel.c_str(), isSelected)) {
                                    if ((int)s != currentSubmeshId) {
                                        editor::PropertyCmd<int>* propCmd = new editor::PropertyCmd<int>(
                                            project, sceneProject->id, entities[0], cpType,
                                            prefix + ".submeshId", (int)s);
                                        propCmd->setNoMerge();
                                        CommandHandle::get(project->getSelectedSceneId())->addCommand(propCmd);
                                    }
                                }
                                if (isSelected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                }

                endTable();
                ImGui::PopID();
            }
        }

        // --- Tiles Section ---
        ImGui::SeparatorText("Tiles");

        int activeTileCount = (int)tilemap.numTiles;

        beginTable(cpType, getLabelSize("Tiles"), "tilemap_tiles_header");
        propertyHeader("Tiles", -1, false, false);
        ImGui::Text("%d", activeTileCount);
        float tilesArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        if (drawSummaryAddButton(ICON_FA_PLUS " Add Tile##tilemap_tiles_add", tilesArrowWidth)) {
            int freeSlot = (int)tilemap.numTiles;
            if (tilemap.tiles.validIndex(freeSlot)) {
                float defaultWidth = 100.0f;
                float defaultHeight = 100.0f;
                if (tilemap.numTilesRect > 0 && tilemap.tilesRect[0].rect.getWidth() > 0.0f && tilemap.tilesRect[0].rect.getHeight() > 0.0f) {
                    defaultWidth = tilemap.tilesRect[0].rect.getWidth();
                    defaultHeight = tilemap.tilesRect[0].rect.getHeight();
                }
                editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();
                std::string prefix = "tiles[" + std::to_string(freeSlot) + "]";
                multiCmd->addPropertyCmd<std::string>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".name", "tile_" + std::to_string(freeSlot));
                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".width", defaultWidth);
                multiCmd->addPropertyCmd<float>(project, sceneProject->id, entities[0], cpType,
                    prefix + ".height", defaultHeight);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, entities[0], cpType,
                    "numTiles", (unsigned int)(freeSlot + 1));
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }
        ImGui::SameLine();
        if (ImGui::ArrowButton("##toggle_all_tiles", tilemapTilesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
            tilemapTilesExpanded = !tilemapTilesExpanded;
        }
        endTable();

        if (tilemapTilesExpanded) {
            RowSettings settingsTileFloat;
            settingsTileFloat.stepSize = 1.0f;

            for (unsigned int i = 0; i < tilemap.numTiles; i++) {
                ImGui::PushID(1000 + i);

                std::string label = "[" + std::to_string(i) + "] " + tilemap.tiles[i].name;
                std::string groupStr = "tile_" + std::to_string(i);
                std::string prefix = "tiles[" + std::to_string(i) + "]";

                bool isTileSelected = sceneProject->sceneRender &&
                    sceneProject->sceneRender->getSelectedTileIndex() == (int)i &&
                    sceneProject->sceneRender->getSelectedTileEntity() == entities[0];

                // sub-selected in the scene viewport: same marking as occluder points
                if (isTileSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubSelectionText);
                    ImGui::SeparatorText((std::string(ICON_FA_CARET_RIGHT) + " " + label).c_str());
                    ImGui::PopStyleColor();
                } else {
                    ImGui::SeparatorText(label.c_str());
                }
                if (isLastHeaderRowClicked()) {
                    syncSubSelection(sceneProject, entities[0], (int)i, -1);
                }

                beginTable(cpType, getLabelSize("Position"), groupStr);

                propertyHeader("Tile", -1, false, false);
                {
                    float previewSize = ImGui::GetFrameHeight() * 2.2f;
                    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                    ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
                    ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

                    Texture* tileTex = nullptr;
                    Rect tileRect;
                    if (tilemap.tiles[i].rectId >= 0 && tilemap.tiles[i].rectId < (int)tilemap.numTilesRect) {
                        const auto& rectData = tilemap.tilesRect[tilemap.tiles[i].rectId];
                        tileRect = rectData.rect;
                        if (meshComp && rectData.submeshId >= 0 && (unsigned int)rectData.submeshId < meshComp->numSubmeshes) {
                            tileTex = &meshComp->submeshes[rectData.submeshId].material.baseColorTexture;
                            if (tileTex->empty()) tileTex = nullptr;
                        }
                    }

                    bool hasFramePreview = tileTex && tileRect.getWidth() > 0.0f && tileRect.getHeight() > 0.0f;

                    float trailingWidth = arrowButtonSize.x
                            + ImGui::GetStyle().ItemSpacing.x + deleteButtonSize.x;
                    if (hasFramePreview) {
                        trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
                    }

                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
                    ImGui::SetCursorPosX(targetX);

                    if (hasFramePreview) {
                        drawSpriteFramePreview(tileTex, tileRect, ImVec2(previewSize, previewSize), "##tile_preview");
                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("%s", label.c_str());

                            float tooltipMaxSize = Theme::dpi(200.0f);
                            float scale = std::min(tooltipMaxSize / std::max(1.0f, tileRect.getWidth()),
                                                   tooltipMaxSize / std::max(1.0f, tileRect.getHeight()));
                            scale = std::min(scale, 1.0f);
                            ImVec2 tooltipSize(std::max(Theme::dpi(32.0f), tileRect.getWidth() * scale),
                                               std::max(Theme::dpi(32.0f), tileRect.getHeight() * scale));

                            drawSpriteFramePreview(tileTex, tileRect, tooltipSize, "##tile_preview_tooltip");
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                    }

                    if (ImGui::ArrowButton("##toggle_tile", tilemapTilesButtonGroups[groupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
                        tilemapTilesButtonGroups[groupStr] = !tilemapTilesButtonGroups[groupStr];
                    }
                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(ICON_FA_TRASH_CAN, deleteButtonSize)) {
                        Command* deleteCmd = ProjectUtils::buildDeleteTileCmd(project, sceneProject->id, entities[0], i);
                        if (deleteCmd) {
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(deleteCmd);
                        }
                        endTable();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor(2);
                        ImGui::PopID();
                        break;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                }

                if (tilemapTilesButtonGroups[groupStr]) {
                    propertyRow(RowPropertyType::String, cpType, prefix + ".name", "Name", sceneProject, entities);
                    propertyRow(RowPropertyType::Int, cpType, prefix + ".rectId", "Rect ID", sceneProject, entities, settingsInt);
                    propertyRow(RowPropertyType::Vector2, cpType, prefix + ".position", "Position", sceneProject, entities, settingsTileFloat);
                    propertyRow(RowPropertyType::Float, cpType, prefix + ".width", "Width", sceneProject, entities, settingsTileFloat);
                    propertyRow(RowPropertyType::Float, cpType, prefix + ".height", "Height", sceneProject, entities, settingsTileFloat);
                }

                endTable();
                ImGui::PopID();
            }
        }
    }
}

void editor::Properties::drawTerrainComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    TerrainComponent& terrain = sceneProject->scene->getComponent<TerrainComponent>(entities[0]);

    RowSettings textureSettings;
    textureSettings.secondColSize = -1;

    if (entities.size() == 1){
        if (ImGui::Button(ICON_FA_MOUNTAIN " Open Terrain Editor", ImVec2(-1, 0))){
            if (TerrainEditWindow* terrainEditWindow = Backend::getApp().getTerrainEditWindow()){
                terrainEditWindow->openForEntity(entities[0], sceneProject->id);
            }
        }
    }

    ImGui::SeparatorText("Maps");
    beginTable(cpType, getLabelSize("Height Map"), "terrain_maps");
    propertyRow(RowPropertyType::Texture, cpType, "heightMap", "Height Map", sceneProject, entities, textureSettings);
    endTable();

    // The Terrain Editor owns adding and removing these, so here they are only listed
    if (!terrain.blendMaps.empty()){
        ImGui::SeparatorText("Blend Maps");
        beginTable(cpType, getLabelSize("Blend Map 1"), "terrain_blendmaps");
        for (size_t i = 0; i < terrain.blendMaps.size(); i++){
            const std::string property = "blendMaps[" + std::to_string(i) + "]";
            const std::string label = "Blend Map " + std::to_string(i + 1);
            propertyRow(RowPropertyType::Texture, cpType, property, label, sceneProject, entities, textureSettings);
        }
        endTable();
    }

    if (!terrain.surfaceLayers.empty()){
        RowSettings settingsLayerFloat;
        settingsLayerFloat.secondColSize = 6 * ImGui::GetFontSize();

        ImGui::SeparatorText("Layers");
        for (size_t i = 0; i < terrain.surfaceLayers.size(); i++){
            const std::string property = "surfaceLayers[" + std::to_string(i) + "]";
            const std::string label = "Layer " + std::to_string(i + 1);
            if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)){
                continue;
            }
            ImGui::PushID((int)i);
            beginTable(cpType, getLabelSize("Occlusion Strength"), "terrain_layer");
            propertyRow(RowPropertyType::Bool, cpType, property + ".pbr", "Enable PBR", sceneProject, entities, textureSettings);
            propertyRow(RowPropertyType::Texture, cpType, property + ".colorTexture", "Color", sceneProject, entities, textureSettings);
            // A color layer blends its texture as it is; the rest belongs to PBR layers
            if (terrain.surfaceLayers[i].pbr){
                propertyRow(RowPropertyType::Color4L, cpType, property + ".colorFactor", "Tint", sceneProject, entities, textureSettings);
                propertyRow(RowPropertyType::Texture, cpType, property + ".normalTexture", "Normal", sceneProject, entities, textureSettings);
                propertyRow(RowPropertyType::Float, cpType, property + ".normalStrength", "Normal Strength", sceneProject, entities, settingsLayerFloat);
                propertyRow(RowPropertyType::Texture, cpType, property + ".roughnessTexture", "Roughness", sceneProject, entities, textureSettings);
                propertyRow(RowPropertyType::Float_0_1, cpType, property + ".roughnessFactor", "Roughness Factor", sceneProject, entities, settingsLayerFloat);
                propertyRow(RowPropertyType::Texture, cpType, property + ".metallicTexture", "Metallic", sceneProject, entities, textureSettings);
                propertyRow(RowPropertyType::Float_0_1, cpType, property + ".metallicFactor", "Metallic Factor", sceneProject, entities, settingsLayerFloat);
                propertyRow(RowPropertyType::Texture, cpType, property + ".occlusionTexture", "Occlusion", sceneProject, entities, textureSettings);
                propertyRow(RowPropertyType::Float_0_1, cpType, property + ".occlusionStrength", "Occlusion Strength", sceneProject, entities, settingsLayerFloat);
                propertyRow(RowPropertyType::Texture, cpType, property + ".heightTexture", "Height", sceneProject, entities, textureSettings);
            }
            // Tiling rides on every layer, PBR or not
            propertyRow(RowPropertyType::Vector2, cpType, property + ".uvScale", "UV Scale", sceneProject, entities, settingsLayerFloat);
            propertyRow(RowPropertyType::Vector2, cpType, property + ".uvOffset", "UV Offset", sceneProject, entities, settingsLayerFloat);
            endTable();
            ImGui::PopID();
            ImGui::TreePop();
        }
    }

    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsResolution = settingsFloat;
    settingsResolution.stepSize = 1.0f;
    settingsResolution.format = "%.0f";

    // The engine rounds the resolution to a multiple of 4 and writes it back (see
    // createOrUpdateTerrain), so drag by 4 to not fight the value the row is rebound to.
    RowSettings settingsGridResolution = settingsResolution;
    settingsGridResolution.stepSize = 4.0f;

    RowSettings settingsInt;
    settingsInt.stepSize = 1.0f;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    // Bound the quadtree so it can never request an impossible node allocation
    // (see the MAX_TERRAIN_NODES guard in MeshSystem). rootGridSize^2 must fit in
    // MAX_TERRAINGRID (16 -> max 4); Levels is capped so even at the largest root
    // grid the node count stays within the engine budget, so any value set here
    // always renders instead of tripping the engine's rejection.
    RowSettings settingsRootGrid = settingsInt;
    settingsRootGrid.intMin = 1;
    settingsRootGrid.intMax = 4;

    RowSettings settingsLevels = settingsInt;
    settingsLevels.intMin = 1;
    settingsLevels.intMax = 9;

    ImGui::SeparatorText("Shape");
    beginTable(cpType, getLabelSize("Root Grid Size"), "terrain_shape");
    propertyRow(RowPropertyType::FloatPositive, cpType, "terrainSize", "Size", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::FloatPositive, cpType, "maxHeight", "Max Height", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::FloatPositive, cpType, "resolution", "Resolution", sceneProject, entities, settingsGridResolution);
    propertyRow(RowPropertyType::Int, cpType, "rootGridSize", "Root Grid Size", sceneProject, entities, settingsRootGrid);
    propertyRow(RowPropertyType::Int, cpType, "levels", "Levels", sceneProject, entities, settingsLevels);
    propertyRow(RowPropertyType::Vector2, cpType, "offset", "Offset", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Texturing");
    beginTable(cpType, getLabelSize("Detail Tiles"), "terrain_tiling");
    propertyRow(RowPropertyType::FloatPositive, cpType, "textureBaseTiles", "Base Tiles", sceneProject, entities, settingsResolution);
    propertyRow(RowPropertyType::FloatPositive, cpType, "textureDetailTiles", "Detail Tiles", sceneProject, entities, settingsResolution);
    endTable();

    ImGui::SeparatorText("Ranges");
    beginTable(cpType, getLabelSize("Auto Ranges"), "terrain_ranges_auto");
    propertyRow(RowPropertyType::Bool, cpType, "autoSetRanges", "Auto Ranges", sceneProject, entities);
    endTable();

    if (!terrain.autoSetRanges) {
        drawTrackValues<TerrainComponent, float>(cpType, sceneProject, entities, RowPropertyType::FloatPositive, 0.0f, "terrain", &TerrainComponent::ranges, "ranges");
    }
}

void editor::Properties::drawLightComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    LightComponent& light = sceneProject->scene->getComponent<LightComponent>(entities[0]);

    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsCone;
    settingsCone.secondColSize = 6 * ImGui::GetFontSize();
    settingsCone.format = "%.1f";

    RowSettings settingsLightType;
    settingsLightType.enumEntries = &entriesLightType;

    beginTable(cpType, getLabelSize("Inner cone"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsLightType);
    propertyRow(RowPropertyType::Float, cpType, "intensity", "Intensity", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "range", "Range", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Color3L, cpType, "color", "Color", sceneProject, entities);
    if (light.type != LightType::POINT){
        propertyRow(RowPropertyType::Direction, cpType, "direction", "Direction", sceneProject, entities);
    }
    if (light.type == LightType::SPOT){
        RowSettings settingsMask;
        settingsMask.allowCameraTexture = false;
        settingsMask.allowTextureSettings = false;
        settingsMask.help = "Optional image projected by the spotlight. Alpha controls intensity when transparency is present; otherwise luminance is used. Empty uses the default circular cone.";
        propertyRow(RowPropertyType::Texture, cpType, "spotMask", "Mask", sceneProject, entities, settingsMask);
        if (light.spotMask.empty()){
            propertyRow(RowPropertyType::HalfCone, cpType, "innerConeCos", "Inner Cone", sceneProject, entities, settingsCone);
            propertyRow(RowPropertyType::HalfCone, cpType, "outerConeCos", "Outer Cone", sceneProject, entities, settingsCone);
        }else{
            propertyRow(RowPropertyType::HalfCone, cpType, "outerConeCos", "Angle Cone", sceneProject, entities, settingsCone);
        }
    }
    endTable();

    ImGui::SeparatorText("Shadow settings");

    RowSettings settingsBias;
    settingsBias.stepSize = 0.000001f;
    settingsBias.secondColSize = 6 * ImGui::GetFontSize();
    settingsBias.format = "%.6f";

    RowSettings settingsMapRes;
    settingsMapRes.sliderValues = &po2Values;

    RowSettings settingsCascade;
    settingsCascade.sliderValues = &cascadeValues;

    beginTable(cpType, getLabelSize("Map Resolution"), "shadow_settings_table");
    propertyRow(RowPropertyType::Bool, cpType, "shadows", "Enabled", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "shadowBias", "Bias", sceneProject, entities, settingsBias);
    propertyRow(RowPropertyType::UIntSlider, cpType, "mapResolution", "Map Resolution", sceneProject, entities, settingsMapRes);
    propertyRow(RowPropertyType::UIntSlider, cpType, "numShadowCascades", "Num Cascades", sceneProject, entities, settingsCascade);

    propertyHeader("Shadow Camera");
    if (ImGui::Button(ICON_FA_GEAR)){
        ImGui::OpenPopup("menusettings_shadow_camera");
    }

    ImGui::SetNextWindowSizeConstraints(ImVec2(14 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::BeginPopup("menusettings_shadow_camera")){
        ImGui::Text("Shadow camera settings");
        ImGui::Separator();

        RowSettings settingsFloat;
        settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

        beginTable(cpType, getLabelSize("Camera Near"), "shadow_camera_popup");

        propertyRow(RowPropertyType::Bool, cpType, "automaticShadowCamera", "Automatic", sceneProject, entities);
        ImGui::BeginDisabled(light.automaticShadowCamera);
        propertyRow(RowPropertyType::Float, cpType, "shadowCameraNear", "Camera Near", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "shadowCameraFar", "Camera Far", sceneProject, entities, settingsFloat);
        ImGui::EndDisabled();

        endTable();

        ImGui::EndPopup();
    }
    endTable();
}

void editor::Properties::drawLight2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsHeight = settingsFloat;
    settingsHeight.help = "Virtual Z height of the light above the 2D plane. Gives normal maps a direction to respond to; 0 disables normal response (pure radial light).";

    beginTable(cpType, getLabelSize("Intensity"));
    propertyRow(RowPropertyType::Float, cpType, "intensity", "Intensity", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "range", "Range", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "falloff", "Falloff", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::Float, cpType, "height", "Height", sceneProject, entities, settingsHeight);
    propertyRow(RowPropertyType::Color3L, cpType, "color", "Color", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Shadow settings");

    RowSettings settingsBias;
    settingsBias.stepSize = 0.001f;
    settingsBias.secondColSize = 6 * ImGui::GetFontSize();
    settingsBias.format = "%.4f";

    RowSettings settingsMapRes;
    settingsMapRes.sliderValues = &po2Values;

    beginTable(cpType, getLabelSize("Map Resolution"), "shadow2d_settings_table");
    propertyRow(RowPropertyType::Bool, cpType, "shadows", "Enabled", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "shadowBias", "Bias", sceneProject, entities, settingsBias);
    propertyRow(RowPropertyType::Float, cpType, "shadowSoftness", "Softness", sceneProject, entities, settingsFloat);
    propertyRow(RowPropertyType::UIntSlider, cpType, "mapResolution", "Map Resolution", sceneProject, entities, settingsMapRes);
    endTable();
}

void editor::Properties::drawOccluder2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Occluder2DComponent& occluder = sceneProject->scene->getComponent<Occluder2DComponent>(entities[0]);

    RowSettings settingsShape;
    settingsShape.enumEntries = &entriesOccluder2DShape;
    settingsShape.help = "Auto Quad derives the occluder outline from this entity's mesh bounds; Polygon uses the point list below (local space).";

    beginTable(cpType, getLabelSize("Enabled"));
    propertyRow(RowPropertyType::Enum, cpType, "shape", "Shape", sceneProject, entities, settingsShape);
    if (occluder.shape == Occluder2DShape::POLYGON){
        propertyRow(RowPropertyType::Bool, cpType, "closed", "Closed", sceneProject, entities);
    }
    propertyRow(RowPropertyType::Bool, cpType, "enabled", "Enabled", sceneProject, entities);
    endTable();

    if (occluder.shape == Occluder2DShape::POLYGON && entities.size() == 1){
        Entity entity = entities[0];

        Command* pointsCmd = nullptr;
        auto setPoints = [&](const std::vector<Vector2>& newPoints, bool merge){
            editor::PropertyCmd<std::vector<Vector2>>* cmd = new editor::PropertyCmd<std::vector<Vector2>>(project, sceneProject->id, entity, cpType, "points", newPoints);
            if (!merge){
                cmd->setNoMerge();
            }
            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
            pointsCmd = cmd;
        };

        ImGui::SeparatorText("Points");

        // point sub-selected in the scene viewport (click on a handle)
        int selectedPoint = -1;
        if (sceneProject->sceneRender && sceneProject->sceneRender->getSelectedOccluderPointEntity() == entity){
            selectedPoint = sceneProject->sceneRender->getSelectedOccluderPointIndex();
        }

        beginTable(cpType, getLabelSize(ICON_FA_CARET_RIGHT " Point 000"), "occluder2d_points_table");
        for (size_t i = 0; i < occluder.points.size(); i++){
            bool pointSelected = ((int)i == selectedPoint);

            ImGui::PushID(static_cast<int>(i));
            if (pointSelected){
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubSelectionText);
                propertyHeader(std::string(ICON_FA_CARET_RIGHT) + " Point " + std::to_string(i), -1, false, false);
                ImGui::PopStyleColor();
            }else{
                propertyHeader("Point " + std::to_string(i), -1, false, false);
            }

            float deleteButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float value[2] = {occluder.points[i].x, occluder.points[i].y};
            ImGui::SetNextItemWidth(std::max(0.0f, ImGui::GetContentRegionAvail().x - deleteButtonWidth - ImGui::GetStyle().ItemSpacing.x));
            if (ImGui::DragFloat2("##occluder2d_point", value, 1.0f, 0.0f, 0.0f, "%.2f")){
                std::vector<Vector2> newPoints = occluder.points;
                newPoints[i] = Vector2(value[0], value[1]);
                setPoints(newPoints, true);
            }
            if (ImGui::IsItemDeactivatedAfterEdit()){
                if (pointsCmd){
                    pointsCmd->setNoMerge();
                    pointsCmd = nullptr;
                }
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::Button(ICON_FA_TRASH_CAN "##occluder2d_point_delete")){
                std::vector<Vector2> newPoints = occluder.points;
                newPoints.erase(newPoints.begin() + static_cast<std::ptrdiff_t>(i));
                if (sceneProject->sceneRender && (int)i <= selectedPoint){
                    sceneProject->sceneRender->clearOccluderPointSelection();
                }
                setPoints(newPoints, false);
                ImGui::PopStyleColor(2);
                ImGui::PopID();
                break;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopID();
        }
        endTable();

        if (ImGui::Button(ICON_FA_PLUS " Add Point##occluder2d", ImVec2(std::max(0.0f, ImGui::GetContentRegionAvail().x), 0))){
            std::vector<Vector2> newPoints = occluder.points;
            if (newPoints.size() >= 2){
                // continue the outline: offset from the last edge so the new point is visible
                Vector2 last = newPoints[newPoints.size() - 1];
                Vector2 prev = newPoints[newPoints.size() - 2];
                Vector2 dir = last - prev;
                newPoints.push_back(last + dir * 0.5f);
            }else if (newPoints.size() == 1){
                newPoints.push_back(newPoints[0] + Vector2(50.0f, 0.0f));
            }else{
                newPoints.push_back(Vector2(0.0f, 0.0f));
            }
            setPoints(newPoints, false);
        }
    }
}

void editor::Properties::drawFogComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    FogComponent& fog = sceneProject->scene->getComponent<FogComponent>(entities[0]);

    RowSettings settingsFogType;
    settingsFogType.enumEntries = &entriesFogType;

    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsDensity = settingsFloat;
    settingsDensity.stepSize = 0.001f;
    settingsDensity.format = "%.4f";

    beginTable(cpType, getLabelSize("Linear Start"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Type", sceneProject, entities, settingsFogType);
    propertyRow(RowPropertyType::Color3L, cpType, "color", "Color", sceneProject, entities);
    if (fog.type == FogType::LINEAR) {
        propertyRow(RowPropertyType::Float, cpType, "linearStart", "Linear Start", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "linearEnd", "Linear End", sceneProject, entities, settingsFloat);
    } else {
        propertyRow(RowPropertyType::Float, cpType, "density", "Density", sceneProject, entities, settingsDensity);
    }
    endTable();
}

void editor::Properties::drawMirrorComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings normalSettings;
    normalSettings.help = "Reflecting surface normal in local space (the mirror plane is this normal at the entity's position). Flip its sign if the reflection clips the wrong side. The reflection camera is created automatically.";

    beginTable(cpType, getLabelSize("Normal"));
    propertyRow(RowPropertyType::Direction, cpType, "normal", "Normal", sceneProject, entities, normalSettings);
    endTable();
}

void editor::Properties::drawReflectionProbeComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    ReflectionProbeComponent& probe = sceneProject->scene->getComponent<ReflectionProbeComponent>(entities[0]);

    RowSettings modeSettings;
    modeSettings.enumEntries = &entriesReflectionProbeMode;
    RowSettings updateSettings;
    updateSettings.enumEntries = &entriesReflectionProbeUpdateMode;
    RowSettings resolutionSettings;
    resolutionSettings.sliderValues = &probeResolutionValues;
    RowSettings floatSettings;
    floatSettings.secondColSize = 6 * ImGui::GetFontSize();
    RowSettings boxOffsetSettings;
    boxOffsetSettings.help = "Moves the influence box in local space; the cubemap is still captured at the entity origin. Large offsets can increase box-projection distortion near the volume edges.";
    RowSettings blendSettings = floatSettings;
    blendSettings.help = "Runtime blending is limited to the influence box's smallest world-space half-extent. The authored value is preserved so later box or scale changes can restore it.";

    beginTable(cpType, getLabelSize("Update Interval"), "reflection_probe_main");
    propertyRow(RowPropertyType::Enum, cpType, "mode", "Mode", sceneProject, entities, modeSettings);
    if (probe.mode == ReflectionProbeMode::DYNAMIC){
        propertyRow(RowPropertyType::Enum, cpType, "updateMode", "Update", sceneProject, entities, updateSettings);
        if (probe.updateMode == ReflectionProbeUpdateMode::INTERVAL){
            propertyRow(RowPropertyType::FloatPositive, cpType, "updateInterval", "Update Interval", sceneProject, entities, floatSettings);
        }
    }else{
        propertyRow(RowPropertyType::TextureCube, cpType, "texture", "Cubemap", sceneProject, entities);
    }
    propertyRow(RowPropertyType::FloatPositive, cpType, "intensity", "Intensity", sceneProject, entities, floatSettings);
    propertyRow(RowPropertyType::Int, cpType, "priority", "Priority", sceneProject, entities);
    endTable();

    ImGui::SeparatorText("Influence");
    beginTable(cpType, getLabelSize("Blend Distance"), "reflection_probe_influence");
    propertyRow(RowPropertyType::Vector3, cpType, "boxOffset", "Box Offset", sceneProject, entities, boxOffsetSettings);
    propertyRow(RowPropertyType::Vector3, cpType, "boxSize", "Box Size", sceneProject, entities);
    propertyRow(RowPropertyType::FloatPositive, cpType, "blendDistance", "Blend Distance", sceneProject, entities, blendSettings);
    endTable();

    if (entities.size() == 1){
        Transform* transform = sceneProject->scene->findComponent<Transform>(entities[0]);
        if (transform){
            Vector3 half(
                std::fabs(probe.boxSize.x * transform->worldScale.x) * 0.5f,
                std::fabs(probe.boxSize.y * transform->worldScale.y) * 0.5f,
                std::fabs(probe.boxSize.z * transform->worldScale.z) * 0.5f);
            float maxBlendDistance = std::max(0.0f, std::min(half.x, std::min(half.y, half.z)));
            if (probe.blendDistance > maxBlendDistance + 0.0001f){
                ImGui::TextDisabled("Effective blend distance: %.2f (limited by box size)", maxBlendDistance);
            }
        }
    }

    ImGui::SeparatorText("Capture");
    beginTable(cpType, getLabelSize("Resolution"), "reflection_probe_capture");
    propertyRow(RowPropertyType::UIntSlider, cpType, "resolution", "Resolution", sceneProject, entities, resolutionSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "nearClip", "Near", sceneProject, entities, floatSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "farClip", "Far", sceneProject, entities, floatSettings);
    propertyRow(RowPropertyType::Bool, cpType, "includeSky", "Include Sky", sceneProject, entities);
    endTable();

    if (entities.size() == 1 && ImGui::Button(ICON_FA_ARROWS_ROTATE " Refresh Probe", ImVec2(ImGui::GetContentRegionAvail().x, 0))){
        probe.needUpdate = true;
        probe.captureRevision++;
    }
}

void editor::Properties::drawWaterComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    WaterComponent& water = sceneProject->scene->getComponent<WaterComponent>(entities[0]);

    RowSettings floatSettings;
    floatSettings.secondColSize = 6 * ImGui::GetFontSize();

    drawCustomShaderRow(cpType, ShaderType::WATER, sceneProject, entities);

    ImGui::SeparatorText("Surface");
    beginTable(cpType, getLabelSize("Subdivisions"), "water_surface");
    RowSettings subdivisionSettings;
    subdivisionSettings.sliderValues = &waterSubdivisionValues;
    subdivisionSettings.help = "Grid cells along each side. Short waves need more cells to keep their shape.";
    propertyRow(RowPropertyType::Vector2, cpType, "size", "Size", sceneProject, entities);
    propertyRow(RowPropertyType::UIntSlider, cpType, "subdivisions", "Subdivisions", sceneProject, entities, subdivisionSettings);
    endTable();

    ImGui::SeparatorText("Color");
    beginTable(cpType, getLabelSize("Shallow Color"), "water_color");
    RowSettings depthFadeSettings = floatSettings;
    depthFadeSettings.help = "Depth where the deep color takes over. Shallower water is lighter and clearer.";
    RowSettings shallowSettings;
    shallowSettings.help = "Color of shallow water. With Refraction or Underwater, the tint of the scene seen through it.";
    propertyRow(RowPropertyType::Color3L, cpType, "shallowColor", "Shallow Color", sceneProject, entities, shallowSettings);
    propertyRow(RowPropertyType::Color3L, cpType, "deepColor", "Deep Color", sceneProject, entities);
    propertyRow(RowPropertyType::FloatPositive, cpType, "depthFade", "Depth Fade", sceneProject, entities, depthFadeSettings);
    endTable();

    ImGui::SeparatorText("Waves");
    beginTable(cpType, getLabelSize("Steepness"), "water_waves");
    RowSettings heightSettings = floatSettings;
    heightSettings.stepSize = 0.01f;
    heightSettings.help = "How far the crests rise above the water level.";
    RowSettings lengthSettings = floatSettings;
    lengthSettings.help = "Length of the longest wave. Longer waves also travel faster.";
    RowSettings directionSettings = floatSettings;
    directionSettings.stepSize = 1.0f;
    directionSettings.format = "%.0f";
    directionSettings.help = "Angle around Y the waves travel toward. 0 is +X.";
    RowSettings steepnessSettings = floatSettings;
    steepnessSettings.stepSize = 0.01f;
    steepnessSettings.help = "0 gives round waves, 1 sharp crests. Lowered where the waves would fold over.";
    propertyRow(RowPropertyType::FloatPositive, cpType, "waveHeight", "Height", sceneProject, entities, heightSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "waveLength", "Length", sceneProject, entities, lengthSettings);
    propertyRow(RowPropertyType::Float, cpType, "waveSpeed", "Speed", sceneProject, entities, floatSettings);
    propertyRow(RowPropertyType::Float, cpType, "waveDirection", "Direction", sceneProject, entities, directionSettings);
    propertyRow(RowPropertyType::Float_0_1, cpType, "waveSteepness", "Steepness", sceneProject, entities, steepnessSettings);
    endTable();

    ImGui::SeparatorText("Ripples");
    beginTable(cpType, getLabelSize("Normal Map"), "water_ripples");
    RowSettings normalSettings;
    normalSettings.allowCameraTexture = false;
    normalSettings.help = "Tiling normal map of the small ripples, the built-in one when empty. Use a mipmap filter so distant ripples do not shimmer.";
    RowSettings tileSettings = floatSettings;
    tileSettings.help = "World size of one tile of the normal map.";
    RowSettings strengthSettings = floatSettings;
    strengthSettings.stepSize = 0.01f;
    propertyRow(RowPropertyType::Texture, cpType, "normalTexture", "Normal Map", sceneProject, entities, normalSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "normalScale", "Tile Size", sceneProject, entities, tileSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "normalStrength", "Strength", sceneProject, entities, strengthSettings);
    propertyRow(RowPropertyType::Float, cpType, "rippleSpeed", "Speed", sceneProject, entities, floatSettings);
    endTable();

    ImGui::SeparatorText("Reflection");
    beginTable(cpType, getLabelSize("Planar Reflection"), "water_reflection");
    RowSettings unitSettings = floatSettings;
    unitSettings.stepSize = 0.01f;
    RowSettings roughnessSettings = unitSettings;
    roughnessSettings.help = "Spread of the sun highlight. Low values give sharp glints.";
    RowSettings shadowSettings;
    shadowSettings.help = "Shadows of the scene lights darken the sun glints and the light under the surface.";
    RowSettings planarSettings;
    planarSettings.help = "Reflects the scene, not only the sky. Draws the scene a second time at half resolution.";
    RowSettings distortionSettings = floatSettings;
    distortionSettings.stepSize = 0.005f;
    distortionSettings.format = "%.3f";
    distortionSettings.help = "How much the ripples bend the planar reflection.";
    propertyRow(RowPropertyType::FloatPositive, cpType, "reflectivity", "Reflectivity", sceneProject, entities, unitSettings);
    propertyRow(RowPropertyType::FloatPositive, cpType, "specularIntensity", "Specular", sceneProject, entities, unitSettings);
    propertyRow(RowPropertyType::Float_0_1, cpType, "roughness", "Roughness", sceneProject, entities, roughnessSettings);
    propertyRow(RowPropertyType::Bool, cpType, "receiveShadows", "Receive Shadows", sceneProject, entities, shadowSettings);
    propertyRow(RowPropertyType::Bool, cpType, "planarReflection", "Planar Reflection", sceneProject, entities, planarSettings);
    if (water.planarReflection){
        propertyRow(RowPropertyType::FloatPositive, cpType, "reflectionDistortion", "Distortion", sceneProject, entities, distortionSettings);
    }
    endTable();

    ImGui::SeparatorText("Refraction");
    beginTable(cpType, getLabelSize("Caustics Scale"), "water_refraction");
    RowSettings refractionSettings;
    refractionSettings.help = "Shows the scene behind the water, bent by the ripples and tinted by the water it crosses. Copies the scene once per frame.";
    RowSettings refractionDistortionSettings = distortionSettings;
    refractionDistortionSettings.help = "How much the ripples bend the scene behind the water.";
    RowSettings causticsSettings = unitSettings;
    causticsSettings.help = "Sunlight patterns on the floor seen through the water. Needs Depth Effects.";
    RowSettings causticsScaleSettings = floatSettings;
    causticsScaleSettings.help = "World size of one cell of the caustics.";
    propertyRow(RowPropertyType::Bool, cpType, "refraction", "Refraction", sceneProject, entities, refractionSettings);
    if (water.refraction){
        propertyRow(RowPropertyType::FloatPositive, cpType, "refractionDistortion", "Distortion", sceneProject, entities, refractionDistortionSettings);
        propertyRow(RowPropertyType::FloatPositive, cpType, "caustics", "Caustics", sceneProject, entities, causticsSettings);
        propertyRow(RowPropertyType::FloatPositive, cpType, "causticsScale", "Caustics Scale", sceneProject, entities, causticsScaleSettings);
    }
    endTable();

    ImGui::SeparatorText("Underwater");
    beginTable(cpType, getLabelSize("Underwater"), "water_underwater");
    RowSettings underwaterSettings;
    underwaterSettings.help = "A camera below the surface sees the scene fade into the water with distance. Without Depth Effects the fade is even.";
    propertyRow(RowPropertyType::Bool, cpType, "underwater", "Underwater", sceneProject, entities, underwaterSettings);
    endTable();

    ImGui::SeparatorText("Foam");
    beginTable(cpType, getLabelSize("Depth Effects"), "water_foam");
    RowSettings depthSettings;
    depthSettings.help = "Shore foam, soft edges and depth tint from the scene depth. Adds a depth pass when SSAO and SSR are off.";
    RowSettings shoreSettings = floatSettings;
    shoreSettings.help = "Water depth the shore foam covers. Needs Depth Effects.";
    RowSettings crestSettings = unitSettings;
    crestSettings.help = "Share of the highest crests that turn to foam.";
    propertyRow(RowPropertyType::Bool, cpType, "depthEffects", "Depth Effects", sceneProject, entities, depthSettings);
    propertyRow(RowPropertyType::Color3L, cpType, "foamColor", "Foam Color", sceneProject, entities);
    propertyRow(RowPropertyType::FloatPositive, cpType, "shoreFoam", "Shore Foam", sceneProject, entities, shoreSettings);
    propertyRow(RowPropertyType::Float_0_1, cpType, "crestFoam", "Crest Foam", sceneProject, entities, crestSettings);
    endTable();

    ImGui::SeparatorText("Buoyancy");
    beginTable(cpType, getLabelSize("Buoyancy"), "water_buoyancy");
    RowSettings buoyancySettings;
    buoyancySettings.help = "Floats the dynamic bodies that have Body3D buoyancy.";
    RowSettings buoyancyDepthSettings = floatSettings;
    buoyancyDepthSettings.help = "How far below the surface bodies still float. 0 has no bottom. Where waters overlap, the one with the highest bottom is used.";
    propertyRow(RowPropertyType::Bool, cpType, "buoyancy", "Buoyancy", sceneProject, entities, buoyancySettings);
    if (water.buoyancy){
        propertyRow(RowPropertyType::FloatPositive, cpType, "buoyancyDepth", "Depth", sceneProject, entities, buoyancyDepthSettings);
    }
    endTable();
}

void editor::Properties::drawWaterExclusionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    WaterExclusionComponent& exclusion = sceneProject->scene->getComponent<WaterExclusionComponent>(entities[0]);

    RowSettings shapeSettings;
    shapeSettings.enumEntries = &entriesWaterExclusionShape;
    shapeSettings.help = "Hull wraps the meshes of this entity, like a boat, and follows them when they change. Box and Sphere fill a volume of their own.";

    beginTable(cpType, getLabelSize("Center"), "water_exclusion");
    propertyRow(RowPropertyType::Enum, cpType, "shape", "Shape", sceneProject, entities, shapeSettings);
    if (exclusion.shape != WaterExclusionShape::HULL){
        propertyRow(RowPropertyType::Vector3, cpType, "center", "Center", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "size", "Size", sceneProject, entities);
    }
    endTable();

    // after a build was tried
    if (entities.size() == 1 && exclusion.shape == WaterExclusionShape::HULL && exclusion.hullSignature != 0 && exclusion.hullPlanes.empty()){
        ImGui::TextDisabled("No mesh on this entity to wrap");
    }
}

void editor::Properties::drawScriptComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    if (entities.empty()) return;

    ScriptComponent& firstScriptComp = sceneProject->scene->getComponent<ScriptComponent>(entities[0]);

    // Identify common scripts by index
    std::vector<size_t> commonIndices;
    for (size_t i = 0; i < firstScriptComp.scripts.size(); i++) {
        const ScriptEntry& refScript = firstScriptComp.scripts[i];
        bool isCommon = true;
        for (size_t e = 1; e < entities.size(); e++) {
            ScriptComponent& otherScriptComp = sceneProject->scene->getComponent<ScriptComponent>(entities[e]);
            if (i >= otherScriptComp.scripts.size()) {
                isCommon = false;
                break;
            }
            const ScriptEntry& otherScript = otherScriptComp.scripts[i];
            // headerPath is where the properties come from, so entries not sharing it differ
            if (refScript.type != otherScript.type || 
                refScript.className != otherScript.className || 
                refScript.path != otherScript.path ||
                refScript.headerPath != otherScript.headerPath) {
                isCommon = false;
                break;
            }
        }
        if (isCommon) {
            commonIndices.push_back(i);
        }
    }

    beginTable(cpType, getLabelSize("Scripts"), "script_component_header");
    propertyHeader("Scripts", -1, false, false);
    ImGui::Text("%zu", firstScriptComp.scripts.size());
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Script##script_add")) {
        ImGui::OpenPopup("script_add_menu");
    }
    if (ImGui::BeginPopup("script_add_menu")) {
        bool addLua = ImGui::MenuItem("Lua Script");
        bool addCpp = ImGui::MenuItem("C++ Script");
        if (addLua || addCpp) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& entity : entities) {
                ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                std::vector<ScriptEntry> newScripts = sc.scripts;
                ScriptEntry entry;
                entry.type = addLua ? ScriptType::LUA : ScriptType::CPP;
                entry.enabled = true;
                newScripts.push_back(entry);
                multiCmd->addPropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }
        ImGui::EndPopup();
    }
    endTable();

    if (commonIndices.empty()) {
        if (entities.size() > 1) {
            ImGui::TextDisabled("Select a single entity to view script details");
        }
        return;
    }

    bool removedScriptThisFrame = false;

    for (size_t scriptIdx : commonIndices) {
        if (removedScriptThisFrame) break; // Avoid using invalidated references after removal

        ScriptEntry& script = firstScriptComp.scripts[scriptIdx];

        ImGui::PushID(static_cast<int>(scriptIdx));

        std::string scriptLabel = script.className.empty() ? "Unnamed Script" : script.className;
        std::string typeLabel;
        if (script.type == ScriptType::CPP) {
            typeLabel = " [C++]";
        } else if (script.type == ScriptType::LUA) {
            typeLabel = " [Lua]";
        }

        const float indentation = 10.0f;

        // Indent to show scripts are nested inside ScriptComponent
        ImGui::Indent(indentation);

        // Custom styling for script headers
        ImGui::PushStyleColor(ImGuiCol_Header, Theme::Colors::NestedHeader);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::Colors::NestedHeaderHovered);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::Colors::NestedHeaderActive);

        // Add icon to distinguish from component headers
        std::string headerText = ICON_FA_FILE_CODE " " + scriptLabel + typeLabel;

        // AllowOverlap so the trailing trash button can receive clicks
        bool headerOpen = ImGui::CollapsingHeader(
            headerText.c_str(),
            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        const ImVec2 headerMin = ImGui::GetItemRectMin();
        const ImVec2 headerMax = ImGui::GetItemRectMax();
        const ImVec2 cursorAfterHeader = ImGui::GetCursorPos();

        auto removeScriptAtIndex = [&]() {
            for (const Entity& entity : entities) {
                ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                std::vector<ScriptEntry> newScripts = sc.scripts;
                if (scriptIdx < newScripts.size()) {
                    newScripts.erase(newScripts.begin() + scriptIdx);
                    project->updateScriptProperties(sceneProject, entity, newScripts);
                    cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                }
            }
            if (cmd) cmd->setNoMerge();
            removedScriptThisFrame = true;
        };

        // Context menu must run while the header is still the last item
        if (ImGui::BeginPopupContextItem(("script_options_menu_" + std::to_string(scriptIdx)).c_str())) {
            ImGui::TextDisabled("Script options");
            ImGui::Separator();

            bool canMoveUp = scriptIdx > 0;
            if (ImGui::MenuItem(ICON_FA_ARROW_UP " Move Up", nullptr, false, canMoveUp)) {
                for (const Entity& entity : entities) {
                    ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                    std::vector<ScriptEntry> newScripts = sc.scripts;
                    if (scriptIdx < newScripts.size() && scriptIdx > 0) {
                        std::swap(newScripts[scriptIdx], newScripts[scriptIdx - 1]);

                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                }
                if (cmd) cmd->setNoMerge();
            }

            bool canMoveDown = scriptIdx < firstScriptComp.scripts.size() - 1;
            if (ImGui::MenuItem(ICON_FA_ARROW_DOWN " Move Down", nullptr, false, canMoveDown)) {
                for (const Entity& entity : entities) {
                    ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                    std::vector<ScriptEntry> newScripts = sc.scripts;
                    if (scriptIdx < newScripts.size() - 1) {
                        std::swap(newScripts[scriptIdx], newScripts[scriptIdx + 1]);

                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                }
                if (cmd) cmd->setNoMerge();
            }

            if (ImGui::MenuItem(ICON_FA_TRASH " Remove")) {
                removeScriptAtIndex();
            }

            ImGui::EndPopup();
        }

        // Trash button overlaid on the right of the collapsing header
        if (!removedScriptThisFrame) {
            const float framePadX = ImGui::GetStyle().FramePadding.x / 4.0f;
            const ImVec2 deleteButtonSize(ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + framePadX * 2.0f, 0.0f);

            ImGui::SetCursorScreenPos(ImVec2(
                headerMax.x - deleteButtonSize.x,
                headerMin.y + (headerMax.y - headerMin.y - ImGui::GetFrameHeight()) * 0.5f));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadX, ImGui::GetStyle().FramePadding.y));
            const bool removeClicked = ImGui::Button(ICON_FA_TRASH_CAN "##delete_script", deleteButtonSize);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove script");
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            if (removeClicked) {
                removeScriptAtIndex();
            }
        }

        if (headerOpen && !removedScriptThisFrame) {
            ImGui::SetCursorPos(cursorAfterHeader);
            ImGui::Unindent(indentation); // Unindent for content

            beginTable(cpType, getLabelSize("Script", false), "script_" + std::to_string(scriptIdx));

            // Path and enabled status
            propertyHeader("Script");

            // Enabled Checkbox Logic
            bool allEnabled = true;
            bool anyEnabled = false;
            for (const Entity& e : entities) {
                bool en = sceneProject->scene->getComponent<ScriptComponent>(e).scripts[scriptIdx].enabled;
                if (en) anyEnabled = true;
                else allEnabled = false;
            }

            bool enabled = allEnabled;
            bool mixed = anyEnabled && !allEnabled;

            if (mixed) ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::Checkbox(("##script_enabled_" + std::to_string(scriptIdx)).c_str(), &enabled);
            if (mixed) ImGui::PopStyleColor();

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                std::string propName = "scripts[" + std::to_string(scriptIdx) + "].enabled";
                for (const Entity& entity : entities) {
                    cmd = new PropertyCmd<bool>(project, sceneProject->id, entity, ComponentType::ScriptComponent, propName, enabled);
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                }
                if (cmd) cmd->setNoMerge();
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(script.className.c_str());

            //ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 3.0, ImGui::GetStyle().FramePadding.y / 2.0));

            if (ImGui::Button((ICON_FA_PEN_TO_SQUARE "##edit_name_" + std::to_string(scriptIdx)).c_str())) {
                ImGui::OpenPopup(("Edit Script##" + std::to_string(scriptIdx)).c_str());
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Edit script details");
            }

            ImGui::PopStyleVar();

            ImGui::SetNextWindowSizeConstraints(ImVec2(23 * ImGui::GetFontSize(), 0), ImVec2(FLT_MAX, FLT_MAX));
            if (ImGui::BeginPopup(("Edit Script##" + std::to_string(scriptIdx)).c_str())) {
                ImGui::Text("Edit Script Details");
                ImGui::Separator();

                static char nameBuffer[128];
                static char sourceBuffer[256];
                static char headerBuffer[256];

                std::filesystem::path projectPath = project->getProjectPath();
                std::filesystem::path srcPath(script.path);
                std::filesystem::path hdrPath(script.headerPath);
                if (ImGui::IsWindowAppearing()) {
                    strncpy(nameBuffer, script.className.c_str(), sizeof(nameBuffer) - 1);
                    nameBuffer[sizeof(nameBuffer) - 1] = '\0';

                    strncpy(sourceBuffer, srcPath.filename().string().c_str(), sizeof(sourceBuffer) - 1);
                    sourceBuffer[sizeof(sourceBuffer) - 1] = '\0';

                    strncpy(headerBuffer, hdrPath.filename().string().c_str(), sizeof(headerBuffer) - 1);
                    headerBuffer[sizeof(headerBuffer) - 1] = '\0';
                }

                bool changed = false;
                float secondColSize = 15 * ImGui::GetFontSize();

                beginTable(cpType, getLabelSize("Source Path"), "edit_script_details");

                // Class Name
                propertyHeader(script.type == ScriptType::LUA ? "Module Name" : "Class Name", secondColSize);
                if (ImGui::InputText("##new_name", nameBuffer, sizeof(nameBuffer),
                        ImGuiInputTextFlags_CallbackCharFilter,
                        ScriptCreateDialog::classNameCharFilter)) {
                    // Edit happening
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    changed = true;
                }

                float openBtnWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float clearBtnWidth = ImGui::CalcTextSize(ICON_FA_XMARK).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                float btnSpacing = ImGui::GetStyle().ItemSpacing.x;

                // Header Path
                if (script.type != ScriptType::LUA) {
                    propertyHeader("Header File", secondColSize);
                    ImGui::SetNextItemWidth(secondColSize - openBtnWidth - clearBtnWidth - btnSpacing * 2.0f);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::InputText("##new_header", headerBuffer, sizeof(headerBuffer), ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();
                    if (!hdrPath.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", hdrPath.string().c_str());
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_FOLDER_OPEN "##header_btn")) {
                        fs::path startDirectory = projectPath;
                        if (!script.headerPath.empty()) {
                            fs::path fullHdrPath(script.headerPath);
                            if (fullHdrPath.is_relative()) fullHdrPath = projectPath / fullHdrPath;
                            startDirectory = fullHdrPath.parent_path();
                        }
                        std::string selected = FileDialogs::openFileDialog(
                            startDirectory.string(), FILE_DIALOG_SCRIPT);
                        if (!selected.empty()) {
                            std::filesystem::path p(selected);
                            if (!Util::isHeaderFile(p.string())) {
                                Backend::getApp().registerAlert("Error", "Select a C++ header file.");
                            } else if (!Util::isInsidePath(p, projectPath)) {
                                Backend::getApp().registerAlert("Error", "File must be inside project directory.");
                            } else {
                                std::error_code ec;
                                std::filesystem::path rel = std::filesystem::relative(p, projectPath, ec);
                                if (!ec) {
                                    hdrPath = rel;
                                    strncpy(headerBuffer, rel.filename().string().c_str(), sizeof(headerBuffer) - 1);
                                    headerBuffer[sizeof(headerBuffer) - 1] = '\0';
                                    changed = true;
                                } else {
                                    Backend::getApp().registerAlert("Error", "File must be inside project directory.");
                                }
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::BeginDisabled(hdrPath.empty());
                    if (ImGui::Button(ICON_FA_XMARK "##header_clear")) {
                        hdrPath = "";
                        headerBuffer[0] = '\0';
                        changed = true;
                    }
                    ImGui::EndDisabled();
                }

                // Source Path
                propertyHeader("Source File", secondColSize);
                ImGui::SetNextItemWidth(secondColSize - openBtnWidth - clearBtnWidth - btnSpacing * 2.0f);

                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::InputText("##new_source", sourceBuffer, sizeof(sourceBuffer), ImGuiInputTextFlags_ReadOnly);
                ImGui::PopStyleColor();
                if (!srcPath.empty() && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", srcPath.string().c_str());
                }

                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_FOLDER_OPEN "##source_btn")) {
                    const fs::path sourceRoot = script.type == ScriptType::LUA
                        ? project->getLuaPath() : projectPath;
                    fs::path startDirectory = sourceRoot;
                    if (!script.path.empty()) {
                        fs::path fullSrcPath = script.type == ScriptType::LUA
                            ? project->resolveLuaPath(script.path) : projectPath / fs::path(script.path);
                        startDirectory = fullSrcPath.parent_path();
                    }
                    std::string selected = FileDialogs::openFileDialog(
                        startDirectory.string(), FILE_DIALOG_SCRIPT);
                    if (!selected.empty()) {
                        std::filesystem::path p(selected);
                        const bool validType = script.type == ScriptType::LUA
                            ? Util::isLuaFile(p.string())
                            : Util::isSourceFile(p.string());
                        if (!validType) {
                            Backend::getApp().registerAlert("Error", script.type == ScriptType::LUA
                                ? "Select a Lua script file."
                                : "Select a C++ source file.");
                        } else {
                            // Lua sources are stored relative to the Lua root ("lua://")
                            if (!Util::isInsidePath(p, sourceRoot)) {
                                Backend::getApp().registerAlert("Error", script.type == ScriptType::LUA
                                    ? "File must be inside the Lua directory."
                                    : "File must be inside project directory.");
                            } else {
                                std::error_code ec;
                                std::filesystem::path rel = std::filesystem::relative(p, sourceRoot, ec);
                                if (!ec) {
                                    srcPath = rel;
                                    strncpy(sourceBuffer, rel.filename().string().c_str(), sizeof(sourceBuffer) - 1);
                                    sourceBuffer[sizeof(sourceBuffer) - 1] = '\0';
                                    changed = true;
                                } else {
                                    Backend::getApp().registerAlert("Error", "File must be inside project directory.");
                                }
                            }
                        }
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(srcPath.empty());
                if (ImGui::Button(ICON_FA_XMARK "##source_clear")) {
                    srcPath = "";
                    sourceBuffer[0] = '\0';
                    changed = true;
                }
                ImGui::EndDisabled();

                endTable();

                if (changed) {
                    std::string newName = nameBuffer;
                    std::string newSource = srcPath.string();
                    std::string newHeader = hdrPath.string();

                    // Empty class name falls back to header (or source) filename stem
                    if (newName.empty()) {
                        newName = !hdrPath.empty() ? hdrPath.stem().string() : srcPath.stem().string();
                    }

                    // Normalize to a valid C++ identifier - the same rule the Create
                    // Script dialog uses - since className is emitted verbatim as a
                    // class token by the code generator. Reflect it back in the field.
                    if (!newName.empty()) {
                        newName = Factory::toIdentifier(newName);
                        strncpy(nameBuffer, newName.c_str(), sizeof(nameBuffer) - 1);
                        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
                    }

                    for (const Entity& entity : entities) {
                        ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(entity);
                        std::vector<ScriptEntry> newScripts = sc.scripts;
                        if (scriptIdx < newScripts.size()) {
                            newScripts[scriptIdx].className = newName;
                            newScripts[scriptIdx].path = newSource;
                            if (script.type != ScriptType::LUA) {
                                newScripts[scriptIdx].headerPath = newHeader;
                                if (newHeader.empty()) {
                                    newScripts[scriptIdx].properties.clear();
                                }
                            } else {
                                if (newSource.empty()) {
                                    newScripts[scriptIdx].properties.clear();
                                }
                            }

                            const ScriptEntry& updated = newScripts[scriptIdx];
                            bool hasAnyPath = !updated.path.empty() || !updated.headerPath.empty();
                            if (hasAnyPath) {
                                project->updateScriptProperties(sceneProject, entity, newScripts);
                            }

                            cmd = new PropertyCmd<std::vector<ScriptEntry>>(project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts);
                            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                        }
                    }
                    if (cmd) cmd->setNoMerge();
                }

                ImGui::EndPopup();
            }

            ImGui::SameLine();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x / 3.0, ImGui::GetStyle().FramePadding.y / 2.0));

            // Resolve header path (if present)
            std::filesystem::path projectPath = project->getProjectPath();
            std::filesystem::path hdrPath = script.headerPath;
            bool hasHeader = !script.headerPath.empty();
            if (hasHeader) {
                if (hdrPath.is_relative()) hdrPath = projectPath / hdrPath;
            }
            bool hdrExists = hasHeader && std::filesystem::exists(hdrPath);
            ImGui::BeginDisabled(!hdrExists);
            if (ImGui::Button((ICON_FA_FILE_LINES "##open_hdr_" + std::to_string(scriptIdx)).c_str())) {
                Backend::getApp().getCodeEditor()->openFile(hdrPath.string(), true);
            }
            if (ImGui::IsItemHovered()) {
                if (hdrExists){
                    ImGui::SetTooltip("Header: %s", script.headerPath.c_str());
                } else if (hasHeader) {
                    ImGui::SetTooltip("Header file not found");
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            // Resolve source path: Lua sources are stored relative to the Lua root
            std::filesystem::path srcPath = script.type == ScriptType::LUA
                ? project->resolveLuaPath(script.path) : std::filesystem::path(script.path);
            if (srcPath.is_relative()) srcPath = projectPath / srcPath;
            bool srcExists = !script.path.empty() && std::filesystem::exists(srcPath);
            ImGui::BeginDisabled(!srcExists);
            if (ImGui::Button((ICON_FA_FILE_CODE "##open_src_" + std::to_string(scriptIdx)).c_str())) {
                Backend::getApp().getCodeEditor()->openFile(srcPath.string(), true);
            }
            if (ImGui::IsItemHovered()) {
                if (srcExists){
                    ImGui::SetTooltip("Source: %s", script.path.c_str());
                } else if (!script.path.empty()) {
                    ImGui::SetTooltip("Source file not found");
                }
            }
            ImGui::EndDisabled();

            ImGui::PopStyleVar();

            endTable();

            // Display script properties if available
            if (!script.properties.empty()) {
                ImGui::SeparatorText("Properties");

                ImGui::BeginDisabled(!enabled && !mixed);

                // Compute dynamic first column width based on longest property label
                float maxPropLabelSize = 0;
                for (const ScriptProperty& prop : script.properties) {
                    std::string displayName = prop.displayName.empty() ? prop.name : prop.displayName;
                    float size = getLabelSize(displayName);
                    if (size > maxPropLabelSize)
                        maxPropLabelSize = size;
                }

                beginTable(cpType, maxPropLabelSize, "script_properties_" + std::to_string(scriptIdx));

                for (size_t propIdx = 0; propIdx < script.properties.size(); propIdx++) {
                    ScriptProperty& prop = script.properties[propIdx];

                    std::string propertyId = "scripts[" + std::to_string(scriptIdx) + "]." + prop.name;
                    std::string displayName = prop.displayName.empty() ? prop.name : prop.displayName;

                    RowPropertyType propType = scriptPropertyTypeToRowPropertyType(prop.type);

                    RowSettings propSettings;
                    propSettings.onValueChanged = [this, sceneProject, entities, scriptIdx, propName = prop.name]() {
                        for (const Entity& e : entities) {
                            ScriptComponent& sc = sceneProject->scene->getComponent<ScriptComponent>(e);
                            if (scriptIdx < sc.scripts.size()) {
                                ScriptEntry& se = sc.scripts[scriptIdx];
                                for (ScriptProperty& sp : se.properties) {
                                    if (sp.name == propName) {
                                        sp.syncToMember();
                                        break;
                                    }
                                }
                            }
                        }
                    };

                    propertyRow(propType, cpType, propertyId, displayName, sceneProject, entities, propSettings);
                }

                endTable();

                ImGui::EndDisabled();
            }

            ImGui::Indent(indentation); // Re-indent after content
        }

        ImGui::PopStyleColor(3);
        ImGui::Unindent(indentation);

        ImGui::PopID();
    }
}

void editor::Properties::drawSkyComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Default sky"), "default_sky");

    bool allDefault = true;
    for (auto& entity : entities){
        PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, "texture");
        Texture* tex = static_cast<Texture*>(prop.ref);
        if (tex->getId() != "editor:resources:default_sky") {
            allDefault = false;
            break;
        }
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Default sky");
    ImGui::TableNextColumn();

    if (allDefault) ImGui::BeginDisabled();
    if (ImGui::Button("Apply")){
        for (auto& entity : entities){
            PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, cpType, "texture");
            Texture newTex;

            ProjectUtils::setDefaultSkyTexture(newTex);

            cmd = new PropertyCmd<Texture>(project, sceneProject->id, entity, cpType, "texture", newTex, nullptr);
            CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
        }
    }
    if (allDefault) ImGui::EndDisabled();

    endTable();

    drawCustomShaderRow(cpType, ShaderType::SKYBOX, sceneProject, entities);

    ImGui::SeparatorText("Sky settings");

    beginTable(cpType, getLabelSize("Rotation"), "sky_settings");

    RowSettings visibleSettings;
    visibleSettings.help = "When disabled, the sky is not drawn but still provides image-based lighting to meshes.";
    propertyRow(RowPropertyType::Bool, cpType, "visible", "Visible", sceneProject, entities, visibleSettings);
    propertyRow(RowPropertyType::TextureCube, cpType, "texture", "Texture", sceneProject, entities);
    propertyRow(RowPropertyType::Color4L, cpType, "color", "Color", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "rotation", "Rotation", sceneProject, entities);

    endTable();
}

void editor::Properties::drawInstancedMeshComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsUInt;
    settingsUInt.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings fadeSettings;
    fadeSettings.help = "Instances shrink toward their base between Fade Start and Fade End, using horizontal distance in the mesh's local space, instead of popping out. Fade End must be greater than Fade Start.";

    beginTable(cpType, getLabelSize("Cylindrical Billboard"));
    propertyRow(RowPropertyType::UInt, cpType, "maxInstances", "Max Instances", sceneProject, entities, settingsUInt);
    propertyRow(RowPropertyType::Bool, cpType, "instancedBillboard", "Billboard", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "instancedCylindricalBillboard", "Cylindrical Billboard", sceneProject, entities);
    RowSettings cullSettings;
    cullSettings.help = "Skip instances outside the camera and shadow views. Turn off for shaders that move instances away from their bounds.";
    propertyRow(RowPropertyType::Bool, cpType, "cullInstances", "Cull Instances", sceneProject, entities, cullSettings);
    RowSettings distanceSettings = settingsUInt;
    distanceSettings.help = "Drop instances farther than this from the main camera, in world units. Zero means unlimited. A hard cut, so hide it with fog. Requires Cull Instances.";
    propertyRow(RowPropertyType::FloatPositive, cpType, "cullDistance", "Cull Distance", sceneProject, entities, distanceSettings);
    propertyRow(RowPropertyType::Bool, cpType, "distanceFade", "Distance Fade", sceneProject, entities, fadeSettings);
    if (sceneProject->scene->getComponent<InstancedMeshComponent>(entities[0]).distanceFade) {
        propertyRow(RowPropertyType::FloatPositive, cpType, "fadeStart", "Fade Start", sceneProject, entities, settingsUInt);
        propertyRow(RowPropertyType::FloatPositive, cpType, "fadeEnd", "Fade End", sceneProject, entities, settingsUInt);
    }
    endTable();

    if (entities.size() != 1) {
        ImGui::SeparatorText("Instances");
        ImGui::TextDisabled("Select a single entity to edit instances");
        return;
    }

    Entity entity = entities[0];
    InstancedMeshComponent& instmesh = sceneProject->scene->getComponent<InstancedMeshComponent>(entity);

    if (ProjectUtils::hasModelMeshChildrenWithoutRootGeometry(sceneProject->scene, entity)){
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::WarningText);
        ImGui::TextWrapped(
            ICON_FA_TRIANGLE_EXCLAMATION
            " Instances will not render: this model stores its geometry in child mesh entities. "
            "Right-click the model in Structure and choose 'Merge static model'.");
        ImGui::PopStyleColor();
    }

    ImGui::SeparatorText("Instances");

    beginTable(cpType, getLabelSize("Instances"), "instanced_mesh_instances_header");
    propertyHeader("Instances", -1, false, false);
    ImGui::Text("%zu", instmesh.instances.size());
    float instancesArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Instance##instanced_mesh_add", instancesArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (InstancedMeshComponent* instComp = sceneProject->scene->findComponent<InstancedMeshComponent>(selectedEntity)) {
                std::vector<InstanceData> newInstances = instComp->instances;
                InstanceData newInst;
                if (!newInstances.empty()) {
                    newInst = newInstances.back();
                }
                newInstances.push_back(newInst);
                multiCmd->addPropertyCmd<std::vector<InstanceData>>(project, sceneProject->id, selectedEntity, cpType, "instances", newInstances);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_instances", instancesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        instancesExpanded = !instancesExpanded;
    }
    endTable();

    if (!instancesExpanded || instmesh.instances.empty()) {
        return;
    }

    bool removedInstance = false;

    for (size_t i = 0; i < instmesh.instances.size(); i++) {
        ImGui::PushID((int)i);

        std::string instanceGroupStr = "instance_" + std::to_string(i);
        std::string instanceLabel = "[" + std::to_string(i) + "] Instance " + std::to_string(i);

        bool isInstanceSelected = sceneProject->sceneRender &&
            sceneProject->sceneRender->getSelectedInstanceIndex() == (int)i &&
            sceneProject->sceneRender->getSelectedInstanceEntity() == entities[0];

        // sub-selected in the scene viewport: same marking as occluder points
        if (isInstanceSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubSelectionText);
            ImGui::SeparatorText((std::string(ICON_FA_CARET_RIGHT) + " " + instanceLabel).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::SeparatorText(instanceLabel.c_str());
        }
        if (isLastHeaderRowClicked()) {
            syncSubSelection(sceneProject, entities[0], -1, (int)i);
        }

        beginTable(cpType, getLabelSize("Position"), instanceGroupStr);
        propertyHeader("Instance", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_instance", instancesButtonGroups[instanceGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            instancesButtonGroups[instanceGroupStr] = !instancesButtonGroups[instanceGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_instance", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (InstancedMeshComponent* instComp = sceneProject->scene->findComponent<InstancedMeshComponent>(selectedEntity)) {
                    if (i < instComp->instances.size()) {
                        std::vector<InstanceData> newInstances = instComp->instances;
                        newInstances.erase(newInstances.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<InstanceData>>(project, sceneProject->id, selectedEntity, cpType, "instances", newInstances);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedInstance = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (instancesButtonGroups[instanceGroupStr]) {
            std::string propPrefix = "instances[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".position", "Position", sceneProject, entities);
            propertyRow(RowPropertyType::Quat, cpType, propPrefix + ".rotation", "Rotation", sceneProject, entities);
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".scale", "Scale", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".color", "Color", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, propPrefix + ".visible", "Visible", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedInstance) {
        return;
    }
}

void editor::Properties::drawParticlesComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsFloat;
    settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

    RowSettings settingsInt;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Max Per Update"));
    propertyRow(RowPropertyType::UInt, cpType, "maxParticles", "Max Particles", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Bool, cpType, "emitter", "Emitter", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "localSpace", "Local Space", sceneProject, entities);
    propertyRow(RowPropertyType::Int, cpType, "rate", "Rate", sceneProject, entities, settingsInt);
    propertyRow(RowPropertyType::Int, cpType, "maxPerUpdate", "Max Per Update", sceneProject, entities, settingsInt);
    endTable();

    // Bursts (single-entity editor; vector edits are not multi-entity safe)
    if (entities.size() == 1) {
        Scene* burstScene = sceneProject->scene;
        ParticlesComponent* bp = burstScene ? burstScene->findComponent<ParticlesComponent>(entities[0]) : nullptr;
        if (bp) {
            Entity entity = entities[0];
            PropertyData burstProp = Catalog::getProperty(sceneProject->scene, entity, cpType, "bursts");
            std::vector<ParticleBurst>* bursts = static_cast<std::vector<ParticleBurst>*>(burstProp.ref);
            std::vector<ParticleBurst>* defBursts = static_cast<std::vector<ParticleBurst>*>(burstProp.def);

            ImGui::SeparatorText("Bursts");
            if (bursts) {
                bool defChanged = defBursts && *bursts != *defBursts;
                beginTable(cpType, getLabelSize("Bursts"), "particle_burst_header");
                if (propertyHeader("Bursts", -1, defChanged, false) && defBursts) {
                    setParticleBursts(cpType, sceneProject, entity, *defBursts);
                    finishProperty = true;
                }
                ImGui::Text("%zu", bursts->size());
                std::string addBurstLabel = ICON_FA_PLUS " Add Burst##particle_burst_add";
                ImGui::SameLine();
                float addBurstButtonWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
                if (ImGui::Button(addBurstLabel.c_str(), ImVec2(addBurstButtonWidth, 0))) {
                    std::vector<ParticleBurst> newBursts = *bursts;
                    newBursts.push_back(ParticleBurst());
                    setParticleBursts(cpType, sceneProject, entity, newBursts);
                }
                endTable();
            }
            if (bursts && !bursts->empty()) {
                beginTable(cpType, getLabelSize("Max Count"), "particle_burst_table");
                for (size_t i = 0; i < bursts->size(); i++) {
                    ImGui::PushID((int)(1000 + i));
                    ParticleBurst burst = (*bursts)[i];

                    propertyHeader("Time " + std::to_string(i), -1, false, false);
                    ImGui::SetNextItemWidth(settingsFloat.secondColSize);
                    if (ImGui::DragFloat("##particle_burst_time", &burst.time, settingsFloat.stepSize, 0.0f, 0.0f, settingsFloat.format)) {
                        std::vector<ParticleBurst> newBursts = *bursts;
                        newBursts[i] = burst;
                        setParticleBursts(cpType, sceneProject, entity, newBursts);
                    }
                    float deleteButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                    ImGui::SameLine();
                    float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - deleteButtonWidth);
                    ImGui::SetCursorPosX(targetX);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    bool removed = ImGui::Button(ICON_FA_TRASH_CAN "##particle_burst_delete");
                    ImGui::PopStyleColor(2);

                    propertyHeader("Min Count", -1, false, false);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputInt("##particle_burst_min", &burst.minCount)) {
                        std::vector<ParticleBurst> newBursts = *bursts;
                        newBursts[i] = burst;
                        setParticleBursts(cpType, sceneProject, entity, newBursts);
                    }

                    propertyHeader("Max Count", -1, false, false);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputInt("##particle_burst_max", &burst.maxCount)) {
                        std::vector<ParticleBurst> newBursts = *bursts;
                        newBursts[i] = burst;
                        setParticleBursts(cpType, sceneProject, entity, newBursts);
                    }

                    ImGui::PopID();

                    if (removed) {
                        std::vector<ParticleBurst> newBursts = *bursts;
                        newBursts.erase(newBursts.begin() + (std::ptrdiff_t)i);
                        setParticleBursts(cpType, sceneProject, entity, newBursts);
                        break;
                    }
                }
                endTable();
            }
        }
    } else {
        ImGui::SeparatorText("Bursts");
        ImGui::TextDisabled("Select a single entity to edit bursts");
    }

    const float subsectionIndent = 10.0f;

    ImGui::Indent(subsectionIndent);
    ImGui::PushStyleColor(ImGuiCol_Header, Theme::Colors::NestedHeader);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::Colors::NestedHeaderHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::Colors::NestedHeaderActive);
    bool initializersOpen = ImGui::CollapsingHeader("Initializers");
    ImGui::PopStyleColor(3);
    if (initializersOpen) {
        ImGui::Unindent(subsectionIndent);

        ImGui::SeparatorText("Life");
        beginTable(cpType, getLabelSize("Max Life"), "life_table");
        propertyRow(RowPropertyType::Float, cpType, "lifeInitializer.minLife", "Min Life", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "lifeInitializer.maxLife", "Max Life", sceneProject, entities, settingsFloat);
        endTable();

        ImGui::SeparatorText("Position");
        RowSettings settingsShape;
        settingsShape.enumEntries = &entriesEmitterShape;
        beginTable(cpType, getLabelSize("Cone Height"), "position_initializer_table");
        propertyRow(RowPropertyType::Enum, cpType, "positionInitializer.shape", "Shape", sceneProject, entities, settingsShape);

        ParticleEmitterShape shownShape = ParticleEmitterShape::Box;
        if (!entities.empty()) {
            ParticlesComponent* sp = sceneProject->scene->findComponent<ParticlesComponent>(entities[0]);
            if (sp) shownShape = sp->positionInitializer.shape;
        }

        if (shownShape == ParticleEmitterShape::Box) {
            propertyRow(RowPropertyType::Vector3, cpType, "positionInitializer.minPosition", "Min Position", sceneProject, entities);
            propertyRow(RowPropertyType::Vector3, cpType, "positionInitializer.maxPosition", "Max Position", sceneProject, entities);
        } else if (shownShape == ParticleEmitterShape::Sphere ||
                   shownShape == ParticleEmitterShape::Hemisphere ||
                   shownShape == ParticleEmitterShape::Circle) {
            propertyRow(RowPropertyType::Float, cpType, "positionInitializer.radius", "Radius", sceneProject, entities, settingsFloat);
            propertyRow(RowPropertyType::Float, cpType, "positionInitializer.innerRadius", "Inner Radius", sceneProject, entities, settingsFloat);
        } else if (shownShape == ParticleEmitterShape::Cone) {
            propertyRow(RowPropertyType::Float, cpType, "positionInitializer.coneAngle", "Cone Angle", sceneProject, entities, settingsFloat);
            propertyRow(RowPropertyType::Float, cpType, "positionInitializer.coneHeight", "Cone Height", sceneProject, entities, settingsFloat);
        }
        endTable();

        ImGui::SeparatorText("Velocity");
        beginTable(cpType, getLabelSize("Max Velocity"), "velocity_initializer_table");
        propertyRow(RowPropertyType::Vector3, cpType, "velocityInitializer.minVelocity", "Min Velocity", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "velocityInitializer.maxVelocity", "Max Velocity", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Acceleration");
        beginTable(cpType, getLabelSize("Max Acceleration"), "acceleration_initializer_table");
        propertyRow(RowPropertyType::Vector3, cpType, "accelerationInitializer.minAcceleration", "Min Acceleration", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "accelerationInitializer.maxAcceleration", "Max Acceleration", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Color");
        beginTable(cpType, getLabelSize("Use sRGB"), "color_initializer_table");
        propertyRow(RowPropertyType::Color3L, cpType, "colorInitializer.minColor", "Min Color", sceneProject, entities);
        propertyRow(RowPropertyType::Color3L, cpType, "colorInitializer.maxColor", "Max Color", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "colorInitializer.useSRGB", "Use sRGB", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Alpha");
        beginTable(cpType, getLabelSize("Max Alpha"), "alpha_initializer_table");
        propertyRow(RowPropertyType::Float, cpType, "alphaInitializer.minAlpha", "Min Alpha", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "alphaInitializer.maxAlpha", "Max Alpha", sceneProject, entities, settingsFloat);
        endTable();

        ImGui::SeparatorText("Size");
        beginTable(cpType, getLabelSize("Max Size"), "size_initializer_table");
        propertyRow(RowPropertyType::Float, cpType, "sizeInitializer.minSize", "Min Size", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "sizeInitializer.maxSize", "Max Size", sceneProject, entities, settingsFloat);
        endTable();

        ImGui::SeparatorText("Sprite");
        drawParticleFrameList(cpType, "spriteInitializer.frames", "sprite_initializer_frames", sceneProject, entities);

        ImGui::SeparatorText("Rotation");
        beginTable(cpType, getLabelSize("Shortest Path"), "rotation_initializer_table");
        propertyRow(RowPropertyType::Quat, cpType, "rotationInitializer.minRotation", "Min Rotation", sceneProject, entities);
        propertyRow(RowPropertyType::Quat, cpType, "rotationInitializer.maxRotation", "Max Rotation", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "rotationInitializer.shortestPath", "Shortest Path", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Scale");
        beginTable(cpType, getLabelSize("Max Scale"), "scale_initializer_table");
        propertyRow(RowPropertyType::Vector3, cpType, "scaleInitializer.minScale", "Min Scale", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "scaleInitializer.maxScale", "Max Scale", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "scaleInitializer.linearSort", "Linear Sort", sceneProject, entities);
        endTable();

        ImGui::Indent(subsectionIndent);
    }
    ImGui::Unindent(subsectionIndent);

    ImGui::Indent(subsectionIndent);
    ImGui::PushStyleColor(ImGuiCol_Header, Theme::Colors::NestedHeader);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::Colors::NestedHeaderHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::Colors::NestedHeaderActive);
    bool modifiersOpen = ImGui::CollapsingHeader("Modifiers");
    ImGui::PopStyleColor(3);
    if (modifiersOpen) {
        ImGui::Unindent(subsectionIndent);

        ImGui::SeparatorText("Position");
        beginTable(cpType, getLabelSize("From Position"), "position_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "positionModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "positionModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "positionModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "positionModifier.fromPosition", "From Position", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "positionModifier.toPosition", "To Position", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Velocity");
        beginTable(cpType, getLabelSize("From Velocity"), "velocity_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "velocityModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "velocityModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "velocityModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "velocityModifier.fromVelocity", "From Velocity", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "velocityModifier.toVelocity", "To Velocity", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Acceleration");
        beginTable(cpType, getLabelSize("From Acceleration"), "acceleration_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "accelerationModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "accelerationModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "accelerationModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "accelerationModifier.fromAcceleration", "From Acceleration", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "accelerationModifier.toAcceleration", "To Acceleration", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Color");
        beginTable(cpType, getLabelSize("Mod sRGB"), "color_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "colorModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "colorModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "colorModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Color3L, cpType, "colorModifier.fromColor", "From Color", sceneProject, entities);
        propertyRow(RowPropertyType::Color3L, cpType, "colorModifier.toColor", "To Color", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "colorModifier.useSRGB", "Mod sRGB", sceneProject, entities);
        endTable();

        if (entities.size() == 1) {
            Scene* gradScene = sceneProject->scene;
            ParticlesComponent* gp = gradScene ? gradScene->findComponent<ParticlesComponent>(entities[0]) : nullptr;
            if (gp) {
                Entity entity = entities[0];
                PropertyData gradProp = Catalog::getProperty(sceneProject->scene, entity, cpType, "colorGradient");
                ParticleColorGradient* gradient = static_cast<ParticleColorGradient*>(gradProp.ref);
                ParticleColorGradient* defGradient = static_cast<ParticleColorGradient*>(gradProp.def);

                ImGui::SeparatorText("Color Gradient");
                if (gradient) {
                    bool defChanged = defGradient && !(*gradient == *defGradient);
                    beginTable(cpType, getLabelSize("Stops"), "particle_gradient_header");
                    if (propertyHeader("Stops", -1, defChanged, false) && defGradient) {
                        setParticleColorGradient(cpType, sceneProject, entity, *defGradient);
                        finishProperty = true;
                    }
                    ImGui::Text("%zu", gradient->stops.size());
                    std::string addStopLabel = ICON_FA_PLUS " Add Stop##particle_grad_add";
                    ImGui::SameLine();
                    float addStopButtonWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x);
                    if (ImGui::Button(addStopLabel.c_str(), ImVec2(addStopButtonWidth, 0))) {
                        ParticleColorGradient newGrad = *gradient;
                        ParticleColorGradientStop stop;
                        if (!newGrad.stops.empty()) {
                            float last = newGrad.stops.back().time;
                            stop.time = std::min(1.0f, last + 0.25f);
                            stop.color = newGrad.stops.back().color;
                        }
                        newGrad.stops.push_back(stop);
                        setParticleColorGradient(cpType, sceneProject, entity, newGrad);
                    }
                    endTable();
                }
                if (gradient && !gradient->stops.empty()) {
                    beginTable(cpType, getLabelSize("Color N"), "particle_gradient_table");
                    for (size_t i = 0; i < gradient->stops.size(); i++) {
                        ImGui::PushID((int)(2000 + i));
                        ParticleColorGradientStop stop = gradient->stops[i];

                        propertyHeader("Time " + std::to_string(i), -1, false, false);
                        float deleteButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                        ImGui::SetNextItemWidth(-deleteButtonWidth - ImGui::GetStyle().ItemSpacing.x);
                        if (ImGui::SliderFloat("##particle_grad_time", &stop.time, 0.0f, 1.0f)) {
                            ParticleColorGradient newGrad = *gradient;
                            newGrad.stops[i] = stop;
                            setParticleColorGradient(cpType, sceneProject, entity, newGrad);
                        }
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                        bool removed = ImGui::Button(ICON_FA_TRASH_CAN "##particle_grad_delete");
                        ImGui::PopStyleColor(2);

                        propertyHeader("Color " + std::to_string(i), -1, false, false);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        float col[3] = { stop.color.x, stop.color.y, stop.color.z };
                        if (ImGui::ColorEdit3("##particle_grad_color", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                            stop.color = Vector3(col[0], col[1], col[2]);
                            ParticleColorGradient newGrad = *gradient;
                            newGrad.stops[i] = stop;
                            setParticleColorGradient(cpType, sceneProject, entity, newGrad);
                        }

                        ImGui::PopID();

                        if (removed) {
                            ParticleColorGradient newGrad = *gradient;
                            newGrad.stops.erase(newGrad.stops.begin() + (std::ptrdiff_t)i);
                            setParticleColorGradient(cpType, sceneProject, entity, newGrad);
                            break;
                        }
                    }
                    endTable();
                }

                beginTable(cpType, getLabelSize("Grad sRGB"), "particle_gradient_options_table");
                propertyHeader("Grad sRGB", -1, false, false);
                bool useSRGB = gradient ? gradient->useSRGB : gp->colorGradient.useSRGB;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::Checkbox("##particle_grad_srgb", &useSRGB)) {
                    ParticleColorGradient newGrad = gradient ? *gradient : gp->colorGradient;
                    newGrad.useSRGB = useSRGB;
                    setParticleColorGradient(cpType, sceneProject, entity, newGrad);
                }
                endTable();
            }
        } else {
            ImGui::SeparatorText("Color Gradient");
            ImGui::TextDisabled("Select a single entity to edit color gradient");
        }

        ImGui::SeparatorText("Alpha");
        beginTable(cpType, getLabelSize("From Alpha"), "alpha_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "alphaModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "alphaModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "alphaModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Float, cpType, "alphaModifier.fromAlpha", "From Alpha", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "alphaModifier.toAlpha", "To Alpha", sceneProject, entities, settingsFloat);
        endTable();

        ImGui::SeparatorText("Size");
        beginTable(cpType, getLabelSize("From Size"), "size_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "sizeModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "sizeModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "sizeModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Float, cpType, "sizeModifier.fromSize", "From Size", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "sizeModifier.toSize", "To Size", sceneProject, entities, settingsFloat);
        endTable();

        ImGui::SeparatorText("Sprite");
        beginTable(cpType, getLabelSize("From Time"), "sprite_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "spriteModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "spriteModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "spriteModifier.function", "Ease", sceneProject, entities);
        endTable();
        drawParticleFrameList(cpType, "spriteModifier.frames", "sprite_modifier_frames", sceneProject, entities);

        ImGui::SeparatorText("Rotation");
        beginTable(cpType, getLabelSize("Mod Shortest"), "rotation_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "rotationModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "rotationModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "rotationModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Quat, cpType, "rotationModifier.fromRotation", "From Rotation", sceneProject, entities);
        propertyRow(RowPropertyType::Quat, cpType, "rotationModifier.toRotation", "To Rotation", sceneProject, entities);
        propertyRow(RowPropertyType::Bool, cpType, "rotationModifier.shortestPath", "Mod Shortest", sceneProject, entities);
        endTable();

        ImGui::SeparatorText("Scale");
        beginTable(cpType, getLabelSize("From Scale"), "scale_modifier_table");
        propertyRow(RowPropertyType::Float, cpType, "scaleModifier.fromTime", "From Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, "scaleModifier.toTime", "To Time", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Ease, cpType, "scaleModifier.function", "Ease", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "scaleModifier.fromScale", "From Scale", sceneProject, entities);
        propertyRow(RowPropertyType::Vector3, cpType, "scaleModifier.toScale", "To Scale", sceneProject, entities);
        endTable();

        ImGui::Indent(subsectionIndent);
    }
    ImGui::Unindent(subsectionIndent);
}

void editor::Properties::drawPointsComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Transparent"));
    propertyRow(RowPropertyType::Texture, cpType, "texture", "Texture", sceneProject, entities);

    RowSettings transparencySettings;
    transparencySettings.help = "Just for render ordering";
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "transparent", "Transparent", "autoTransparency", "Auto Transparency", sceneProject, entities, transparencySettings);

    propertyRow(RowPropertyType::UInt, cpType, "maxPoints", "Max Points", sceneProject, entities, settingsInt);
    endTable();

    drawCustomShaderRow(cpType, ShaderType::POINTS, sceneProject, entities);

    if (entities.size() != 1) {
        ImGui::SeparatorText("Points");
        ImGui::TextDisabled("Select a single entity to edit points");
        return;
    }

    Entity entity = entities[0];
    PointsComponent& pts = sceneProject->scene->getComponent<PointsComponent>(entity);

    ImGui::SeparatorText("Points");

    beginTable(cpType, getLabelSize("Points"), "points_header");
    propertyHeader("Points", -1, false, false);
    ImGui::Text("%zu", pts.points.size());
    float pointsArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Point##points_add", pointsArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (PointsComponent* ptsComp = sceneProject->scene->findComponent<PointsComponent>(selectedEntity)) {
                std::vector<PointData> newPoints = ptsComp->points;
                PointData newPt;
                if (!newPoints.empty()) {
                    newPt = newPoints.back();
                }
                newPoints.push_back(newPt);
                multiCmd->addPropertyCmd<std::vector<PointData>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_points", pointsExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        pointsExpanded = !pointsExpanded;
    }
    endTable();

    if (!pointsExpanded || pts.points.empty()) {
        return;
    }

    bool removedPoint = false;
    const size_t maxDrawnPoints = 256;
    const size_t drawnPoints = std::min(pts.points.size(), maxDrawnPoints);

    if (drawnPoints < pts.points.size()) {
        ImGui::TextDisabled("Showing first %zu of %zu points", drawnPoints, pts.points.size());
    }

    for (size_t i = 0; i < drawnPoints; i++) {
        ImGui::PushID((int)i);

        std::string pointGroupStr = "point_" + std::to_string(i);
        std::string pointLabel = "[" + std::to_string(i) + "] Point " + std::to_string(i);

        ImGui::SeparatorText(pointLabel.c_str());

        beginTable(cpType, getLabelSize("Position"), pointGroupStr);
        propertyHeader("Point", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_point", pointsButtonGroups[pointGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            pointsButtonGroups[pointGroupStr] = !pointsButtonGroups[pointGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_point", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (PointsComponent* ptsComp = sceneProject->scene->findComponent<PointsComponent>(selectedEntity)) {
                    if (i < ptsComp->points.size()) {
                        std::vector<PointData> newPoints = ptsComp->points;
                        newPoints.erase(newPoints.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<PointData>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedPoint = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (pointsButtonGroups[pointGroupStr]) {
            std::string propPrefix = "points[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".position", "Position", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".color", "Color", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, propPrefix + ".size", "Size", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, propPrefix + ".rotation", "Rotation", sceneProject, entities);
            propertyRow(RowPropertyType::Vector4, cpType, propPrefix + ".textureRect", "Texture Rect", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, propPrefix + ".visible", "Visible", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedPoint) {
        return;
    }
}

void editor::Properties::drawLinesComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsInt;
    settingsInt.secondColSize = 6 * ImGui::GetFontSize();

    beginTable(cpType, getLabelSize("Max Lines"));
    propertyRow(RowPropertyType::UInt, cpType, "maxLines", "Max Lines", sceneProject, entities, settingsInt);
    endTable();

    drawCustomShaderRow(cpType, ShaderType::LINES, sceneProject, entities);

    if (entities.size() != 1) {
        ImGui::SeparatorText("Lines");
        ImGui::TextDisabled("Select a single entity to edit lines");
        return;
    }

    Entity entity = entities[0];
    LinesComponent& lines = sceneProject->scene->getComponent<LinesComponent>(entity);

    ImGui::SeparatorText("Lines");

    beginTable(cpType, getLabelSize("Lines"), "lines_header");
    propertyHeader("Lines", -1, false, false);
    ImGui::Text("%zu", lines.lines.size());
    float linesArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Line##lines_add", linesArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (LinesComponent* linesComp = sceneProject->scene->findComponent<LinesComponent>(selectedEntity)) {
                std::vector<LineData> newLines = linesComp->lines;
                LineData newLine;
                if (!newLines.empty()) {
                    newLine = newLines.back();
                } else {
                    newLine.pointB = Vector3(1.0f, 0.0f, 0.0f);
                }
                newLines.push_back(newLine);
                multiCmd->addPropertyCmd<std::vector<LineData>>(project, sceneProject->id, selectedEntity, cpType, "lines", newLines);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_lines", linesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        linesExpanded = !linesExpanded;
    }
    endTable();

    if (!linesExpanded || lines.lines.empty()) {
        return;
    }

    bool removedLine = false;

    // endpoint sub-selected in the scene viewport (index = lineIndex * 2 + endpoint)
    int selectedLinePoint = -1;
    if (sceneProject->sceneRender && sceneProject->sceneRender->getSelectedLinePointEntity() == entity){
        selectedLinePoint = sceneProject->sceneRender->getSelectedLinePointIndex();
    }

    for (size_t i = 0; i < lines.lines.size(); i++) {
        ImGui::PushID((int)i);

        std::string lineGroupStr = "line_" + std::to_string(i);
        std::string lineLabel = "[" + std::to_string(i) + "] Line " + std::to_string(i);

        bool isLineSelected = (selectedLinePoint >= 0 && selectedLinePoint / 2 == (int)i);

        // sub-selected in the scene viewport: same marking as occluder points
        if (isLineSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubSelectionText);
            ImGui::SeparatorText((std::string(ICON_FA_CARET_RIGHT) + " " + lineLabel).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::SeparatorText(lineLabel.c_str());
        }

        beginTable(cpType, getLabelSize("Point A"), lineGroupStr);
        propertyHeader("Line", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_line", linesButtonGroups[lineGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            linesButtonGroups[lineGroupStr] = !linesButtonGroups[lineGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_line", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (LinesComponent* linesComp = sceneProject->scene->findComponent<LinesComponent>(selectedEntity)) {
                    if (i < linesComp->lines.size()) {
                        std::vector<LineData> newLines = linesComp->lines;
                        newLines.erase(newLines.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<LineData>>(project, sceneProject->id, selectedEntity, cpType, "lines", newLines);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedLine = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (linesButtonGroups[lineGroupStr]) {
            std::string propPrefix = "lines[" + std::to_string(i) + "]";
            bool pointASelected = isLineSelected && (selectedLinePoint % 2 == 0);
            bool pointBSelected = isLineSelected && (selectedLinePoint % 2 == 1);
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".pointA", pointASelected ? (std::string(ICON_FA_CARET_RIGHT) + " Point A") : "Point A", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".colorA", "Color A", sceneProject, entities);
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".pointB", pointBSelected ? (std::string(ICON_FA_CARET_RIGHT) + " Point B") : "Point B", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".colorB", "Color B", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedLine) {
        return;
    }
}

void editor::Properties::drawPolygonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    if (entities.size() != 1) {
        ImGui::SeparatorText("Vertices");
        ImGui::TextDisabled("Select a single entity to edit polygon vertices");
        return;
    }

    Entity entity = entities[0];
    PolygonComponent& polygon = sceneProject->scene->getComponent<PolygonComponent>(entity);

    ImGui::SeparatorText("Vertices");

    beginTable(cpType, getLabelSize("Vertices"), "polygon_points_header");
    propertyHeader("Vertices", -1, false, false);
    ImGui::Text("%zu", polygon.points.size());
    float polygonArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Vertex##polygon_add", polygonArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (PolygonComponent* polygonComp = sceneProject->scene->findComponent<PolygonComponent>(selectedEntity)) {
                std::vector<PolygonPoint> newPoints = polygonComp->points;
                PolygonPoint newPoint;
                if (!newPoints.empty()) {
                    newPoint = newPoints.back();
                }
                newPoints.push_back(newPoint);
                multiCmd->addPropertyCmd<std::vector<PolygonPoint>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_polygon", polygonExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        polygonExpanded = !polygonExpanded;
    }
    endTable();

    if (!polygonExpanded || polygon.points.empty()) {
        return;
    }

    // vertex sub-selected in the scene viewport (Polygon = non-mesh)
    int selectedVertex = -1;
    if (sceneProject->sceneRender && sceneProject->sceneRender->getSelectedPolygonPointEntity() == entity
        && !sceneProject->sceneRender->isSelectedPolygonPointMesh()){
        selectedVertex = sceneProject->sceneRender->getSelectedPolygonPointIndex();
    }

    bool removedPoint = false;

    for (size_t i = 0; i < polygon.points.size(); i++) {
        ImGui::PushID((int)i);

        std::string pointGroupStr = "polygon_point_" + std::to_string(i);
        std::string pointLabel = "[" + std::to_string(i) + "] Vertex " + std::to_string(i);

        bool vertexSelected = ((int)i == selectedVertex);

        // sub-selected in the scene viewport: same marking as occluder/line points
        if (vertexSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubSelectionText);
            ImGui::SeparatorText((std::string(ICON_FA_CARET_RIGHT) + " " + pointLabel).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::SeparatorText(pointLabel.c_str());
        }

        beginTable(cpType, getLabelSize("Position"), pointGroupStr);
        propertyHeader("Vertex", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_polygon_point", polygonButtonGroups[pointGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            polygonButtonGroups[pointGroupStr] = !polygonButtonGroups[pointGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_polygon_point", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (PolygonComponent* polygonComp = sceneProject->scene->findComponent<PolygonComponent>(selectedEntity)) {
                    if (i < polygonComp->points.size()) {
                        std::vector<PolygonPoint> newPoints = polygonComp->points;
                        newPoints.erase(newPoints.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<PolygonPoint>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedPoint = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (polygonButtonGroups[pointGroupStr]) {
            std::string propPrefix = "points[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".position", vertexSelected ? (std::string(ICON_FA_CARET_RIGHT) + " Position") : "Position", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".color", "Color", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedPoint) {
        return;
    }
}

void editor::Properties::drawMeshPolygonComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Flip Y"));
    propertyRowWithAutoButton(RowPropertyType::Bool, cpType, "flipY", "Flip Y", "automaticFlipY", "Automatic Flip Y", sceneProject, entities);
    endTable();

    if (entities.size() == 1) {
        MeshPolygonComponent& polygon = sceneProject->scene->getComponent<MeshPolygonComponent>(entities[0]);
        ImGui::SeparatorText("Size");
        beginTable(cpType, getLabelSize("Height"));
        propertyHeader("Width", -1, false, false);
        ImGui::Text("%u", polygon.width);
        propertyHeader("Height", -1, false, false);
        ImGui::Text("%u", polygon.height);
        endTable();
    }

    if (entities.size() != 1) {
        ImGui::SeparatorText("Vertices");
        ImGui::TextDisabled("Select a single entity to edit mesh polygon vertices");
        return;
    }

    Entity entity = entities[0];
    MeshPolygonComponent& polygon = sceneProject->scene->getComponent<MeshPolygonComponent>(entity);

    ImGui::SeparatorText("Vertices");

    beginTable(cpType, getLabelSize("Vertices"), "mesh_polygon_points_header");
    propertyHeader("Vertices", -1, false, false);
    ImGui::Text("%zu", polygon.points.size());
    float polygonArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Vertex##mesh_polygon_add", polygonArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (MeshPolygonComponent* polygonComp = sceneProject->scene->findComponent<MeshPolygonComponent>(selectedEntity)) {
                std::vector<PolygonPoint> newPoints = polygonComp->points;
                PolygonPoint newPoint;
                if (!newPoints.empty()) {
                    newPoint = newPoints.back();
                }
                newPoints.push_back(newPoint);
                multiCmd->addPropertyCmd<std::vector<PolygonPoint>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_mesh_polygon", meshPolygonExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        meshPolygonExpanded = !meshPolygonExpanded;
    }
    endTable();

    if (!meshPolygonExpanded || polygon.points.empty()) {
        return;
    }

    // vertex sub-selected in the scene viewport (MeshPolygon = mesh)
    int selectedVertex = -1;
    if (sceneProject->sceneRender && sceneProject->sceneRender->getSelectedPolygonPointEntity() == entity
        && sceneProject->sceneRender->isSelectedPolygonPointMesh()){
        selectedVertex = sceneProject->sceneRender->getSelectedPolygonPointIndex();
    }

    bool removedPoint = false;

    for (size_t i = 0; i < polygon.points.size(); i++) {
        ImGui::PushID((int)i);

        std::string pointGroupStr = "mesh_polygon_point_" + std::to_string(i);
        std::string pointLabel = "[" + std::to_string(i) + "] Vertex " + std::to_string(i);

        bool vertexSelected = ((int)i == selectedVertex);

        // sub-selected in the scene viewport: same marking as occluder/line points
        if (vertexSelected) {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubSelectionText);
            ImGui::SeparatorText((std::string(ICON_FA_CARET_RIGHT) + " " + pointLabel).c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::SeparatorText(pointLabel.c_str());
        }

        beginTable(cpType, getLabelSize("Position"), pointGroupStr);
        propertyHeader("Vertex", -1, false, false);

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        float arrowButtonWidth = ImGui::GetFrameHeight();
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonWidth;
        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (ImGui::ArrowButton("##toggle_mesh_polygon_point", meshPolygonButtonGroups[pointGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            meshPolygonButtonGroups[pointGroupStr] = !meshPolygonButtonGroups[pointGroupStr];
        }
        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_mesh_polygon_point", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (MeshPolygonComponent* polygonComp = sceneProject->scene->findComponent<MeshPolygonComponent>(selectedEntity)) {
                    if (i < polygonComp->points.size()) {
                        std::vector<PolygonPoint> newPoints = polygonComp->points;
                        newPoints.erase(newPoints.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<PolygonPoint>>(project, sceneProject->id, selectedEntity, cpType, "points", newPoints);
                    }
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            removedPoint = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            endTable();
            ImGui::PopID();
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (meshPolygonButtonGroups[pointGroupStr]) {
            std::string propPrefix = "points[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::Vector3, cpType, propPrefix + ".position", vertexSelected ? (std::string(ICON_FA_CARET_RIGHT) + " Position") : "Position", sceneProject, entities);
            propertyRow(RowPropertyType::Color4L, cpType, propPrefix + ".color", "Color", sceneProject, entities);
        }

        endTable();
        ImGui::PopID();
    }

    if (removedPoint) {
        return;
    }
}

void editor::Properties::drawBody2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Body2DComponent& body = sceneProject->scene->getComponent<Body2DComponent>(entities[0]);

    auto getSizeForShape = [sceneProject](Entity entity, float defaultSize = 100.0f) -> Vector2 {
        float width = defaultSize;
        float height = defaultSize;

        if (SpriteComponent* spriteComp = sceneProject->scene->findComponent<SpriteComponent>(entity)){
            if (spriteComp->width > 0){
                width = (float)spriteComp->width;
            }
            if (spriteComp->height > 0){
                height = (float)spriteComp->height;
            }
        }else if (UILayoutComponent* layoutComp = sceneProject->scene->findComponent<UILayoutComponent>(entity)){
            if (layoutComp->width > 0){
                width = (float)layoutComp->width;
            }
            if (layoutComp->height > 0){
                height = (float)layoutComp->height;
            }
        }

        return Vector2(width, height);
    };

    RowSettings settingsBodyType;
    settingsBodyType.enumEntries = &entriesBodyType;

    beginTable(cpType, getLabelSize("Update Shapes"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Body Type", sceneProject, entities, settingsBodyType);
    endTable();

    ImGui::SeparatorText("Shapes");
    ImGui::Text("Shapes: %zu", body.numShapes);

    static int createShape2DType = (int)Shape2DType::POLYGON;
    const char* createShape2DLabel = "Polygon";
    for (const EnumEntry& entry : entriesShape2DType){
        if (entry.value == createShape2DType){
            createShape2DLabel = entry.name;
            break;
        }
    }

    ImGui::SetNextItemWidth(9 * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##shape2d_create_type", createShape2DLabel)){
        for (const EnumEntry& entry : entriesShape2DType){
            bool selected = (createShape2DType == entry.value);
            if (ImGui::Selectable(entry.name, selected)){
                createShape2DType = entry.value;
            }
            if (selected){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Shape##body2d")){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

        for (Entity entity : entities){
            if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){

                Shape2D shape;
                shape.type = (Shape2DType)createShape2DType;

                switch (shape.type){
                case Shape2DType::POLYGON:
                {
                    Vector2 layoutSize = getSizeForShape(entity, 0.0f);

                    shape.numVertices = 4;

                    if (layoutSize != Vector2::ZERO){
                        shape.pointA = Vector2(0.0f, 0.0f);
                        shape.pointB = Vector2(layoutSize.x, layoutSize.y);
                        shape.vertices[0] = Vector2(0.0f, 0.0f);
                        shape.vertices[1] = Vector2(layoutSize.x, 0.0f);
                        shape.vertices[2] = Vector2(layoutSize.x, layoutSize.y);
                        shape.vertices[3] = Vector2(0.0f, layoutSize.y);
                    }else{
                        layoutSize = Vector2(100.0f, 100.0f);

                        const float halfW = layoutSize.x * 0.5f;
                        const float halfH = layoutSize.y * 0.5f;

                        shape.pointA = Vector2(-halfW, -halfH);
                        shape.pointB = Vector2(halfW, halfH);
                        shape.vertices[0] = Vector2(-halfW, -halfH);
                        shape.vertices[1] = Vector2(halfW, -halfH);
                        shape.vertices[2] = Vector2(halfW, halfH);
                        shape.vertices[3] = Vector2(-halfW, halfH);
                    }

                    break;
                }
                case Shape2DType::CIRCLE:
                    shape.pointA = Vector2::ZERO;
                    shape.radius = 0.5f;
                    break;
                case Shape2DType::CAPSULE:
                    shape.pointA = Vector2(0.0f, -0.5f);
                    shape.pointB = Vector2(0.0f, 0.5f);
                    shape.radius = 0.5f;
                    break;
                case Shape2DType::SEGMENT:
                    shape.pointA = Vector2(-1.0f, 0.0f);
                    shape.pointB = Vector2(1.0f, 0.0f);
                    break;
                case Shape2DType::CHAIN:
                    shape.numVertices = 4;
                    shape.vertices[0] = Vector2(-0.5f, -0.5f);
                    shape.vertices[1] = Vector2(0.5f, -0.5f);
                    shape.vertices[2] = Vector2(0.5f, 0.5f);
                    shape.vertices[3] = Vector2(-0.5f, 0.5f);
                    break;
                }

                size_t shapeIdx = bodyComp->numShapes;
                multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(shapeIdx) + "]", shape);
                multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", shapeIdx + 1);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    size_t numShapes = body.numShapes;
    for (Entity entity : entities){
        numShapes = std::min(numShapes, sceneProject->scene->getComponent<Body2DComponent>(entity).numShapes);
    }

    RowSettings settingsShapeType;
    settingsShapeType.enumEntries = &entriesShape2DType;

    RowSettings settingsShapeValue;

    for (size_t s = 0; s < numShapes; s++){
        Body2DComponent& bodyRef = sceneProject->scene->getComponent<Body2DComponent>(entities[0]);
        Shape2D& shape = bodyRef.shapes[s];

        ImGui::SeparatorText(("Shape " + std::to_string(s + 1)).c_str());
        ImGui::PushID((int)s);
        if (ImGui::Button("Remove Shape")){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

            for (Entity entity : entities){
                if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                    if (s >= bodyComp->numShapes){
                        continue;
                    }

                    for (size_t i = s + 1; i < bodyComp->numShapes; i++){
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(i - 1) + "]", bodyComp->shapes[i]);
                    }

                    multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(bodyComp->numShapes - 1) + "]", Shape2D());
                    multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", bodyComp->numShapes - 1);
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();

        std::string shapeKey = "shapes[" + std::to_string(s) + "]";

        beginTable(cpType, getLabelSize("Enable Hit Events"), "body2d_shape");
        propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".type", "Type", sceneProject, entities, settingsShapeType);

        RowSettings settingsFloat;
        //settingsFloat.stepSize = 0.1f;
        settingsFloat.secondColSize = 6 * ImGui::GetFontSize();

        if (shape.type == Shape2DType::POLYGON){
            propertyHeader("Vertices");
            ImGui::Text("%d", shape.numVertices);
            ImGui::SameLine();
            if (ImGui::Button("Add Vertex")){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;
                        Shape2D shapeValue = bodyComp->shapes[s];

                        const uint8_t oldCount = shapeValue.numVertices;
                        if (oldCount >= 2){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                        }else if (oldCount == 1){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector2(10.0f, 0.0f);
                        }else{
                            shapeValue.vertices[oldCount] = Vector2::ZERO;
                        }
                        shapeValue.numVertices = oldCount + 1;
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            bool removedVertex = false;
            float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
            float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
            ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
            if (inputVerSize.x < 100.0f){
                inputVerSize.x = 100.0f;
            }

            RowSettings settingsVertex = settingsShapeValue;
            settingsVertex.secondColSize = inputVerSize.x;

            for (int v = 0; v < (int)shape.numVertices; v++){

                propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_polygon_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                if (ImGui::Button(removeVertexId.c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;

                            Shape2D shapeValue = bodyComp->shapes[s];
                            if (v >= shapeValue.numVertices) continue;

                            for (size_t i = (size_t)v + 1; i < shapeValue.numVertices; i++){
                                shapeValue.vertices[i - 1] = shapeValue.vertices[i];
                            }
                            shapeValue.numVertices -= 1;

                            multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                    removedVertex = true;
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    break;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Remove vertex");
                }
            }

            if (removedVertex){
                endTable();
                return;
            }

            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsFloat);
        }else if (shape.type == Shape2DType::CIRCLE){
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointA", "Center", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsFloat);
        }else if (shape.type == Shape2DType::CAPSULE){
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointA", "Center A", sceneProject, entities);
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointB", "Center B", sceneProject, entities);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsFloat);
        }else if (shape.type == Shape2DType::SEGMENT){
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointA", "Point A", sceneProject, entities);
            propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".pointB", "Point B", sceneProject, entities);
        }else if (shape.type == Shape2DType::CHAIN){
            RowSettings loopSettings;
            loopSettings.help = "An open chain doesn't collide on its first and last edges";
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".loop", "Loop", sceneProject, entities, loopSettings);

            propertyHeader("Collision Side");
            if (ImGui::Button("Reverse")){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;
                        Shape2D shapeValue = bodyComp->shapes[s];

                        std::reverse(shapeValue.vertices.data(), shapeValue.vertices.data() + shapeValue.numVertices);
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            if (ImGui::IsItemHovered()){
                ImGui::SetTooltip("Flip the side that collides, shown by the ticks in the scene");
            }

            propertyHeader("Vertices");
            ImGui::Text("%d", shape.numVertices);
            ImGui::SameLine();
            if (ImGui::Button("Add Vertex")){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;
                        Shape2D shapeValue = bodyComp->shapes[s];

                        const uint8_t oldCount = shapeValue.numVertices;
                        if (oldCount >= 2){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                        }else if (oldCount == 1){
                            shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector2(10.0f, 0.0f);
                        }else{
                            shapeValue.vertices[oldCount] = Vector2::ZERO;
                        }
                        shapeValue.numVertices = oldCount + 1;
                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }

            bool removedVertex = false;
            float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
            float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
            ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
            if (inputVerSize.x < 100.0f){
                inputVerSize.x = 100.0f;
            }

            RowSettings settingsVertex = settingsShapeValue;
            settingsVertex.secondColSize = inputVerSize.x;

            for (int v = 0; v < (int)shape.numVertices; v++){
                propertyRow(RowPropertyType::Vector2, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);

                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_chain_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                if (ImGui::Button(removeVertexId.c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;

                            Shape2D shapeValue = bodyComp->shapes[s];
                            if (v >= shapeValue.numVertices) continue;

                            for (size_t i = (size_t)v + 1; i < shapeValue.numVertices; i++){
                                shapeValue.vertices[i - 1] = shapeValue.vertices[i];
                            }
                            shapeValue.numVertices -= 1;

                            multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                    removedVertex = true;
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    break;
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Remove vertex");
                }
            }

            if (removedVertex){
                endTable();
                return;
            }
        }

        if (shape.type != Shape2DType::SEGMENT && shape.type != Shape2DType::CHAIN){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".density", "Density", sceneProject, entities, settingsFloat);
        }
        propertyRow(RowPropertyType::Float, cpType, shapeKey + ".friction", "Friction", sceneProject, entities, settingsFloat);
        propertyRow(RowPropertyType::Float, cpType, shapeKey + ".restitution", "Restitution", sceneProject, entities, settingsFloat);
        if (shape.type != Shape2DType::CHAIN){
            RowSettings sensorSettings;
            sensorSettings.help = "Overlap only: never collides and always reports its sensor events";
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".sensor", "Sensor", sceneProject, entities, sensorSettings);
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".enableHitEvents", "Enable Hit Events", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".contactEvents", "Contact Events", sceneProject, entities);
            propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".preSolveEvents", "PreSolve Events", sceneProject, entities);
            if (!shape.sensor){
                propertyRow(RowPropertyType::Bool, cpType, shapeKey + ".sensorEvents", "Sensor Events", sceneProject, entities);
            }
        }
        if (shape.type == Shape2DType::POLYGON){
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Colors::SubtleText);
            propertyHeader("Reset shape");
            ImGui::PopStyleColor();
            if (ImGui::SmallButton(("Box##shape_preset_box_" + std::to_string(s)).c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;

                        Shape2D shapeValue = bodyComp->shapes[s];
                        Vector2 layoutSize = getSizeForShape(entity);

                        shapeValue.numVertices = 4;
                        shapeValue.radius = 0.0f;
                        shapeValue.pointA = Vector2(0.0f, 0.0f);
                        shapeValue.pointB = Vector2(layoutSize.x, layoutSize.y);
                        shapeValue.vertices[0] = Vector2(0.0f, 0.0f);
                        shapeValue.vertices[1] = Vector2(layoutSize.x, 0.0f);
                        shapeValue.vertices[2] = Vector2(layoutSize.x, layoutSize.y);
                        shapeValue.vertices[3] = Vector2(0.0f, layoutSize.y);

                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            if (ImGui::SmallButton(("Centered Box##shape_preset_centered_box_" + std::to_string(s)).c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;

                        Shape2D shapeValue = bodyComp->shapes[s];
                        Vector2 layoutSize = getSizeForShape(entity);
                        const float halfW = layoutSize.x * 0.5f;
                        const float halfH = layoutSize.y * 0.5f;

                        shapeValue.numVertices = 4;
                        shapeValue.radius = 0.0f;
                        shapeValue.pointA = Vector2(-halfW, -halfH);
                        shapeValue.pointB = Vector2(halfW, halfH);
                        shapeValue.vertices[0] = Vector2(-halfW, -halfH);
                        shapeValue.vertices[1] = Vector2(halfW, -halfH);
                        shapeValue.vertices[2] = Vector2(halfW, halfH);
                        shapeValue.vertices[3] = Vector2(-halfW, halfH);

                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            if (ImGui::SmallButton(("Rounded Box##shape_preset_rounded_box_" + std::to_string(s)).c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Body2DComponent* bodyComp = sceneProject->scene->findComponent<Body2DComponent>(entity)){
                        if (s >= bodyComp->numShapes) continue;

                        Shape2D shapeValue = bodyComp->shapes[s];
                        Vector2 layoutSize = getSizeForShape(entity);
                        const float halfW = layoutSize.x * 0.5f;
                        const float halfH = layoutSize.y * 0.5f;

                        shapeValue.numVertices = 4;
                        shapeValue.radius = std::max(1.0f, std::min(layoutSize.x, layoutSize.y) * 0.04f);
                        shapeValue.pointA = Vector2(-halfW, -halfH);
                        shapeValue.pointB = Vector2(halfW, halfH);
                        shapeValue.vertices[0] = Vector2(-halfW, -halfH);
                        shapeValue.vertices[1] = Vector2(halfW, -halfH);
                        shapeValue.vertices[2] = Vector2(halfW, halfH);
                        shapeValue.vertices[3] = Vector2(-halfW, halfH);

                        multiCmd->addPropertyCmd<Shape2D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }
        endTable();
    }
}

void editor::Properties::drawBody3DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Body3DComponent& body = sceneProject->scene->getComponent<Body3DComponent>(entities[0]);

    RowSettings settingsBodyType;
    settingsBodyType.enumEntries = &entriesBodyType;

    RowSettings settingsMotionQuality;
    settingsMotionQuality.enumEntries = &entriesBody3DMotionQuality;

    RowSettings settingsBodyValue;

    beginTable(cpType, getLabelSize("Motion Quality"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Body Type", sceneProject, entities, settingsBodyType);
    propertyRow(RowPropertyType::Enum, cpType, "motionQuality", "Motion Quality", sceneProject, entities, settingsMotionQuality);
    propertyRow(RowPropertyType::Bool, cpType, "sensor", "Sensor", sceneProject, entities, settingsBodyValue);
    propertyRow(RowPropertyType::Float, cpType, "gravityFactor", "Gravity Factor", sceneProject, entities, settingsBodyValue);
    propertyRow(RowPropertyType::Bool, cpType, "overrideMassProperties", "Override Mass", sceneProject, entities, settingsBodyValue);

    if (body.overrideMassProperties) {
        propertyRow(RowPropertyType::Vector3, cpType, "solidBoxSize", "Solid Box Size", sceneProject, entities, settingsBodyValue);
        propertyRow(RowPropertyType::Float, cpType, "solidBoxDensity", "Solid Box Density", sceneProject, entities, settingsBodyValue);
    }
    endTable();

    if (body.type == BodyType::DYNAMIC) {
        ImGui::SeparatorText("Buoyancy");
        beginTable(cpType, getLabelSize("Angular Drag"), "buoyancy");
        RowSettings buoyancySettings;
        buoyancySettings.stepSize = 0.01f;
        buoyancySettings.help = "How the body floats in Water. 1 is neutral, more floats and less sinks. 0 ignores the water.";
        RowSettings dragSettings;
        dragSettings.stepSize = 0.01f;
        dragSettings.help = "How much the water slows the body down.";
        RowSettings angularDragSettings;
        angularDragSettings.stepSize = 0.001f;
        angularDragSettings.format = "%.3f";
        angularDragSettings.help = "How much the water slows the body rotation.";
        propertyRow(RowPropertyType::FloatPositive, cpType, "buoyancy", "Buoyancy", sceneProject, entities, buoyancySettings);
        if (body.buoyancy > 0.0f) {
            propertyRow(RowPropertyType::FloatPositive, cpType, "waterDrag", "Drag", sceneProject, entities, dragSettings);
            propertyRow(RowPropertyType::FloatPositive, cpType, "waterAngularDrag", "Angular Drag", sceneProject, entities, angularDragSettings);
        }
        endTable();
    }

    ImGui::SeparatorText("Shapes");
    ImGui::Text("Shapes: %zu", body.numShapes);

    static int createShape3DType = (int)Shape3DType::BOX;
    const char* createShape3DLabel = "Unknown";
    for (const EnumEntry& entry : entriesShape3DType){
        if (entry.value == createShape3DType){
            createShape3DLabel = entry.name;
            break;
        }
    }

    ImGui::SetNextItemWidth(9 * ImGui::GetFontSize());
    if (ImGui::BeginCombo("##shape3d_create_type", createShape3DLabel)){
        for (const EnumEntry& entry : entriesShape3DType){
            bool selected = (createShape3DType == entry.value);
            if (ImGui::Selectable(entry.name, selected)){
                createShape3DType = entry.value;
            }
            if (selected){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Shape##body3d")){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

        for (Entity entity : entities){
            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){

                Shape3D shape;
                shape.type = (Shape3DType)createShape3DType;
                if (shape.type == Shape3DType::MESH){
                    shape.source = Shape3DSource::ENTITY_MESH;
                    if (sceneProject->scene->findComponent<MeshComponent>(entity)){
                        shape.sourceEntity = entity;
                    }
                }else if (shape.type == Shape3DType::CONVEX_HULL){
                    shape.source = Shape3DSource::ENTITY_MESH;
                    if (sceneProject->scene->findComponent<MeshComponent>(entity)){
                        shape.sourceEntity = entity;
                    }
                }else if (shape.type == Shape3DType::HEIGHTFIELD){
                    shape.source = Shape3DSource::ENTITY_HEIGHTFIELD;
                    shape.samplesSize = 256;
                    if (sceneProject->scene->findComponent<TerrainComponent>(entity)){
                        shape.sourceEntity = entity;
                    }
                }

                size_t shapeIdx = bodyComp->numShapes;
                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(shapeIdx) + "]", shape);
                multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", shapeIdx + 1);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    size_t numShapes = body.numShapes;
    for (Entity entity : entities){
        numShapes = std::min(numShapes, sceneProject->scene->getComponent<Body3DComponent>(entity).numShapes);
    }

    RowSettings settingsShapeSourceConvexHull;
    settingsShapeSourceConvexHull.enumEntries = &entriesShape3DSourceConvexHull;

    RowSettings settingsShapeSourceMesh;
    settingsShapeSourceMesh.enumEntries = &entriesShape3DSourceMesh;

    RowSettings settingsShapeSourceHeightfield;
    settingsShapeSourceHeightfield.enumEntries = &entriesShape3DSourceHeightfield;

    RowSettings settingsShapeValue;

    auto normalizeShape3DSource = [](Shape3DType type, Shape3DSource source) {
        if (type == Shape3DType::CONVEX_HULL){
            if (source != Shape3DSource::RAW_VERTICES && source != Shape3DSource::ENTITY_MESH){
                return Shape3DSource::RAW_VERTICES;
            }
        }else if (type == Shape3DType::MESH){
            if (source != Shape3DSource::RAW_MESH && source != Shape3DSource::ENTITY_MESH){
                return Shape3DSource::RAW_MESH;
            }
        }else if (type == Shape3DType::HEIGHTFIELD){
            if (source != Shape3DSource::ENTITY_HEIGHTFIELD){
                return Shape3DSource::ENTITY_HEIGHTFIELD;
            }
        }

        return source;
    };

    for (size_t s = 0; s < numShapes; s++){
        Body3DComponent& bodyRef = sceneProject->scene->getComponent<Body3DComponent>(entities[0]);
        Shape3D& shape = bodyRef.shapes[s];
        Transform* bodyTransform = sceneProject->scene->findComponent<Transform>(entities[0]);
        const Vector3 bodyScale = bodyTransform ? bodyTransform->scale : Vector3::UNIT_SCALE;
        const float physicsOffset = (shape.position * bodyScale).length();
        const bool suspiciousSingleShapeOffset = bodyRef.numShapes == 1 && physicsOffset > Stream::MAX_SINGLE_SHAPE_PHYSICS_OFFSET;

        ImGui::SeparatorText(("Shape " + std::to_string(s + 1)).c_str());
        ImGui::PushID((int)s);
        if (ImGui::Button("Remove Shape")){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

            for (Entity entity : entities){
                if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                    if (s >= bodyComp->numShapes){
                        continue;
                    }

                    for (size_t i = s + 1; i < bodyComp->numShapes; i++){
                        multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(i - 1) + "]", bodyComp->shapes[i]);
                    }

                    multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, "shapes[" + std::to_string(bodyComp->numShapes - 1) + "]", Shape3D());
                    multiCmd->addPropertyCmd<size_t>(project, sceneProject->id, entity, cpType, "numShapes", bodyComp->numShapes - 1);
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();

        std::string shapeKey = "shapes[" + std::to_string(s) + "]";

        beginTable(cpType, getLabelSize("Bottom Radius"), "body3d_shape");

        PropertyData shapeTypeProp = Catalog::getProperty(sceneProject->scene, entities[0], cpType, shapeKey + ".type");
        int* shapeTypeDef = static_cast<int*>(shapeTypeProp.def);
        int shapeTypeValue = *static_cast<int*>(shapeTypeProp.ref);
        bool shapeTypeMixed = false;
        for (size_t entityIndex = 1; entityIndex < entities.size(); entityIndex++){
            PropertyData otherShapeTypeProp = Catalog::findProperty(sceneProject->scene, entities[entityIndex], cpType, shapeKey + ".type");
            // shapes are counted from the first body, the others may have fewer
            if (!otherShapeTypeProp.ref){
                continue;
            }
            if (*static_cast<int*>(otherShapeTypeProp.ref) != shapeTypeValue){
                shapeTypeMixed = true;
                break;
            }
        }

        int shapeTypeItemCurrent = 0;
        for (size_t i = 0; i < entriesShape3DType.size(); ++i) {
            if (entriesShape3DType[i].value == shapeTypeValue) {
                shapeTypeItemCurrent = static_cast<int>(i);
                break;
            }
        }

        int shapeTypeItemDefault = shapeTypeItemCurrent;
        bool shapeTypeDefChanged = false;
        if (shapeTypeDef){
            for (size_t i = 0; i < entriesShape3DType.size(); ++i) {
                if (entriesShape3DType[i].value == *shapeTypeDef) {
                    shapeTypeItemDefault = static_cast<int>(i);
                    break;
                }
            }
            shapeTypeDefChanged = (shapeTypeItemCurrent != shapeTypeItemDefault);
        }

        if (propertyHeader("Type", -1, shapeTypeDefChanged, false)){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            Shape3DType resetType = static_cast<Shape3DType>(entriesShape3DType[shapeTypeItemDefault].value);

            for (Entity entity : entities){
                if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                    if (s >= bodyComp->numShapes) continue;

                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".type", (int)resetType);

                    Shape3DSource normalizedSource = normalizeShape3DSource(resetType, bodyComp->shapes[s].source);
                    if (normalizedSource != bodyComp->shapes[s].source){
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".source", (int)normalizedSource);
                    }
                    if (bodyComp->shapes[s].sourceEntity == NULL_ENTITY && normalizedSource != Shape3DSource::NONE){
                        multiCmd->addPropertyCmd<Entity>(project, sceneProject->id, entity, cpType, shapeKey + ".sourceEntity", entity);
                    }
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }

        std::vector<const char*> shapeTypeNames;
        for (const auto& entry : entriesShape3DType) {
            shapeTypeNames.push_back(entry.name);
        }

        if (shapeTypeMixed)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Combo(("##combo_" + shapeKey + ".type").c_str(), &shapeTypeItemCurrent, shapeTypeNames.data(), static_cast<int>(shapeTypeNames.size()))) {
            Shape3DType newShapeType = static_cast<Shape3DType>(entriesShape3DType[shapeTypeItemCurrent].value);
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

            for (Entity entity : entities){
                if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                    if (s >= bodyComp->numShapes) continue;

                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".type", (int)newShapeType);

                    Shape3DSource normalizedSource = normalizeShape3DSource(newShapeType, bodyComp->shapes[s].source);
                    if (normalizedSource != bodyComp->shapes[s].source){
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, entity, cpType, shapeKey + ".source", (int)normalizedSource);
                    }
                    if (bodyComp->shapes[s].sourceEntity == NULL_ENTITY && normalizedSource != Shape3DSource::NONE){
                        multiCmd->addPropertyCmd<Entity>(project, sceneProject->id, entity, cpType, shapeKey + ".sourceEntity", entity);
                    }
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }
        if (shapeTypeMixed)
            ImGui::PopStyleColor();

        Shape3DType shapeTypeUI = sceneProject->scene->getComponent<Body3DComponent>(entities[0]).shapes[s].type;
        Shape3DSource shapeSourceUI = sceneProject->scene->getComponent<Body3DComponent>(entities[0]).shapes[s].source;
        Shape3DSource normalizedSourceUI = normalizeShape3DSource(shapeTypeUI, shapeSourceUI);

        propertyRow(RowPropertyType::Vector3, cpType, shapeKey + ".position", "Position", sceneProject, entities, settingsShapeValue);

        if (suspiciousSingleShapeOffset){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Warning");
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 196, 64, 255));
            ImGui::TextUnformatted("Large scaled collider offset can destabilize joints.");
            ImGui::PopStyleColor();
            ImGui::SameLine();

            std::string resetButtonId = "Reset##shape3d_pos_reset_" + std::to_string(s);
            if (ImGui::SmallButton(resetButtonId.c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();

                for (Entity entity : entities){
                    if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                        if (s >= bodyComp->numShapes){
                            continue;
                        }

                        multiCmd->addPropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, shapeKey + ".position", Vector3::ZERO);
                    }
                }

                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
        }

        propertyRow(RowPropertyType::Quat, cpType, shapeKey + ".rotation", "Rotation", sceneProject, entities, settingsShapeValue);

        if (shape.type == Shape3DType::BOX){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".width", "Width", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".height", "Height", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".depth", "Depth", sceneProject, entities, settingsShapeValue);
        }else if (shape.type == Shape3DType::SPHERE){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsShapeValue);
        }else if (shape.type == Shape3DType::CAPSULE || shape.type == Shape3DType::CYLINDER){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".halfHeight", "Half Height", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".radius", "Radius", sceneProject, entities, settingsShapeValue);
        }else if (shape.type == Shape3DType::TAPERED_CAPSULE){
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".halfHeight", "Half Height", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".topRadius", "Top Radius", sceneProject, entities, settingsShapeValue);
            propertyRow(RowPropertyType::Float, cpType, shapeKey + ".bottomRadius", "Bottom Radius", sceneProject, entities, settingsShapeValue);
        }

        if (shapeTypeUI == Shape3DType::CONVEX_HULL){
            propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".source", "Source", sceneProject, entities, settingsShapeSourceConvexHull);
            if (normalizedSourceUI == Shape3DSource::ENTITY_MESH){
                propertyRow(RowPropertyType::LocalEntity, cpType, shapeKey + ".sourceEntity", "Source Entity", sceneProject, entities, settingsShapeValue);
            }else if (normalizedSourceUI == Shape3DSource::RAW_VERTICES){
                propertyHeader("Vertices");
                ImGui::Text("%d", shape.numVertices);
                ImGui::SameLine();
                if (ImGui::Button(("Add Vertex##convex_" + std::to_string(s)).c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;
                            Shape3D shapeValue = bodyComp->shapes[s];
                            const uint16_t oldCount = shapeValue.numVertices;
                            if (oldCount >= 2){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                            }else if (oldCount == 1){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector3(1.0f, 0.0f, 0.0f);
                            }else{
                                shapeValue.vertices[oldCount] = Vector3::ZERO;
                            }
                            shapeValue.numVertices = oldCount + 1;
                            multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }
                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }

                float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
                if (inputVerSize.x < 100.0f){
                    inputVerSize.x = 100.0f;
                }
                RowSettings settingsVertex = settingsShapeValue;
                settingsVertex.secondColSize = inputVerSize.x;

                for (int v = 0; v < (int)shape.numVertices; v++){
                    propertyRow(RowPropertyType::Vector3, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);
                    ImGui::SameLine();

                    std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_convex_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(removeVertexId.c_str())){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (v >= shapeValue.numVertices) continue;
                                for (int vi = v; vi < shapeValue.numVertices - 1; vi++){
                                    shapeValue.vertices[vi] = shapeValue.vertices[vi + 1];
                                }
                                shapeValue.numVertices--;
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove vertex");
                    }
                }
            }
        }else if (shapeTypeUI == Shape3DType::MESH){
            propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".source", "Source", sceneProject, entities, settingsShapeSourceMesh);
            if (normalizedSourceUI == Shape3DSource::ENTITY_MESH){
                propertyRow(RowPropertyType::LocalEntity, cpType, shapeKey + ".sourceEntity", "Source Entity", sceneProject, entities, settingsShapeValue);
            }else if (normalizedSourceUI == Shape3DSource::RAW_MESH){
                // Vertices
                propertyHeader("Vertices");
                ImGui::Text("%d", shape.numVertices);
                ImGui::SameLine();
                if (ImGui::Button(("Add Vertex##mesh_" + std::to_string(s)).c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;
                            Shape3D shapeValue = bodyComp->shapes[s];
                            const uint16_t oldCount = shapeValue.numVertices;
                            if (oldCount >= 2){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[oldCount - 1] + (shapeValue.vertices[oldCount - 1] - shapeValue.vertices[oldCount - 2]);
                            }else if (oldCount == 1){
                                shapeValue.vertices[oldCount] = shapeValue.vertices[0] + Vector3(1.0f, 0.0f, 0.0f);
                            }else{
                                shapeValue.vertices[oldCount] = Vector3::ZERO;
                            }
                            shapeValue.numVertices = oldCount + 1;
                            multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }
                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }

                float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
                float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
                ImVec2 inputVerSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
                if (inputVerSize.x < 100.0f){
                    inputVerSize.x = 100.0f;
                }
                RowSettings settingsVertex = settingsShapeValue;
                settingsVertex.secondColSize = inputVerSize.x;

                for (int v = 0; v < (int)shape.numVertices; v++){
                    propertyRow(RowPropertyType::Vector3, cpType, shapeKey + ".vertices[" + std::to_string(v) + "]", "Vertex " + std::to_string(v + 1), sceneProject, entities, settingsVertex);
                    ImGui::SameLine();

                    std::string removeVertexId = std::string(ICON_FA_TRASH_CAN) + "##remove_mesh_vertex_" + std::to_string(s) + "_" + std::to_string(v);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(removeVertexId.c_str())){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (v >= shapeValue.numVertices) continue;
                                for (int vi = v; vi < shapeValue.numVertices - 1; vi++){
                                    shapeValue.vertices[vi] = shapeValue.vertices[vi + 1];
                                }
                                shapeValue.numVertices--;
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove vertex");
                    }
                }

                // Triangles (indices)
                const int triangleCount = shape.numIndices / 3;
                propertyHeader("Triangles");
                ImGui::Text("%d", triangleCount);
                ImGui::SameLine();
                if (ImGui::Button(("Add Triangle##mesh_tri_" + std::to_string(s)).c_str())){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                            if (s >= bodyComp->numShapes) continue;
                            Shape3D shapeValue = bodyComp->shapes[s];
                            const uint16_t idx = shapeValue.numIndices;
                            shapeValue.indices[idx] = 0;
                            shapeValue.indices[idx + 1] = (shapeValue.numVertices > 1) ? (uint16_t)1 : (uint16_t)0;
                            shapeValue.indices[idx + 2] = (shapeValue.numVertices > 2) ? (uint16_t)2 : (uint16_t)0;
                            shapeValue.numIndices = idx + 3;
                            multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                        }
                    }
                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }

                for (int t = 0; t < triangleCount; t++){
                    int baseIdx = t * 3;
                    std::string triLabel = "Triangle " + std::to_string(t + 1);
                    propertyHeader(triLabel);

                    int triIndices[3] = { (int)shape.indices[baseIdx], (int)shape.indices[baseIdx + 1], (int)shape.indices[baseIdx + 2] };
                    std::string triId = "##mesh_tri_" + std::to_string(s) + "_" + std::to_string(t);

                    ImGui::SetNextItemWidth(inputVerSize.x);
                    if (ImGui::InputInt3(triId.c_str(), triIndices)){
                        for (int k = 0; k < 3; k++){
                            triIndices[k] = std::max(0, std::min(triIndices[k], (int)shape.numVertices - 1));
                        }
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (baseIdx + 2 >= shapeValue.numIndices) continue;
                                shapeValue.indices[baseIdx] = (uint16_t)triIndices[0];
                                shapeValue.indices[baseIdx + 1] = (uint16_t)triIndices[1];
                                shapeValue.indices[baseIdx + 2] = (uint16_t)triIndices[2];
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::SameLine();
                    std::string removeTriId = std::string(ICON_FA_TRASH_CAN) + "##remove_mesh_tri_" + std::to_string(s) + "_" + std::to_string(t);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
                    if (ImGui::Button(removeTriId.c_str())){
                        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                        for (Entity entity : entities){
                            if (Body3DComponent* bodyComp = sceneProject->scene->findComponent<Body3DComponent>(entity)){
                                if (s >= bodyComp->numShapes) continue;
                                Shape3D shapeValue = bodyComp->shapes[s];
                                if (baseIdx + 2 >= shapeValue.numIndices) continue;
                                for (int ii = baseIdx; ii < shapeValue.numIndices - 3; ii++){
                                    shapeValue.indices[ii] = shapeValue.indices[ii + 3];
                                }
                                shapeValue.numIndices -= 3;
                                multiCmd->addPropertyCmd<Shape3D>(project, sceneProject->id, entity, cpType, shapeKey, shapeValue);
                            }
                        }
                        multiCmd->setNoMerge();
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Remove triangle");
                    }
                }
            }
        }else if (shapeTypeUI == Shape3DType::HEIGHTFIELD){
            propertyRow(RowPropertyType::Enum, cpType, shapeKey + ".source", "Source", sceneProject, entities, settingsShapeSourceHeightfield);
            if (normalizedSourceUI == Shape3DSource::ENTITY_HEIGHTFIELD){
                propertyRow(RowPropertyType::LocalEntity, cpType, shapeKey + ".sourceEntity", "Source Entity", sceneProject, entities, settingsShapeValue);
            }
            propertyRow(RowPropertyType::UInt, cpType, shapeKey + ".samplesSize", "Samples Size", sceneProject, entities, settingsShapeValue);
        }

        propertyRow(RowPropertyType::Float, cpType, shapeKey + ".density", "Density", sceneProject, entities, settingsShapeValue);
        endTable();
    }
}

void editor::Properties::drawJoint2DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Joint2DComponent& joint = sceneProject->scene->getComponent<Joint2DComponent>(entities[0]);

    auto markJoint2DDirty = [sceneProject, entities](){
        for (Entity entity : entities){
            if (Joint2DComponent* jointComp = sceneProject->scene->findComponent<Joint2DComponent>(entity)){
                jointComp->needUpdateJoint = true;
            }
        }
    };

    RowSettings settingsJointType;
    settingsJointType.enumEntries = &entriesJoint2DType;
    settingsJointType.onValueChanged = markJoint2DDirty;

    RowSettings settingsJointValue;
    settingsJointValue.onValueChanged = markJoint2DDirty;
    settingsJointValue.entityFilter.set(sceneProject->scene->getComponentId<Body2DComponent>());

    beginTable(cpType, getLabelSize("Joint Type"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Joint Type", sceneProject, entities, settingsJointType);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyA", "Body A", sceneProject, entities, settingsJointValue);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyB", "Body B", sceneProject, entities, settingsJointValue);

    endTable();

    bool hasTypeSettings = joint.type != Joint2DType::MOTOR;
    if (!hasTypeSettings){
        return;
    }

    const char* joint2DTableLabel = "Anchor";
    if (joint.type == Joint2DType::DISTANCE){
        joint2DTableLabel = "Anchor A";
    }else if (joint.type == Joint2DType::MOUSE){
        joint2DTableLabel = "Target";
    }

    ImGui::SeparatorText("Joint settings");
    std::string joint2DTypeTableId = "joint2d_type_settings_" + std::to_string((int)joint.type);
    beginTable(cpType, getLabelSize(joint2DTableLabel), joint2DTypeTableId);

    if (joint.type == Joint2DType::DISTANCE){
        propertyRow(RowPropertyType::Bool, cpType, "autoAnchors", "Auto Anchors", sceneProject, entities, settingsJointValue);
        if (!joint.autoAnchors){
            propertyRow(RowPropertyType::Vector2, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
            propertyRow(RowPropertyType::Vector2, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        }
        propertyRow(RowPropertyType::Bool, cpType, "rope", "Rope", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint2DType::REVOLUTE || joint.type == Joint2DType::WELD){
        propertyRow(RowPropertyType::Vector2, cpType, "anchorA", "Anchor", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint2DType::PRISMATIC || joint.type == Joint2DType::WHEEL){
        propertyRow(RowPropertyType::Vector2, cpType, "anchorA", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector2, cpType, "axis", "Axis", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint2DType::MOUSE){
        propertyRow(RowPropertyType::Vector2, cpType, "target", "Target", sceneProject, entities, settingsJointValue);
    }

    endTable();
}

void editor::Properties::drawJoint3DComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Joint3DComponent& joint = sceneProject->scene->getComponent<Joint3DComponent>(entities[0]);

    auto markJoint3DDirty = [sceneProject, entities](){
        for (Entity entity : entities){
            if (Joint3DComponent* jointComp = sceneProject->scene->findComponent<Joint3DComponent>(entity)){
                jointComp->needUpdateJoint = true;
            }
        }
    };

    RowSettings settingsJointType;
    settingsJointType.enumEntries = &entriesJoint3DType;
    settingsJointType.onValueChanged = markJoint3DDirty;

    RowSettings settingsJointValue;
    settingsJointValue.onValueChanged = markJoint3DDirty;
    settingsJointValue.entityFilter.set(sceneProject->scene->getComponentId<Body3DComponent>());

    beginTable(cpType, getLabelSize("Joint Type"));
    propertyRow(RowPropertyType::Enum, cpType, "type", "Joint Type", sceneProject, entities, settingsJointType);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyA", "Body A", sceneProject, entities, settingsJointValue);
    propertyRow(RowPropertyType::LocalEntity, cpType, "bodyB", "Body B", sceneProject, entities, settingsJointValue);

    endTable();

    bool hasTypeSettings = joint.type != Joint3DType::FIXED;
    if (!hasTypeSettings){
        return;
    }

    const char* joint3DTableLabel = "Anchor";
    switch (joint.type){
        case Joint3DType::DISTANCE:
            joint3DTableLabel = "Auto Anchors";
            break;
        case Joint3DType::POINT:
            joint3DTableLabel = "Anchor";
            break;
        case Joint3DType::HINGE:
            joint3DTableLabel = "Normal";
            break;
        case Joint3DType::CONE:
            joint3DTableLabel = "Twist Axis";
            break;
        case Joint3DType::PRISMATIC:
            joint3DTableLabel = "Limits Min";
            break;
        case Joint3DType::SWINGTWIST:
            joint3DTableLabel = "Normal HalfCone";
            break;
        case Joint3DType::SIXDOF:
            joint3DTableLabel = "Anchor A";
            break;
        case Joint3DType::GEAR:
            joint3DTableLabel = "Teeth Gear A";
            break;
        case Joint3DType::RACKANDPINON:
            joint3DTableLabel = "Rack Length";
            break;
        case Joint3DType::PULLEY:
            joint3DTableLabel = "Fixed Point A";
            break;
        case Joint3DType::PATH:
            joint3DTableLabel = "Position";
            break;
        default:
            break;
    }

    ImGui::SeparatorText("Joint settings");
    std::string joint3DTypeTableId = "joint3d_type_settings_" + std::to_string((int)joint.type);
    beginTable(cpType, getLabelSize(joint3DTableLabel), joint3DTypeTableId);

    if (joint.type == Joint3DType::DISTANCE){
        propertyRow(RowPropertyType::Bool, cpType, "autoAnchors", "Auto Anchors", sceneProject, entities, settingsJointValue);
        if (!joint.autoAnchors){
            propertyRow(RowPropertyType::Vector3, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
            propertyRow(RowPropertyType::Vector3, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        }
    }else if (joint.type == Joint3DType::POINT){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::HINGE){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "axis", "Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "normal", "Normal", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::CONE){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "twistAxis", "Twist Axis", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::PRISMATIC){
        propertyRow(RowPropertyType::Vector3, cpType, "axis", "Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "limitsMin", "Limits Min", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "limitsMax", "Limits Max", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::SWINGTWIST){
        propertyRow(RowPropertyType::Vector3, cpType, "anchor", "Anchor", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "twistAxis", "Twist Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "planeAxis", "Plane Axis", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "normalHalfConeAngle", "Normal HalfCone", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "planeHalfConeAngle", "Plane HalfCone", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "twistMinAngle", "Twist Min Angle", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Float, cpType, "twistMaxAngle", "Twist Max Angle", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::SIXDOF){
        propertyRow(RowPropertyType::Vector3, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "axisX", "Axis X", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "axisY", "Axis Y", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::GEAR){
        propertyRow(RowPropertyType::LocalEntity, cpType, "hingeA", "Hinge A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::LocalEntity, cpType, "hingeB", "Hinge B", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethGearA", "Teeth Gear A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethGearB", "Teeth Gear B", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::RACKANDPINON){
        propertyRow(RowPropertyType::LocalEntity, cpType, "hinge", "Hinge", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::LocalEntity, cpType, "slider", "Slider", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethRack", "Teeth Rack", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "numTeethGear", "Teeth Gear", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Int, cpType, "rackLength", "Rack Length", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::PULLEY){
        propertyRow(RowPropertyType::Vector3, cpType, "anchorA", "Anchor A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "anchorB", "Anchor B", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "fixedPointA", "Fixed Point A", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Vector3, cpType, "fixedPointB", "Fixed Point B", sceneProject, entities, settingsJointValue);
    }else if (joint.type == Joint3DType::PATH){
        propertyRow(RowPropertyType::Vector3, cpType, "pathPosition", "Position", sceneProject, entities, settingsJointValue);
        propertyRow(RowPropertyType::Bool, cpType, "isLooping", "Looping", sceneProject, entities, settingsJointValue);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Points");
        ImGui::TableSetColumnIndex(1);

        ImGui::Text("%zu", joint.pathPoints.size());
        if (drawSummaryAddButton("Add Point##joint3d_path_add")) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (Entity entity : entities){
                if (Joint3DComponent* jointComp = sceneProject->scene->findComponent<Joint3DComponent>(entity)){
                    std::vector<Vector3> newPoints = jointComp->pathPoints;
                    Vector3 newPoint = Vector3::ZERO;
                    if (!newPoints.empty()){
                        Vector3 lastPoint = newPoints.back();
                        newPoint = Vector3(lastPoint.x, lastPoint.y, lastPoint.z + 1.0f);
                    }
                    newPoints.push_back(newPoint);
                    multiCmd->addPropertyCmd<std::vector<Vector3>>(project, sceneProject->id, entity, cpType, "pathPoints", newPoints, settingsJointValue.onValueChanged);
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
        }

        bool removedPoint = false;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
        if (inputSize.x < 100.0f){
            inputSize.x = 100.0f;
        }

        for (size_t pointIndex = 0; pointIndex < joint.pathPoints.size(); pointIndex++){
            std::string pointId = "pathPoints[" + std::to_string(pointIndex) + "]";
            std::string pointLabel = "Point " + std::to_string(pointIndex);

            RowSettings settingsPathPoint = settingsJointValue;
            settingsPathPoint.secondColSize = inputSize.x;
            propertyRow(RowPropertyType::Vector3, cpType, pointId, pointLabel, sceneProject, entities, settingsPathPoint);

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            std::string removePointId = std::string(ICON_FA_TRASH_CAN) + "##remove_path_point_" + std::to_string(pointIndex);
            if (ImGui::Button(removePointId.c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (Joint3DComponent* jointComp = sceneProject->scene->findComponent<Joint3DComponent>(entity)){
                        if (pointIndex < jointComp->pathPoints.size()){
                            std::vector<Vector3> newPoints = jointComp->pathPoints;
                            newPoints.erase(newPoints.begin() + (long int)pointIndex);
                            multiCmd->addPropertyCmd<std::vector<Vector3>>(project, sceneProject->id, entity, cpType, "pathPoints", newPoints, settingsJointValue.onValueChanged);
                        }
                    }
                }

                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                removedPoint = true;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                break;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove point");
            }
        }

        if (removedPoint){
            endTable();
            return;
        }
    }

    endTable();
}

void editor::Properties::startActionPreview(Entity entity, Scene* scene, SceneProject* sceneProject) {
    if (actionPreviewing) return;

    ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
    if (!actionComp) return;

    actionPreviewStates.clear();
    actionPreviewEntity = entity;
    actionPreviewSceneId = sceneProject->id;

    // Collect all entities that will be affected by the preview
    std::unordered_set<Entity> collected;

    std::function<void(Entity)> collectEntities = [&](Entity e) {
        if (e == NULL_ENTITY || !scene->isEntityCreated(e) || !collected.insert(e).second) return;

        // Save this entity's state
        ActionPreviewState state;
        state.entity = e;
        if (Transform* transform = scene->findComponent<Transform>(e)) {
            state.parent = transform->parent;
        }
        state.components = Stream::encodeComponents(e, scene, scene->getSignature(e));
        actionPreviewStates.push_back(state);

        // If entity has an ActionComponent with a target, save the target too
        if (ActionComponent* ac = scene->findComponent<ActionComponent>(e)) {
            if (ac->target != NULL_ENTITY && scene->isEntityCreated(ac->target)) {
                collectEntities(ac->target);
            }
        }

        // If entity has an AnimationComponent, collect child action entities and their targets
        if (AnimationComponent* anim = scene->findComponent<AnimationComponent>(e)) {
            for (const ActionFrame& frame : anim->actions) {
                if (frame.action != NULL_ENTITY && scene->isEntityCreated(frame.action)) {
                    collectEntities(frame.action);
                }
            }
        }
    };

    collectEntities(entity);

    actionComp->state = ActionState::Stopped;
    actionComp->timecount = 0;
    actionComp->stopTrigger = false;
    actionComp->pauseTrigger = false;
    actionComp->startTrigger = true;

    actionPreviewing = true;
}

void editor::Properties::stopActionPreviewIfActive() {
    if (!actionPreviewing) {
        return;
    }

    SceneProject* previewSceneProject = project ? project->getScene(actionPreviewSceneId) : nullptr;
    if (previewSceneProject && previewSceneProject->scene) {
        stopActionPreview(previewSceneProject->scene, previewSceneProject);
        return;
    }

    actionPreviewStates.clear();
    actionPreviewing = false;
    actionPreviewPlaying = false;
    actionPreviewEntity = NULL_ENTITY;
    actionPreviewSceneId = 0;
}

void editor::Properties::stopActionPreview(Scene* scene, SceneProject* sceneProject) {
    if (!actionPreviewing) return;

    // Update snapshots with current user-editable config values before restoring
    for (ActionPreviewState& state : actionPreviewStates) {
        ProjectUtils::updateActionPreviewSnapshot(state.entity, state.components, scene);
    }

    for (const ActionPreviewState& state : actionPreviewStates) {
        if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
            continue;
        }
        Stream::decodeComponents(state.entity, state.parent, scene, state.components);
    }

    // Remove InstancedMeshComponent dynamically added during preview (e.g. particle targets).
    for (const ActionPreviewState& state : actionPreviewStates) {
        ProjectUtils::removeDynamicInstmesh(state.entity, state.components, scene);
    }

    actionPreviewStates.clear();
    actionPreviewing = false;
    actionPreviewPlaying = false;
    actionPreviewEntity = NULL_ENTITY;
    actionPreviewSceneId = 0;
    if (sceneProject) {
        sceneProject->needUpdateRender = true;
    }
}

void editor::Properties::drawActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    ActionComponent* actionComp = nullptr;
    Entity entity = NULL_ENTITY;
    Scene* scene = sceneProject->scene;
    bool sceneIsStopped = (sceneProject->playState == ScenePlayState::STOPPED);

    if (entities.size() == 1) {
        entity = entities[0];
        actionComp = scene->findComponent<ActionComponent>(entity);

        if (actionPreviewing && (actionPreviewEntity != entity || actionPreviewSceneId != sceneProject->id)) {
            SceneProject* prevSceneProject = project->getScene(actionPreviewSceneId);
            if (prevSceneProject && prevSceneProject->scene) {
                stopActionPreview(prevSceneProject->scene, prevSceneProject);
            } else {
                actionPreviewStates.clear();
                actionPreviewing = false;
                actionPreviewPlaying = false;
            }
        }

        if (actionPreviewing && !sceneIsStopped) {
            stopActionPreview(scene, sceneProject);
        }

        if (actionPreviewing && actionPreviewPlaying && sceneIsStopped) {
            float dt = ImGui::GetIO().DeltaTime;
            scene->getSystem<ActionSystem>()->updateActionPreview(dt, entity);
            sceneProject->needUpdateRender = true;

            actionComp = scene->findComponent<ActionComponent>(entity);
            if (actionComp && actionComp->state != ActionState::Running) {
                actionPreviewPlaying = false;
            }
        }
    }

    RowSettings settingsRandom;
    settingsRandom.help = "Starts at a random time of its duration, so copies do not move in sync";

    beginTable(cpType, getLabelSize("Play on start"));

    // a running state when saved is what starts the action with the scene
    int state = (int)ActionState::Stopped;
    bool dif = false;
    for (size_t i = 0; i < entities.size(); i++){
        ActionComponent* comp = scene->findComponent<ActionComponent>(entities[i]);
        int entityState = comp ? (int)comp->state : (int)ActionState::Stopped;
        dif = dif || (i > 0 && entityState != state);
        state = entityState;
    }

    int newState = state;
    bool stateChanged = false;
    if (propertyHeader("Play on start", -1, state != (int)ActionState::Stopped, false)){
        newState = (int)ActionState::Stopped;
        stateChanged = true;
    }

    if (dif){
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }
    bool playOnStart = state != (int)ActionState::Stopped;
    if (ImGui::Checkbox("##checkbox_playonstart", &playOnStart)){
        newState = (int)(playOnStart ? ActionState::Running : ActionState::Stopped);
        stateChanged = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    int stateItem = 0;
    std::vector<const char*> stateNames;
    for (size_t i = 0; i < entriesActionState.size(); i++){
        stateNames.push_back(entriesActionState[i].name);
        if (entriesActionState[i].value == state){
            stateItem = (int)i;
        }
    }
    if (ImGui::Combo("##combo_state", &stateItem, stateNames.data(), (int)stateNames.size())){
        newState = entriesActionState[stateItem].value;
        stateChanged = true;
    }
    if (dif){
        ImGui::PopStyleColor(2);
    }

    if (stateChanged){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity e : entities){
            multiCmd->addPropertyCmd<int>(project, sceneProject->id, e, cpType, "state", newState);
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    propertyRow(RowPropertyType::Float, cpType, "speed", "Speed", sceneProject, entities);
    propertyRow(RowPropertyType::FloatPositive, cpType, "startOffset", "Start offset", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "randomStart", "Random start", sceneProject, entities, settingsRandom);
    propertyRow(RowPropertyType::LocalEntity, cpType, "target", "Target", sceneProject, entities);
    //propertyRow(RowPropertyType::Bool, cpType, "ownedTarget", "Owned target", sceneProject, entities);
    endTable();

    if (entities.size() != 1 || !actionComp) {
        return;
    }

    // Check if this entity also has AnimationComponent → sync with AnimationWindow
    AnimationWindow* animWindow = Backend::getApp().getAnimationWindow();
    bool hasAnimation = scene->findComponent<AnimationComponent>(entity) != nullptr;
    bool syncWithAnimWindow = hasAnimation && animWindow;

    ImGui::Separator();

    ImGui::BeginDisabled(!sceneIsStopped);

    if (syncWithAnimWindow) {
        // Delegate playback to AnimationWindow
        bool animIsPlaying = animWindow->getIsPlaying() && animWindow->isPreviewingEntity(entity, sceneProject->id);
        bool animIsPreviewing = animWindow->isPreviewingEntity(entity, sceneProject->id);

        if (animIsPlaying) {
            if (ImGui::Button(ICON_FA_PAUSE "##action_pause")) {
                animWindow->externalPause();
            }
        } else {
            if (ImGui::Button(ICON_FA_PLAY "##action_play")) {
                animWindow->externalPlay(entity, sceneProject->id);
            }
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(!animIsPlaying && !animIsPreviewing);
        if (ImGui::Button(ICON_FA_STOP "##action_stop")) {
            animWindow->externalStop();
        }
        ImGui::EndDisabled();
    } else {
        // Standalone action preview (no AnimationComponent)
        if (actionPreviewPlaying) {
            if (ImGui::Button(ICON_FA_PAUSE "##action_pause")) {
                if (actionComp && actionComp->state == ActionState::Running) {
                    Action(scene, entity).pause();
                }
            }
        } else {
            if (ImGui::Button(ICON_FA_PLAY "##action_play")) {
                if (!actionPreviewing) {
                    startActionPreview(entity, scene, sceneProject);
                    actionComp = scene->findComponent<ActionComponent>(entity);
                } else if (actionComp) {
                    if (actionComp->state == ActionState::Stopped) {
                        stopActionPreview(scene, sceneProject);
                        startActionPreview(entity, scene, sceneProject);
                        actionComp = scene->findComponent<ActionComponent>(entity);
                    } else {
                        Action(scene, entity).start();
                    }
                }
                actionPreviewPlaying = true;
            }
        }
        ImGui::SameLine();

        ImGui::BeginDisabled(!actionPreviewPlaying && !actionPreviewing);
        if (ImGui::Button(ICON_FA_STOP "##action_stop")) {
            if (actionPreviewing) {
                stopActionPreview(scene, sceneProject);
                actionComp = scene->findComponent<ActionComponent>(entity);
            }
            actionPreviewPlaying = false;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();

    // Time display (fixed width so it doesn't jump around)
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.2fs", actionComp->timecount);
    ImGui::SameLine();

    // Timeline progress strip
    float duration = scene->getSystem<ActionSystem>()->getDuration(entity);

    float fraction = (duration > 0) ? std::clamp(actionComp->timecount / duration, 0.0f, 1.0f) : 0.0f;

    float timelineWidth = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    float frameHeight = ImGui::GetFrameHeight();
    float timelineHeight = 6.0f;
    float rounding = timelineHeight * 0.5f;
    ImVec2 timelinePos = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##action_timeline_strip", ImVec2(timelineWidth, frameHeight));
    bool timelineHovered = ImGui::IsItemHovered();
    bool timelineClicked = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (sceneIsStopped && duration > 0 && timelineClicked) {
        float mouseX = ImGui::GetIO().MousePos.x;
        float clickFraction = std::clamp((mouseX - timelinePos.x) / timelineWidth, 0.0f, 1.0f);
        float seekTime = clickFraction * duration;

        if (syncWithAnimWindow) {
            animWindow->externalPlay(entity, sceneProject->id);
            animWindow->externalPause();
            animWindow->seekPreviewExternal(scene, sceneProject, seekTime);
            actionComp = scene->findComponent<ActionComponent>(entity);
        } else {
            if (!actionPreviewing) {
                startActionPreview(entity, scene, sceneProject);
                actionComp = scene->findComponent<ActionComponent>(entity);
            }
            if (actionPreviewing && actionComp) {
                // Restore state and re-evaluate at seek time
                for (const ActionPreviewState& state : actionPreviewStates) {
                    if (state.entity == NULL_ENTITY || !scene->isEntityCreated(state.entity) || !state.components || state.components.IsNull()) {
                        continue;
                    }
                    Stream::decodeComponents(state.entity, state.parent, scene, state.components);
                }

                actionComp = scene->findComponent<ActionComponent>(entity);
                if (actionComp) {
                    actionComp->state = ActionState::Stopped;
                    actionComp->timecount = 0;
                    actionComp->stopTrigger = false;
                    actionComp->pauseTrigger = false;
                    actionComp->startTrigger = true;
                }

                // Simulate to the seek time so particles and integrations match the state exactly
                float remainingTime = seekTime;
                float stepSize = 1.0f / 60.0f;
                while (remainingTime > 0.0f) {
                    float currentStep = std::min(stepSize, remainingTime);
                    scene->getSystem<ActionSystem>()->updateActionPreview(currentStep, entity);
                    remainingTime -= currentStep;
                }

                actionPreviewPlaying = false;

                actionComp = scene->findComponent<ActionComponent>(entity);
            }
        }
    }

    if (timelineHovered && duration > 0) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 trackColor = style.Colors[ImGuiCol_TextDisabled];
    trackColor.w = 0.22f;
    ImVec4 fillColor = style.Colors[ImGuiCol_PlotHistogram];

    ImVec2 barMin(timelinePos.x, timelinePos.y + (frameHeight - timelineHeight) * 0.5f);
    ImVec2 barMax(timelinePos.x + timelineWidth, barMin.y + timelineHeight);
    drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(trackColor), rounding);

    if (fraction > 0.0f) {
        ImVec2 fillMax(barMin.x + timelineWidth * fraction, barMax.y);
        drawList->AddRectFilled(barMin, fillMax, ImGui::GetColorU32(fillColor), rounding);
    }

    ImGui::EndDisabled();
}

void editor::Properties::drawTimedActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Ping-pong"));
    propertyRow(RowPropertyType::FloatPositive, cpType, "duration", "Duration", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "pingPong", "Ping-pong", sceneProject, entities);
    propertyRow(RowPropertyType::Ease, cpType, "function", "Ease", sceneProject, entities);
    endTable();
}

void editor::Properties::drawPositionActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start position"));

    // We compute a width that offsets the button
    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings vectorSettings;
    vectorSettings.secondColSize = reservedWidth;

    // Start Position Row
    propertyRow(RowPropertyType::Vector3, cpType, "startPosition", "Start position", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_pos")) {
            Vector3 pos = targetTransform->position;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "startPosition", pos);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current position from target");
        }
    }

    // End Position Row
    propertyRow(RowPropertyType::Vector3, cpType, "endPosition", "End position", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_pos")) {
            Vector3 pos = targetTransform->position;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "endPosition", pos);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current position from target");
        }
    }

    endTable();
}

void editor::Properties::drawRotationActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    bool spin = true;
    for (Entity entity : entities){
        RotationActionComponent* rotation = scene->findComponent<RotationActionComponent>(entity);
        spin = spin && rotation && rotation->spin;
    }

    RowSettings settingsAngle;
    settingsAngle.secondColSize = 6 * ImGui::GetFontSize();
    settingsAngle.format = "%.1f°";
    settingsAngle.help = "More than 360° turns more than once";

    beginTable(cpType, getLabelSize("Start rotation"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings quatSettings;
    quatSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Quat, cpType, "startRotation", "Start rotation", sceneProject, entities, quatSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_rot")) {
            Quaternion rot = targetTransform->rotation;
            auto* cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, "startRotation", rot);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current rotation from target");
        }
    }

    propertyRow(RowPropertyType::Bool, cpType, "spin", "Spin", sceneProject, entities);

    if (spin) {
        propertyRow(RowPropertyType::Vector3, cpType, "axis", "Axis", sceneProject, entities);
        propertyRow(RowPropertyType::Float, cpType, "angle", "Angle", sceneProject, entities, settingsAngle);
        endTable();
        return;
    }

    propertyRow(RowPropertyType::Quat, cpType, "endRotation", "End rotation", sceneProject, entities, quatSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_rot")) {
            Quaternion rot = targetTransform->rotation;
            auto* cmd = new PropertyCmd<Quaternion>(project, sceneProject->id, entity, cpType, "endRotation", rot);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current rotation from target");
        }
    }

    propertyRow(RowPropertyType::Bool, cpType, "shortestPath", "Shortest path", sceneProject, entities);
    endTable();
}

void editor::Properties::drawScaleActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start scale"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings vectorSettings;
    vectorSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Vector3, cpType, "startScale", "Start scale", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_scale")) {
            Vector3 scl = targetTransform->scale;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "startScale", scl);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current scale from target");
        }
    }

    propertyRow(RowPropertyType::Vector3, cpType, "endScale", "End scale", sceneProject, entities, vectorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;
        Transform* targetTransform = (target != NULL_ENTITY) ? scene->findComponent<Transform>(target) : nullptr;

        ImGui::SameLine();
        ImGui::BeginDisabled(!targetTransform);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_scale")) {
            Vector3 scl = targetTransform->scale;
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "endScale", scl);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current scale from target");
        }
    }

    endTable();
}

void editor::Properties::drawColorActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start color"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings colorSettings;
    colorSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Color3L, cpType, "startColor", "Start color", sceneProject, entities, colorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        Vector3 targetColor;
        bool hasColor = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetColor = Vector3(ui->color.x, ui->color.y, ui->color.z); hasColor = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetColor = Vector3(mesh->submeshes[0].material.baseColorFactor.x, mesh->submeshes[0].material.baseColorFactor.y, mesh->submeshes[0].material.baseColorFactor.z); hasColor = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasColor);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_color")) {
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "startColor", targetColor);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current color from target");
        }
    }

    propertyRow(RowPropertyType::Color3L, cpType, "endColor", "End color", sceneProject, entities, colorSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        Vector3 targetColor;
        bool hasColor = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetColor = Vector3(ui->color.x, ui->color.y, ui->color.z); hasColor = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetColor = Vector3(mesh->submeshes[0].material.baseColorFactor.x, mesh->submeshes[0].material.baseColorFactor.y, mesh->submeshes[0].material.baseColorFactor.z); hasColor = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasColor);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_color")) {
            auto* cmd = new PropertyCmd<Vector3>(project, sceneProject->id, entity, cpType, "endColor", targetColor);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current color from target");
        }
    }

    propertyRow(RowPropertyType::Bool, cpType, "useSRGB", "Use sRGB", sceneProject, entities);
    endTable();
}

void editor::Properties::drawAlphaActionComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    Scene* scene = sceneProject->scene;

    beginTable(cpType, getLabelSize("Start alpha"));

    float buttonSize = ImGui::GetFrameHeight();
    float reservedWidth = -(buttonSize + ImGui::GetStyle().ItemSpacing.x);
    RowSettings alphaSettings;
    alphaSettings.secondColSize = reservedWidth;

    propertyRow(RowPropertyType::Float, cpType, "startAlpha", "Start alpha", sceneProject, entities, alphaSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        float targetAlpha = 1.0f;
        bool hasAlpha = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetAlpha = ui->color.w; hasAlpha = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetAlpha = mesh->submeshes[0].material.baseColorFactor.w; hasAlpha = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasAlpha);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_start_alpha")) {
            auto* cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, "startAlpha", targetAlpha);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current alpha from target");
        }
    }

    propertyRow(RowPropertyType::Float, cpType, "endAlpha", "End alpha", sceneProject, entities, alphaSettings);

    if (entities.size() == 1) {
        Entity entity = entities[0];
        ActionComponent* actionComp = scene->findComponent<ActionComponent>(entity);
        Entity target = (actionComp && actionComp->target != NULL_ENTITY && scene->isEntityCreated(actionComp->target)) ? actionComp->target : NULL_ENTITY;

        float targetAlpha = 1.0f;
        bool hasAlpha = false;
        if (target != NULL_ENTITY) {
            UIComponent* ui = scene->findComponent<UIComponent>(target);
            if (ui) { targetAlpha = ui->color.w; hasAlpha = true; }
            else {
                MeshComponent* mesh = scene->findComponent<MeshComponent>(target);
                if (mesh) { targetAlpha = mesh->submeshes[0].material.baseColorFactor.w; hasAlpha = true; }
            }
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!hasAlpha);
        ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCATION_CROSSHAIRS);
        float padX = std::max(0.0f, (buttonSize - iconSize.x) / 2.0f);
        float padY = std::max(0.0f, (buttonSize - iconSize.y) / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, padY));
        if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##get_end_alpha")) {
            auto* cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, "endAlpha", targetAlpha);
            CommandHandle::get(sceneProject->id)->addCommand(cmd);
        }
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Get current alpha from target");
        }
    }

    endTable();
}

void editor::Properties::drawSpriteAnimationComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Interval index"));
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "frameIndex", "Frame index", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "frameTimeIndex", "Interval index", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "spriteFrameCount", "Frame count", sceneProject, entities);
    endTable();

    if (entities.size() != 1) {
        ImGui::SeparatorText("Animation Frames");
        ImGui::TextDisabled("Select a single entity to edit sprite animation frames");
        return;
    }

    Entity entity = entities[0];
    SpriteAnimationComponent& spriteAnim = sceneProject->scene->getComponent<SpriteAnimationComponent>(entity);

    Entity previewEntity = NULL_ENTITY;
    if (ActionComponent* actionComp = sceneProject->scene->findComponent<ActionComponent>(entity)) {
        previewEntity = actionComp->target;
    }

    SpriteComponent* spriteComp = nullptr;
    MeshComponent* meshComp = nullptr;
    if (previewEntity != NULL_ENTITY) {
        spriteComp = sceneProject->scene->findComponent<SpriteComponent>(previewEntity);
        meshComp = sceneProject->scene->findComponent<MeshComponent>(previewEntity);
    }

    Texture previewTexture;
    if (meshComp && meshComp->numSubmeshes > 0) {
        previewTexture = meshComp->submeshes[0].material.baseColorTexture;
    }

    unsigned int visibleFrameCount = std::max(spriteAnim.framesSize, spriteAnim.framesTimeSize);
    bool countsMatch = spriteAnim.framesSize == spriteAnim.framesTimeSize;
    bool canAddFrame = spriteAnim.frames.validIndex(visibleFrameCount) && spriteAnim.framesTime.validIndex(visibleFrameCount);

    ImGui::SeparatorText("Animation Frames");

    beginTable(cpType, getLabelSize("Animation Frames"), "sprite_animation_frames_header");
    propertyHeader("Animation Frames", -1, false, false);
    ImGui::Text("%u", visibleFrameCount);
    if (!countsMatch) {
        ImGui::SameLine();
        ImGui::TextDisabled("(frames/intervals mismatch)");
    }
    float spriteAnimArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton(ICON_FA_PLUS " Add Frame##sprite_animation_add", spriteAnimArrowWidth, canAddFrame,
            "Sprite animation has reached the maximum number of frames")) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (const Entity& selectedEntity : entities) {
            if (SpriteAnimationComponent* animComp = sceneProject->scene->findComponent<SpriteAnimationComponent>(selectedEntity)) {
                unsigned int nextIndex = std::max(animComp->framesSize, animComp->framesTimeSize);
                if (!animComp->frames.validIndex(nextIndex) || !animComp->framesTime.validIndex(nextIndex)) {
                    continue;
                }

                int defaultFrame = 0;
                if (animComp->framesSize > 0 && animComp->frames.validIndex(animComp->framesSize - 1)) {
                    defaultFrame = animComp->frames[animComp->framesSize - 1];
                }

                multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                    "frames[" + std::to_string(nextIndex) + "]", defaultFrame);
                multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                    "framesTime[" + std::to_string(nextIndex) + "]", 100);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                    "framesSize", nextIndex + 1);
                multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                    "framesTimeSize", nextIndex + 1);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_all_sprite_animation_frames", spriteAnimationFramesExpanded ? ImGuiDir_Up : ImGuiDir_Down)) {
        spriteAnimationFramesExpanded = !spriteAnimationFramesExpanded;
    }
    endTable();

    if (!spriteAnimationFramesExpanded || visibleFrameCount == 0) {
        return;
    }

    RowSettings settingsFrame;
    settingsFrame.stepSize = 1.0f;
    settingsFrame.format = "%.0f";
    settingsFrame.showColors = false;

    RowSettings settingsInterval = settingsFrame;

    for (unsigned int i = 0; i < visibleFrameCount; i++) {
        ImGui::PushID((int)i);

        std::string frameGroupStr = "sprite_animation_frame_" + std::to_string(i);
        std::string framePropId = "frames[" + std::to_string(i) + "]";
        std::string intervalPropId = "framesTime[" + std::to_string(i) + "]";

        bool hasFrameValue = i < spriteAnim.framesSize;
        bool hasIntervalValue = i < spriteAnim.framesTimeSize;
        int frameId = hasFrameValue ? spriteAnim.frames[i] : -1;

        bool hasSpriteFrame = spriteComp && frameId >= 0 && (unsigned int)frameId < spriteComp->numFramesRect;
        Rect frameRect;
        std::string spriteFrameName = "Invalid";
        if (hasSpriteFrame) {
            frameRect = spriteComp->framesRect[frameId].rect;
            if (!spriteComp->framesRect[frameId].name.empty()) {
                spriteFrameName = spriteComp->framesRect[frameId].name;
            } else {
                spriteFrameName = "Frame " + std::to_string(frameId);
            }
        }

        std::string frameLabel = "[" + std::to_string(i) + "] ";
        if (hasFrameValue) {
            frameLabel += spriteFrameName;
        } else {
            frameLabel += "Missing frame";
        }

        ImGui::SeparatorText(frameLabel.c_str());

        beginTable(cpType, getLabelSize("Interval (ms)"), frameGroupStr);
        propertyHeader("Frame", -1, false, false);

        float previewSize = ImGui::GetFrameHeight() * 2.2f;
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 deleteButtonSize = ImVec2(clearButtonWidth + clearButtonFramePadding * 2, 0);
        ImVec2 arrowButtonSize = ImGui::CalcItemSize(ImVec2(0, 0), ImGui::GetFrameHeight(), ImGui::GetFrameHeight());

        bool hasFramePreview = hasSpriteFrame
                && !previewTexture.empty()
                && frameRect.getWidth() > 0.0f
                && frameRect.getHeight() > 0.0f;
        float trailingWidth = deleteButtonSize.x + ImGui::GetStyle().ItemSpacing.x + arrowButtonSize.x;
        if (hasFramePreview) {
            trailingWidth += previewSize + ImGui::GetStyle().ItemSpacing.x;
        }

        float targetX = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - trailingWidth);
        ImGui::SetCursorPosX(targetX);

        if (hasFramePreview) {
            drawSpriteFramePreview(&previewTexture, frameRect, ImVec2(previewSize, previewSize), "##sprite_animation_frame_preview");
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("Sprite frame %d", frameId);
                if (spriteComp && !spriteComp->framesRect[frameId].name.empty()) {
                    ImGui::TextDisabled("%s", spriteComp->framesRect[frameId].name.c_str());
                }

                float tooltipMaxSize = Theme::dpi(200.0f);
                float scale = std::min(tooltipMaxSize / std::max(1.0f, frameRect.getWidth()),
                                       tooltipMaxSize / std::max(1.0f, frameRect.getHeight()));
                scale = std::min(scale, 1.0f);
                ImVec2 tooltipSize(std::max(Theme::dpi(32.0f), frameRect.getWidth() * scale),
                                   std::max(Theme::dpi(32.0f), frameRect.getHeight() * scale));

                drawSpriteFramePreview(&previewTexture, frameRect, tooltipSize, "##sprite_animation_frame_preview_tooltip");
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
        }

        if (ImGui::ArrowButton("##toggle_sprite_animation_frame", spriteAnimationFramesButtonGroups[frameGroupStr] ? ImGuiDir_Up : ImGuiDir_Down)) {
            spriteAnimationFramesButtonGroups[frameGroupStr] = !spriteAnimationFramesButtonGroups[frameGroupStr];
        }
        ImGui::SameLine();

        bool deleted = false;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(ICON_FA_TRASH_CAN "##delete_sprite_animation_frame", deleteButtonSize)) {
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (const Entity& selectedEntity : entities) {
                if (SpriteAnimationComponent* animComp = sceneProject->scene->findComponent<SpriteAnimationComponent>(selectedEntity)) {
                    if (i < animComp->framesSize) {
                        for (unsigned int j = i; j + 1 < animComp->framesSize; j++) {
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                                "frames[" + std::to_string(j) + "]", animComp->frames[j + 1]);
                        }
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                            "frames[" + std::to_string(animComp->framesSize - 1) + "]", 0);
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                            "framesSize", animComp->framesSize - 1);
                    }

                    if (i < animComp->framesTimeSize) {
                        for (unsigned int j = i; j + 1 < animComp->framesTimeSize; j++) {
                            multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                                "framesTime[" + std::to_string(j) + "]", animComp->framesTime[j + 1]);
                        }
                        multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                            "framesTime[" + std::to_string(animComp->framesTimeSize - 1) + "]", 0);
                        multiCmd->addPropertyCmd<unsigned int>(project, sceneProject->id, selectedEntity, cpType,
                            "framesTimeSize", animComp->framesTimeSize - 1);
                    }

                    int nextFrameIndex = 0;
                    if (animComp->framesSize > 1) {
                        nextFrameIndex = std::min(animComp->frameIndex, (int)animComp->framesSize - 2);
                    }
                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                        "frameIndex", std::max(0, nextFrameIndex));

                    int nextFrameTimeIndex = 0;
                    if (animComp->framesTimeSize > 1) {
                        nextFrameTimeIndex = std::min(animComp->frameTimeIndex, (int)animComp->framesTimeSize - 2);
                    }
                    multiCmd->addPropertyCmd<int>(project, sceneProject->id, selectedEntity, cpType,
                        "frameTimeIndex", std::max(0, nextFrameTimeIndex));
                }
            }
            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            deleted = true;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);

        if (!deleted && spriteAnimationFramesButtonGroups[frameGroupStr]) {
            propertyRow(RowPropertyType::Int, cpType, framePropId, "Sprite Frame", sceneProject, entities, settingsFrame);
            propertyRow(RowPropertyType::Int, cpType, intervalPropId, "Interval (ms)", sceneProject, entities, settingsInterval);

            if (previewEntity == NULL_ENTITY) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Assign Action target to preview animation frames");
            } else if (!spriteComp) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Target entity needs SpriteComponent to preview animation frames");
            } else if (!meshComp || meshComp->numSubmeshes == 0 || previewTexture.empty()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Target entity needs MeshComponent with a valid texture for previews");
            } else if (!hasFrameValue) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Missing frame entry for this slot");
            } else if (frameId < 0 || (unsigned int)frameId >= spriteComp->numFramesRect) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Preview");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Frame %d is outside SpriteComponent frame range", frameId);
            } else if (!hasIntervalValue) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("Interval");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("Missing interval entry for this slot");
            }
        }

        endTable();
        ImGui::PopID();
    }
}

void editor::Properties::drawAnimationComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Owned actions"));
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "duration", "Duration", sceneProject, entities);
    propertyRow(RowPropertyType::Bool, cpType, "ownedActions", "Owned actions", sceneProject, entities);
    propertyRow(RowPropertyType::FloatPositive, cpType, "defaultFadeTime", "Fade time", sceneProject, entities);
    endTable();

    AnimationComponent& anim = sceneProject->scene->getComponent<AnimationComponent>(entities[0]);

    ImGui::SeparatorText("Actions");

    beginTable(cpType, getLabelSize("Actions"), "animation_actions_header");
    propertyHeader("Actions", -1, false, false);
    ImGui::Text("%zu", anim.actions.size());
    if (drawSummaryAddButton("Add Action##animation_actions_add")) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities) {
            if (AnimationComponent* animComp = sceneProject->scene->findComponent<AnimationComponent>(entity)) {
                std::vector<ActionFrame> newActions = animComp->actions;
                // Duration 0 = auto: once an action is assigned, the frame follows
                // the action's own duration.
                ActionFrame newFrame = {0.0f, 0.0f, NULL_ENTITY, 0};

                ActionSystem* actionSystem = sceneProject->scene->getSystem<ActionSystem>().get();
                bool overlap;
                do {
                    overlap = false;
                    for (const auto& a : newActions) {
                        if (a.track == newFrame.track) {
                            float startA = a.startTime;
                            float endA = startA + std::max(actionSystem->getFrameDuration(a), 0.01f);
                            float startB = newFrame.startTime;
                            float endB = startB + std::max(actionSystem->getFrameDuration(newFrame), 0.01f);
                            if (std::max(startA, startB) < std::min(endA, endB)) {
                                overlap = true;
                                newFrame.track++;
                                break;
                            }
                        }
                    }
                } while (overlap);

                newActions.push_back(newFrame);
                multiCmd->addPropertyCmd<std::vector<ActionFrame>>(project, sceneProject->id, entity, cpType, "actions", newActions, nullptr);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }
    endTable();

    if (!anim.actions.empty()) {
        // Entities that cannot be assigned as actions: the edited animations themselves
        // and any animation whose action tree already reaches one (would create a cycle)
        std::vector<Entity> cycleExclusions;
        {
            ActionSystem* actionSystem = sceneProject->scene->getSystem<ActionSystem>().get();
            auto animArray = sceneProject->scene->getComponentArray<AnimationComponent>();
            for (int a = 0; a < (int)animArray->size(); a++){
                Entity candidate = animArray->getEntity(a);
                for (Entity edited : entities){
                    if (actionSystem->isAnimationReachable(candidate, edited)){
                        cycleExclusions.push_back(candidate);
                        break;
                    }
                }
            }
        }

        beginTable(cpType, getLabelSize("Start Time"), "animation_actions_table");
        for (size_t i = 0; i < anim.actions.size(); i++) {
            ImGui::PushID((int)i);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Action %zu", i);
            ImGui::TableSetColumnIndex(1);

            if (ImGui::Button(ICON_FA_TRASH_CAN "##remove_action")) {
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities) {
                    if (AnimationComponent* animComp = sceneProject->scene->findComponent<AnimationComponent>(entity)) {
                        if (i < animComp->actions.size()) {
                            std::vector<ActionFrame> newActions = animComp->actions;
                            newActions.erase(newActions.begin() + i);
                            multiCmd->addPropertyCmd<std::vector<ActionFrame>>(project, sceneProject->id, entity, cpType, "actions", newActions, nullptr);
                        }
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                ImGui::PopID();
                break; // break early to reset next frame cleanly
            }

            std::string prefix = "actions[" + std::to_string(i) + "]";
            propertyRow(RowPropertyType::UInt, cpType, prefix + ".track", "Track", sceneProject, entities);
            RowSettings settingsStartTime;
            settingsStartTime.stepSize = 0.01f;
            propertyRow(RowPropertyType::Float, cpType, prefix + ".startTime", "Start Time", sceneProject, entities, settingsStartTime);

            // Duration row: "Duration | [auto checkbox] [drag or resolved label]".
            // Checked = auto (duration 0): the frame follows the action's own duration.
            ActionSystem* actionSystem = sceneProject->scene->getSystem<ActionSystem>().get();
            std::string durationId = prefix + ".duration";
            bool isAuto = (anim.actions[i].duration <= 0);

            float* durValue = nullptr;
            std::map<Entity, float> eDurValue;
            bool durDif = false;
            float* durDef = nullptr;
            for (Entity& entity : entities){
                PropertyData prop = Catalog::findProperty(sceneProject->scene, entity, cpType, durationId);
                // frames are counted from the first entity, the others may have fewer
                if (!prop.ref){
                    continue;
                }
                durDef = static_cast<float*>(prop.def);
                eDurValue[entity] = *static_cast<float*>(prop.ref);
                if (durValue && *durValue != eDurValue[entity])
                    durDif = true;
                durValue = &eDurValue[entity];
            }
            float newDurValue = *durValue;
            bool durDefChanged = durDef ? (newDurValue != *durDef) : false;

            if (propertyHeader("Duration", -1, durDefChanged, false)){
                for (Entity& entity : entities){
                    cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, durationId, *durDef,
                        [sceneProject]() { sceneProject->isModified = true; });
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    finishProperty = true;
                }
            }

            bool autoChecked = isAuto;
            if (ImGui::Checkbox("##auto_duration", &autoChecked)) {
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities) {
                    if (AnimationComponent* animComp = sceneProject->scene->findComponent<AnimationComponent>(entity)) {
                        if (i < animComp->actions.size()) {
                            float newDuration = 0.0f;
                            if (!autoChecked) {
                                // Freeze the currently resolved duration as the explicit value
                                newDuration = actionSystem->getFrameDuration(animComp->actions[i]);
                                if (newDuration <= 0) newDuration = 1.0f;
                            }
                            multiCmd->addPropertyCmd<float>(project, sceneProject->id, entity, cpType, durationId, newDuration,
                                [sceneProject]() { sceneProject->isModified = true; });
                        }
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Auto: follow the action's own duration");
            }
            ImGui::SameLine();

            if (isAuto) {
                float resolved = actionSystem->getFrameDuration(anim.actions[i]);
                ImGui::TextDisabled("%.2fs (auto)", resolved);
            } else {
                ImGui::SetNextItemWidth(-1);
                if (durDif)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                if (ImGui::DragFloat("##input_duration", &newDurValue, 0.01f, 0.0f, 0.0f, "%.2f")){
                    for (Entity& entity : entities){
                        cmd = new PropertyCmd<float>(project, sceneProject->id, entity, cpType, durationId, newDurValue,
                            [sceneProject]() { sceneProject->isModified = true; });
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                }
                if (durDif)
                    ImGui::PopStyleColor();
            }

            // Close command merging like propertyRow does
            if (ImGui::IsItemDeactivatedAfterEdit() || finishProperty) {
                if (cmd){
                    cmd->setNoMerge();
                    cmd = nullptr;
                }
                finishProperty = false;
            }
            RowSettings settingsAction;
            settingsAction.entityFilter.set(sceneProject->scene->getComponentId<ActionComponent>());
            settingsAction.excludeEntities = cycleExclusions;
            propertyRow(RowPropertyType::LocalEntity, cpType, prefix + ".action", "Action", sceneProject, entities, settingsAction);

            if (i < anim.actions.size() - 1) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Separator();
                ImGui::TableSetColumnIndex(1);
                ImGui::Separator();
            }

            ImGui::PopID();
        }
        endTable();
    }
}

void editor::Properties::drawBundleComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    beginTable(cpType, getLabelSize("Path"));
    propertyRow(RowPropertyType::Label, cpType, "name", "Name", sceneProject, entities);
    propertyRow(RowPropertyType::Label, cpType, "path", "Path", sceneProject, entities);
    endTable();
}

void editor::Properties::drawBoneComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    RowSettings settingsModel;
    settingsModel.entityFilter.set(sceneProject->scene->getComponentId<ModelComponent>());
    beginTable(cpType, getLabelSize("Bind Rotation"));
    propertyRow(RowPropertyType::LocalEntity, cpType, "model", "Model", sceneProject, entities, settingsModel);
    propertyRow(RowPropertyType::Int, cpType, "index", "Index", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "bindPosition", "Bind Position", sceneProject, entities);
    propertyRow(RowPropertyType::Quat, cpType, "bindRotation", "Bind Rotation", sceneProject, entities);
    propertyRow(RowPropertyType::Vector3, cpType, "bindScale", "Bind Scale", sceneProject, entities);
    endTable();
}

void editor::Properties::drawSkinComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    // the model fills the palette, nothing here is edited
    beginTable(cpType, getLabelSize("Bones"));
    propertyRow(RowPropertyType::Label, cpType, "numBones", "Bones", sceneProject, entities);
    endTable();
}

void editor::Properties::drawKeyframeTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    KeyframeTracksComponent& comp = sceneProject->scene->getComponent<KeyframeTracksComponent>(entities[0]);

    // relative only applies to the transform tracks
    bool transformTracks = true;
    for (Entity entity : entities){
        Signature signature = sceneProject->scene->getSignature(entity);
        transformTracks = transformTracks && (signature.test(sceneProject->scene->getComponentId<TranslateTracksComponent>()) ||
            signature.test(sceneProject->scene->getComponentId<RotateTracksComponent>()) ||
            signature.test(sceneProject->scene->getComponentId<ScaleTracksComponent>()));
    }

    RowSettings settingsRelative;
    settingsRelative.help = "Keys are offsets from the pose the target has when the track starts";

    beginTable(cpType, getLabelSize("Interpolation"));
    propertyRow(RowPropertyType::Bool, cpType, "loop", "Loop", sceneProject, entities);
    if (transformTracks){
        propertyRow(RowPropertyType::Bool, cpType, "relative", "Relative", sceneProject, entities, settingsRelative);
    }
    propertyRow(RowPropertyType::Int, cpType, "index", "Index", sceneProject, entities);
    propertyRow(RowPropertyType::Float, cpType, "interpolation", "Interpolation", sceneProject, entities);
    endTable();

    // Keep easings aligned when a key is removed: removing key k removes one
    // segment (a middle removal merges its neighbors, keeping the leading ease)
    auto reconcileEasings = [this, sceneProject, cpType](MultiPropertyCmd* multiCmd, Entity entity, KeyframeTracksComponent* trackComp, size_t removedIndex){
        size_t oldSegments = trackComp->times.size() > 1 ? trackComp->times.size() - 1 : 0;
        if (oldSegments == 0 || trackComp->easings.empty()){
            return;
        }

        std::vector<EaseType> newEasings = trackComp->easings;
        size_t segRemove = std::min(removedIndex, oldSegments - 1);
        if (segRemove < newEasings.size()){
            newEasings.erase(newEasings.begin() + (long int)segRemove);
        }
        size_t newSegments = oldSegments - 1;
        if (newEasings.size() > newSegments){
            newEasings.resize(newSegments);
        }
        if (newEasings != trackComp->easings){
            multiCmd->addPropertyCmd<std::vector<EaseType>>(project, sceneProject->id, entity, cpType, "easings", newEasings);
        }
    };

    drawTrackValues<KeyframeTracksComponent, float>(cpType, sceneProject, entities, RowPropertyType::Float, 0.0f, "keyframe", &KeyframeTracksComponent::times, "times", reconcileEasings);

    // Per-segment easing: easing i shapes the interpolation from key i to key i+1.
    // Missing entries mean linear, so the list cannot grow past the segment count.
    beginTable(cpType, getLabelSize("Ease 00 - 00"), "keyframe_easings_table");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Easing");
    ImGui::TableSetColumnIndex(1);

    size_t easingCount = comp.easings.size();
    size_t maxEasingCount = easingCount;
    bool easingCountsDiffer = false;
    bool canAddEasing = false;
    bool anyTrackHasSegments = false;
    for (Entity entity : entities){
        if (KeyframeTracksComponent* trackComp = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity)){
            size_t segments = trackComp->times.size() > 1 ? trackComp->times.size() - 1 : 0;
            easingCountsDiffer = easingCountsDiffer || trackComp->easings.size() != easingCount;
            maxEasingCount = std::max(maxEasingCount, trackComp->easings.size());
            canAddEasing = canAddEasing || trackComp->easings.size() < segments;
            anyTrackHasSegments = anyTrackHasSegments || segments > 0;
        }
    }

    if (easingCountsDiffer){
        ImGui::TextUnformatted("---");
    }else{
        ImGui::Text("%zu", easingCount);
    }
    float easingArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    const char* addEasingTooltip = !anyTrackHasSegments
        ? "Add at least two keyframe values before adding easing"
        : "Easing already covers every keyframe segment";
    if (drawSummaryAddButton("Add Ease##keyframe_easings_add", easingArrowWidth, canAddEasing, addEasingTooltip)){
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities){
            if (KeyframeTracksComponent* trackComp = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity)){
                size_t segments = trackComp->times.size() > 1 ? trackComp->times.size() - 1 : 0;
                if (trackComp->easings.size() >= segments){
                    continue;
                }

                std::vector<EaseType> newEasings = trackComp->easings;
                EaseType newEasing = newEasings.empty() ? EaseType::LINEAR : newEasings.back();
                newEasings.push_back(newEasing);
                multiCmd->addPropertyCmd<std::vector<EaseType>>(project, sceneProject->id, entity, cpType, "easings", newEasings);
            }
        }
        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_keyframe_easings", keyframeEasingsExpanded ? ImGuiDir_Up : ImGuiDir_Down)){
        keyframeEasingsExpanded = !keyframeEasingsExpanded;
    }

    if (keyframeEasingsExpanded){
        // CUSTOM is omitted here: per-segment easings store only the EaseType.
        std::vector<const char*> easeNames;
        std::vector<EaseType> easeValues;
        for (const auto& entry : entriesEaseType){
            if (entry.value == (int)EaseType::CUSTOM){
                continue;
            }
            easeNames.push_back(entry.name);
            easeValues.push_back((EaseType)entry.value);
        }

        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
        ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
        if (inputSize.x < 100.0f){
            inputSize.x = 100.0f;
        }

        for (size_t i = 0; i < maxEasingCount; i++){
            ImGui::PushID((int)i);

            EaseType current = EaseType::LINEAR;
            bool hasCurrent = false;
            bool easingValuesDiffer = false;
            bool defChanged = false;
            for (Entity entity : entities){
                if (KeyframeTracksComponent* trackComp = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity)){
                    size_t segments = trackComp->times.size() > 1 ? trackComp->times.size() - 1 : 0;
                    if (i >= segments){
                        continue;
                    }

                    EaseType entityEasing = i < trackComp->easings.size() ? trackComp->easings[i] : EaseType::LINEAR;
                    if (!hasCurrent){
                        current = entityEasing;
                        hasCurrent = true;
                    }else if (current != entityEasing){
                        easingValuesDiffer = true;
                    }
                    defChanged = defChanged || entityEasing != EaseType::LINEAR;
                }
            }

            auto applyEasing = [&](EaseType newType){
                MultiPropertyCmd* multiCmd = nullptr;
                for (Entity entity : entities){
                    if (KeyframeTracksComponent* trackComp = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity)){
                        size_t segments = trackComp->times.size() > 1 ? trackComp->times.size() - 1 : 0;
                        if (i >= segments){
                            continue;
                        }
                        std::vector<EaseType> newEasings = trackComp->easings;
                        newEasings.resize(std::max(newEasings.size(), i + 1), EaseType::LINEAR);
                        newEasings[i] = newType;
                        if (!multiCmd){
                            multiCmd = new MultiPropertyCmd();
                        }
                        multiCmd->addPropertyCmd<std::vector<EaseType>>(project, sceneProject->id, entity, cpType, "easings", newEasings);
                    }
                }
                if (!multiCmd){
                    return;
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
            };

            std::string label = "Ease " + std::to_string(i) + " - " + std::to_string(i + 1);
            if (propertyHeader(label, inputSize.x, defChanged, false)){
                applyEasing(EaseType::LINEAR);
            }

            int itemCurrent = 0;
            for (size_t n = 0; n < easeValues.size(); n++){
                if (easeValues[n] == current){
                    itemCurrent = (int)n;
                    break;
                }
            }

            if (!hasCurrent){
                ImGui::BeginDisabled();
            }
            if (easingValuesDiffer){
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            bool easingChanged = ImGui::Combo("##keyframe_ease", &itemCurrent, easeNames.data(), (int)easeNames.size());
            if (easingValuesDiffer){
                ImGui::PopStyleColor();
            }
            if (!hasCurrent){
                ImGui::EndDisabled();
            }
            if (easingChanged){
                applyEasing(easeValues[itemCurrent]);
            }

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            if (ImGui::Button(ICON_FA_TRASH_CAN "##remove_keyframe_ease")){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (KeyframeTracksComponent* trackComp = sceneProject->scene->findComponent<KeyframeTracksComponent>(entity)){
                        if (i >= trackComp->easings.size()){
                            continue;
                        }
                        std::vector<EaseType> newEasings = trackComp->easings;
                        newEasings.erase(newEasings.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<EaseType>>(project, sceneProject->id, entity, cpType, "easings", newEasings);
                    }
                }
                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                ImGui::PopID();
                break;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()){
                ImGui::SetTooltip("Remove easing");
            }

            ImGui::PopID();
        }
    }

    endTable();
}

template<typename Component, typename ValueType>
void editor::Properties::drawTrackValues(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities, RowPropertyType rowType, const ValueType& defaultNewValue, const char* idPrefix, std::vector<ValueType> Component::*memberPtr, const char* propertyName,
                                         const std::function<void(MultiPropertyCmd*, Entity, Component*, size_t)>& onRemoveValue,
                                         const std::function<void(MultiPropertyCmd*, Entity, Component*)>& onAddValue){
    Component& comp = sceneProject->scene->getComponent<Component>(entities[0]);

    float firstColSize = getLabelSize("Value 000");
    beginTable(cpType, firstColSize, std::string(idPrefix) + "_values_header");

    RowSettings settingsValue;
    settingsValue.secondColSize = -1;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Values");
    ImGui::TableSetColumnIndex(1);

    ImGui::Text("%zu", (comp.*memberPtr).size());
    float trackValuesArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    std::string addValueLabel = std::string("Add Value##add_") + idPrefix + "_values";
    if (drawSummaryAddButton(addValueLabel, trackValuesArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities){
            if (Component* trackComp = sceneProject->scene->findComponent<Component>(entity)){
                std::vector<ValueType> newValues = trackComp->*memberPtr;
                ValueType newValue = defaultNewValue;
                if (!newValues.empty()){
                    newValue = newValues.back();
                }
                newValues.push_back(newValue);
                multiCmd->addPropertyCmd<std::vector<ValueType>>(project, sceneProject->id, entity, cpType, propertyName, newValues);
                if (onAddValue){
                    onAddValue(multiCmd, entity, trackComp);
                }
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    ImGui::SameLine();
    std::string arrowId = std::string("##toggle_") + idPrefix + "_values";
    if (ImGui::ArrowButton(arrowId.c_str(), trackValuesExpanded[idPrefix] ? ImGuiDir_Up : ImGuiDir_Down)){
        trackValuesExpanded[idPrefix] = !trackValuesExpanded[idPrefix];
    }

    bool removedValue = false;

    if (trackValuesExpanded[idPrefix]){
    float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
    float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;
    ImVec2 inputSize = ImVec2(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x - clearButtonFramePadding * 2, 0);
    if (inputSize.x < 100.0f){
        inputSize.x = 100.0f;
    }

    for (size_t i = 0; i < (comp.*memberPtr).size(); i++){
        std::string valueId = std::string(propertyName) + "[" + std::to_string(i) + "]";
        std::string valueLabel = "Value " + std::to_string(i);

        RowSettings settingsEntry = settingsValue;
        settingsEntry.secondColSize = inputSize.x;
        propertyRow(rowType, cpType, valueId, valueLabel, sceneProject, entities, settingsEntry);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
        std::string removeId = std::string(ICON_FA_TRASH_CAN) + "##remove_" + idPrefix + "_value_" + std::to_string(i);
        if (ImGui::Button(removeId.c_str())){
            MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
            for (Entity entity : entities){
                if (Component* trackComp = sceneProject->scene->findComponent<Component>(entity)){
                    if (i < (trackComp->*memberPtr).size()){
                        std::vector<ValueType> newValues = trackComp->*memberPtr;
                        newValues.erase(newValues.begin() + (long int)i);
                        multiCmd->addPropertyCmd<std::vector<ValueType>>(project, sceneProject->id, entity, cpType, propertyName, newValues);
                        if (onRemoveValue){
                            onRemoveValue(multiCmd, entity, trackComp, i);
                        }
                    }
                }
            }

            multiCmd->setNoMerge();
            CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

            removedValue = true;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            break;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove value");
        }
    }

    } // trackValuesExpanded

    if (removedValue){
        endTable();
        return;
    }

    endTable();
}

// Keep Hermite tangents (GLTF CUBICSPLINE) mirrored with values across key
// add/remove so cubic playback survives editing. A new key gets zero tangents:
// its two adjacent segments flatten toward it, the rest of the clip keeps its
// imported shape. If one tangent array is missing or short (corrupt state) it
// is zero-padded back to a valid pair, matching writeTrackKey's repair policy.
template<typename Component, typename ValueType>
static void addTangentAlignCmds(editor::Project* project, uint32_t sceneId, editor::ComponentType cpType,
                                Entity entity, Component* trackComp, editor::MultiPropertyCmd* multiCmd,
                                const ValueType& zeroTangent, long removedIndex /* -1 appends */){
    if (trackComp->inTangents.empty() && trackComp->outTangents.empty()) return;

    auto align = [&](const std::vector<ValueType>& tangents, const char* property){
        std::vector<ValueType> newTangents = tangents;
        if (newTangents.size() < trackComp->values.size()){
            newTangents.resize(trackComp->values.size(), zeroTangent);
        }
        if (removedIndex < 0){
            newTangents.push_back(zeroTangent);
        }else if ((size_t)removedIndex < newTangents.size()){
            newTangents.erase(newTangents.begin() + removedIndex);
        }
        if (newTangents != tangents){
            multiCmd->addPropertyCmd<std::vector<ValueType>>(project, sceneId, entity, cpType, property, newTangents);
        }
    };
    align(trackComp->inTangents, "inTangents");
    align(trackComp->outTangents, "outTangents");
}

// drawTrackValues companion callbacks that keep a track's tangents aligned on
// key add/remove
template<typename Component, typename ValueType>
static auto tangentCallbacks(editor::Project* project, editor::SceneProject* sceneProject, editor::ComponentType cpType, const ValueType& zeroTangent){
    auto onRemove = [project, sceneProject, cpType, zeroTangent](editor::MultiPropertyCmd* multiCmd, Entity entity, Component* comp, size_t i){
        addTangentAlignCmds<Component, ValueType>(project, sceneProject->id, cpType, entity, comp, multiCmd, zeroTangent, (long)i);
    };
    auto onAdd = [project, sceneProject, cpType, zeroTangent](editor::MultiPropertyCmd* multiCmd, Entity entity, Component* comp){
        addTangentAlignCmds<Component, ValueType>(project, sceneProject->id, cpType, entity, comp, multiCmd, zeroTangent, -1);
    };
    return std::make_pair(onRemove, onAdd);
}

void editor::Properties::drawTranslateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    auto [onRemove, onAdd] = tangentCallbacks<TranslateTracksComponent>(project, sceneProject, cpType, Vector3::ZERO);
    drawTrackValues<TranslateTracksComponent, Vector3>(cpType, sceneProject, entities, RowPropertyType::Vector3, Vector3::ZERO, "translate", &TranslateTracksComponent::values, "values", onRemove, onAdd);
}

void editor::Properties::drawRotateTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    auto [onRemove, onAdd] = tangentCallbacks<RotateTracksComponent>(project, sceneProject, cpType, Quaternion(0.0f, 0.0f, 0.0f, 0.0f));
    drawTrackValues<RotateTracksComponent, Quaternion>(cpType, sceneProject, entities, RowPropertyType::Quat, Quaternion::IDENTITY, "rotate", &RotateTracksComponent::values, "values", onRemove, onAdd);
}

void editor::Properties::drawScaleTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    auto [onRemove, onAdd] = tangentCallbacks<ScaleTracksComponent>(project, sceneProject, cpType, Vector3::ZERO);
    drawTrackValues<ScaleTracksComponent, Vector3>(cpType, sceneProject, entities, RowPropertyType::Vector3, Vector3(1.0f, 1.0f, 1.0f), "scale", &ScaleTracksComponent::values, "values", onRemove, onAdd);
}

void editor::Properties::drawMorphTracksComponent(ComponentType cpType, SceneProject* sceneProject, std::vector<Entity> entities){
    MorphTracksComponent& comp = sceneProject->scene->getComponent<MorphTracksComponent>(entities[0]);

    float firstColSize = getLabelSize("Value 000");
    beginTable(cpType, firstColSize, "morph_values_header");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted("Values");
    ImGui::TableSetColumnIndex(1);

    ImGui::Text("%zu", comp.values.size());
    float morphValuesArrowWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    if (drawSummaryAddButton("Add Value##morph_values_add", morphValuesArrowWidth)) {
        MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
        for (Entity entity : entities){
            if (MorphTracksComponent* trackComp = sceneProject->scene->findComponent<MorphTracksComponent>(entity)){
                auto newValues = trackComp->values;
                std::vector<float> newValue;
                if (!newValues.empty()){
                    newValue = newValues.back();
                }
                newValues.push_back(newValue);
                multiCmd->addPropertyCmd<std::vector<std::vector<float>>>(project, sceneProject->id, entity, cpType, "values", newValues);
                std::vector<float> zeroTangent(newValue.size(), 0.0f);
                addTangentAlignCmds<MorphTracksComponent, std::vector<float>>(project, sceneProject->id, cpType, entity, trackComp, multiCmd, zeroTangent, -1);
            }
        }

        multiCmd->setNoMerge();
        CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
    }

    ImGui::SameLine();
    if (ImGui::ArrowButton("##toggle_morph_values", trackValuesExpanded["morph"] ? ImGuiDir_Up : ImGuiDir_Down)){
        trackValuesExpanded["morph"] = !trackValuesExpanded["morph"];
    }

    bool removedValue = false;

    if (trackValuesExpanded["morph"]){
        float clearButtonFramePadding = ImGui::GetStyle().FramePadding.x / 4.0f;
        float clearButtonWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x;

        for (size_t i = 0; i < comp.values.size(); i++){
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("Value %zu", i);
            ImGui::TableSetColumnIndex(1);

            ImGui::Text("%zu weights", comp.values[i].size());

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(clearButtonFramePadding, ImGui::GetStyle().FramePadding.y));
            std::string removeId = std::string(ICON_FA_TRASH_CAN) + "##remove_morph_value_" + std::to_string(i);
            if (ImGui::Button(removeId.c_str())){
                MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                for (Entity entity : entities){
                    if (MorphTracksComponent* trackComp = sceneProject->scene->findComponent<MorphTracksComponent>(entity)){
                        if (i < trackComp->values.size()){
                            auto newValues = trackComp->values;
                            newValues.erase(newValues.begin() + (long int)i);
                            multiCmd->addPropertyCmd<std::vector<std::vector<float>>>(project, sceneProject->id, entity, cpType, "values", newValues);
                            std::vector<float> zeroTangent(trackComp->values.back().size(), 0.0f);
                            addTangentAlignCmds<MorphTracksComponent, std::vector<float>>(project, sceneProject->id, cpType, entity, trackComp, multiCmd, zeroTangent, (long)i);
                        }
                    }
                }

                multiCmd->setNoMerge();
                CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);

                removedValue = true;
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
                break;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Remove value");
            }

            for (size_t j = 0; j < comp.values[i].size(); j++){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("  Weight %zu", j);
                ImGui::TableSetColumnIndex(1);

                float val = comp.values[i][j];
                std::string weightId = "##morph_w_" + std::to_string(i) + "_" + std::to_string(j);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat(weightId.c_str(), &val, 0.01f, 0.0f, 0.0f, "%.4f")){
                    MultiPropertyCmd* multiCmd = new MultiPropertyCmd();
                    for (Entity entity : entities){
                        if (MorphTracksComponent* trackComp = sceneProject->scene->findComponent<MorphTracksComponent>(entity)){
                            if (i < trackComp->values.size() && j < trackComp->values[i].size()){
                                auto newValues = trackComp->values;
                                newValues[i][j] = val;
                                multiCmd->addPropertyCmd<std::vector<std::vector<float>>>(project, sceneProject->id, entity, cpType, "values", newValues);
                            }
                        }
                    }
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                }
            }
        }
    }

    if (removedValue){
        endTable();
        return;
    }

    endTable();
}

void editor::Properties::show(){
    // Flush any debounced material file writes
    flushDirtyMaterials(ImGui::GetIO().DeltaTime);

    if (!windowOpen) {
        return;
    }

    if (focusRequested) {
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    bool wasOpen = windowOpen;

    if (!ImGui::Begin(Properties::WINDOW_NAME, &windowOpen)) {
        ImGui::End();
        if (wasOpen && !windowOpen) {
            setOpen(false);
        }
        return;
    }
    if (nameFocusFrames > 0) nameFocusFrames--;

    uint32_t propertiesSceneId = project->getSelectedSceneForProperties();
    SceneProject* sceneProject = project->getScene(propertiesSceneId);
    if (!sceneProject) {
        sceneProject = project->getSelectedScene();
    }
    if (!sceneProject) {
        stopTransientPreviews();
        ImGui::End();
        return;
    }

    std::vector<Entity> entities = project->getSelectedEntities(sceneProject->id);

    const bool previewSelectionMatches = actionPreviewing
            && sceneProject->id == actionPreviewSceneId
            && sceneProject->playState == ScenePlayState::STOPPED
            && entities.size() == 1
            && entities[0] == actionPreviewEntity;
    if (actionPreviewing && !previewSelectionMatches) {
        stopActionPreviewIfActive();
    }

    std::vector<ComponentType> components;
    Scene* scene = sceneProject->scene;

    if (entities.size() > 0){

        // to change component view order, need change ComponentType
        std::filesystem::path bundlePath;
        std::string names;
        bool isFirstEntity = true;
        for (Entity& entity : entities){
            std::vector<ComponentType> newComponents = Catalog::findComponents(scene, entity);

            if (!std::is_sorted(newComponents.begin(), newComponents.end())) {
                std::sort(newComponents.begin(), newComponents.end());
            }

            std::filesystem::path newBundlePath = project->findEntityBundlePathFor(sceneProject->id, entity);

            if (isFirstEntity) {
                components = newComponents;
                bundlePath = newBundlePath;
                isFirstEntity = false;
            } else {
                std::vector<ComponentType> intersection;
                intersection.reserve(std::min(components.size(), newComponents.size()));

                std::set_intersection(
                    components.begin(), components.end(),
                    newComponents.begin(), newComponents.end(),
                    std::back_inserter(intersection));

                components = std::move(intersection);

                if (bundlePath != newBundlePath) {
                    bundlePath.clear(); // Different bundles, so no bundle
                }

                names += ", ";
            }

            names += scene->getEntityName(entity);
        }

        // Stop destroys what the running scene made, so while playing only authored entities are editable
        const std::vector<Entity>& authored = sceneProject->entities;
        const bool isRuntimeSelection = sceneProject->playState != ScenePlayState::STOPPED &&
            std::any_of(entities.begin(), entities.end(), [&authored](Entity entity) {
                return std::find(authored.begin(), authored.end(), entity) == authored.end();
            });

        if (entities.size() == 1){
            ImGui::Text("Entity");
        }else{
            ImGui::Text("Entities (%lu)", entities.size());
        }
        ImGui::SetItemTooltip("%lu selected: %s", entities.size(), names.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        static char nameBuffer[128];
        strncpy(nameBuffer, names.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::BeginDisabled(entities.size() != 1 || isRuntimeSelection);
        if (nameFocusFrames > 0 && entities.size() == 1) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##input_name", nameBuffer, IM_ARRAYSIZE(nameBuffer));
        if (ImGui::IsItemActive()) nameFocusFrames = 0;
        ImGui::EndDisabled();
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (entities.size() == 1){
                if (nameBuffer[0] != '\0' && strcmp(nameBuffer, scene->getEntityName(entities[0]).c_str()) != 0) {
                    CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new EntityNameCmd(project, sceneProject->id, entities[0], nameBuffer));
                }
            }
        }

        ImGui::Separator();

        float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        ImGui::BeginDisabled(isRuntimeSelection);
        if (ImGui::Button(ICON_FA_SQUARE_PLUS " New component", ImVec2(buttonWidth, 0))) {
            componentAddDialog.open(
                [this, sceneProject, entities](ComponentType cpType) {
                    // Add component to all selected entities
                    for (const Entity& entity : entities) {
                        cmd = new AddComponentCmd(project, sceneProject->id, entity, cpType);
                        CommandHandle::get(project->getSelectedSceneId())->addCommand(cmd);
                    }
                    cmd->setNoMerge();

                    // Mark scene as modified
                    sceneProject->isModified = true;
                },
                [this, sceneProject, entities](ComponentType cpType) {
                    // Check if component can be added to all selected entities
                    for (const Entity& entity : entities) {
                        if (!canAddComponent(sceneProject, entity, cpType)) {
                            return false;
                        }
                    }
                    return true;
                },
                [](){}
            );
        }

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_FILE_CIRCLE_PLUS " New Script", ImVec2(buttonWidth, 0))) {
            Entity firstEntity = entities.empty() ? NULL_ENTITY : entities[0];
            std::string defaultName = "NewScript";
            if (firstEntity != NULL_ENTITY) {
                std::string entityName = sceneProject->scene->getEntityName(firstEntity);
                if (!entityName.empty())
                    defaultName = entityName + "Script";
            }
            scriptCreateDialog.open(
                sceneProject->scene,
                firstEntity,
                project->getProjectPath(),
                project->getLuaPath(),
                defaultName,
                [this, sceneProject, entities](const std::filesystem::path& headerPath,
                                            const std::filesystem::path& sourcePath,
                                            const std::string& name,
                                            ScriptType type) {

                    editor::MultiPropertyCmd* multiCmd = new editor::MultiPropertyCmd();

                    for (const Entity& entity : entities) {
                        bool hasScriptComponent = false;
                        std::vector<ComponentType> existingComponents = Catalog::findComponents(sceneProject->scene, entity);
                        for (ComponentType ct : existingComponents) {
                            if (ct == ComponentType::ScriptComponent) {
                                hasScriptComponent = true;
                                break;
                            }
                        }

                        std::vector<ScriptEntry> newScripts;

                        if (!hasScriptComponent) {
                            auto addCmd = std::make_unique<editor::AddComponentCmd>(project, sceneProject->id, entity, ComponentType::ScriptComponent);
                            multiCmd->addCommand(std::move(addCmd));
                        } else {
                            ScriptComponent& scriptComp = sceneProject->scene->getComponent<ScriptComponent>(entity);
                            newScripts = scriptComp.scripts;
                        }

                        ScriptEntry entry;
                        entry.type = type;
                        entry.enabled = true;

                        if (type == ScriptType::CPP) {
                            entry.headerPath = headerPath.string();
                            entry.path = sourcePath.string();
                            entry.className = name;
                        } else if (type == ScriptType::LUA) {
                            entry.headerPath.clear();
                            entry.path = sourcePath.string();
                            entry.className = name; // module (file base) name
                        }

                        newScripts.push_back(entry);

                        project->updateScriptProperties(sceneProject, entity, newScripts);

                        auto propCmd = std::make_unique<editor::PropertyCmd<std::vector<ScriptEntry>>>(
                            project, sceneProject->id, entity, ComponentType::ScriptComponent, "scripts", newScripts
                        );
                        multiCmd->addCommand(std::move(propCmd));
                    }

                    multiCmd->setNoMerge();
                    CommandHandle::get(project->getSelectedSceneId())->addCommand(multiCmd);
                },
                [](){}
            );
        }

        ImGui::EndDisabled();

        // Show the component add dialog
        componentAddDialog.show();

        bool isBundle = !bundlePath.empty();

        // Root entity of a bundle is always local, not part of the bundle template
        if (isBundle) {
            EntityBundle* bundle = project->getEntityBundle(bundlePath);
            if (bundle) {
                for (Entity& entity : entities) {
                    Entity root = bundle->getRootEntity(sceneProject->id, entity);
                    if (root == entity && bundle->getRegistryEntity(sceneProject->id, entity) == NULL_ENTITY) {
                        isBundle = false;
                        break;
                    }
                }
            }
        }

        for (ComponentType& cpType : components){

            // Check if this component is overridden for bundle entities
            bool isBundleOverridden = false;
            EntityBundle* entityBundle = nullptr;
            if (isBundle) {
                entityBundle = project->getEntityBundle(bundlePath);
                if (entityBundle) {
                    for (Entity& entity : entities) {
                        if (entityBundle->hasComponentOverride(sceneProject->id, entity, cpType)) {
                            isBundleOverridden = true;
                            break;
                        }
                        isBundleOverridden = false;
                    }
                }
            }

            // Create header with appropriate styling
            ImGui::PushID(static_cast<int>(cpType));

            // Build header text with icon
            std::string headerText;

            if (isBundle && !isBundleOverridden) {
                // Bundle components - light blue with cube icon (same as Structure window)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                headerText = ICON_FA_CUBE " ";
            }
            headerText += Catalog::getComponentName(cpType);

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            bool headerOpen = ImGui::CollapsingHeader(headerText.c_str());

            if (isBundle && !isBundleOverridden) {
                ImGui::PopStyleColor();
            }

            // Context menu and body disabled for a runtime selection
            const bool compReadOnly = isRuntimeSelection;
            handleComponentMenu(sceneProject, entities, cpType, isBundle, isBundleOverridden, headerOpen, compReadOnly);

            // Add hover tooltip for bundle components
            if (isBundle && !isBundleOverridden && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), ICON_FA_CUBE " Bundle Component");
                ImGui::Text("This component comes from the bundle template.");
                ImGui::EndTooltip();
            }

            ImGui::PopID();

            if (headerOpen){
                // Disable non-script components while playing
                if (compReadOnly) ImGui::BeginDisabled(true);

                if (cpType == ComponentType::Transform){
                    drawTransform(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::MeshComponent){
                    drawMeshComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ModelComponent){
                    drawModelComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::UIComponent){
                    drawUIComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ButtonComponent){
                    drawButtonComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScrollbarComponent){
                    drawScrollbarComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ProgressbarComponent){
                    drawProgressbarComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TextEditComponent){
                    drawTextEditComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::PanelComponent){
                    drawPanelComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::PolygonComponent){
                    drawPolygonComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::MeshPolygonComponent){
                    drawMeshPolygonComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TextComponent){
                    drawTextComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::UILayoutComponent){
                    drawUILayoutComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::UIContainerComponent){
                    drawUIContainerComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ImageComponent){
                    drawImageComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SpriteComponent){
                    drawSpriteComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TilemapComponent){
                    drawTilemapComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TerrainComponent){
                    drawTerrainComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::LightComponent){
                    drawLightComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Light2DComponent){
                    drawLight2DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Occluder2DComponent){
                    drawOccluder2DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::FogComponent){
                    drawFogComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::MirrorComponent){
                    drawMirrorComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ReflectionProbeComponent){
                    drawReflectionProbeComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::CameraComponent){
                    drawCameraComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SoundComponent){
                    drawAudioComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::WaterComponent){
                    drawWaterComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::WaterExclusionComponent){
                    drawWaterExclusionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SkyComponent){
                    drawSkyComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScriptComponent){
                    drawScriptComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Body2DComponent){
                    drawBody2DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Body3DComponent){
                    drawBody3DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Joint2DComponent){
                    drawJoint2DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::Joint3DComponent){
                    drawJoint3DComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ActionComponent){
                    drawActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TimedActionComponent){
                    drawTimedActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::PositionActionComponent){
                    drawPositionActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::RotationActionComponent){
                    drawRotationActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScaleActionComponent){
                    drawScaleActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ColorActionComponent){
                    drawColorActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::AlphaActionComponent){
                    drawAlphaActionComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SpriteAnimationComponent){
                    drawSpriteAnimationComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::AnimationComponent){
                    drawAnimationComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::BundleComponent){
                    drawBundleComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::BoneComponent){
                    drawBoneComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::SkinComponent){
                    drawSkinComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::KeyframeTracksComponent){
                    drawKeyframeTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::TranslateTracksComponent){
                    drawTranslateTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::RotateTracksComponent){
                    drawRotateTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ScaleTracksComponent){
                    drawScaleTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::MorphTracksComponent){
                    drawMorphTracksComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::ParticlesComponent){
                    drawParticlesComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::LinesComponent){
                    drawLinesComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::PointsComponent){
                    drawPointsComponent(cpType, sceneProject, entities);
                }else if (cpType == ComponentType::InstancedMeshComponent){
                    drawInstancedMeshComponent(cpType, sceneProject, entities);
                }

                if (compReadOnly) ImGui::EndDisabled();
            }
        }

    }else{
        ImGui::Text("Scene");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        static char nameBuffer[128];
        strncpy(nameBuffer, sceneProject->name.c_str(), sizeof(nameBuffer) - 1);
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';

        bool isMainScene = (sceneProject->id == project->getSelectedSceneId());
        ImGui::BeginDisabled(!isMainScene);
        if (nameFocusFrames > 0 && isMainScene) ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##input_scene_name", nameBuffer, IM_ARRAYSIZE(nameBuffer));
        if (ImGui::IsItemActive()) nameFocusFrames = 0;
        ImGui::EndDisabled();

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (isMainScene && nameBuffer[0] != '\0' && strcmp(nameBuffer, sceneProject->name.c_str()) != 0) {
                CommandHandle::get(project->getSelectedSceneId())->addCommandNoMerge(new SceneNameCmd(project, sceneProject->id, nameBuffer));
            }
        }
        ImGui::Separator();

        if (sceneProject->scene) {
            ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchSame;
            const float shadowQualityLabelColWidth = getLabelSize("Filter Quality") + ImGui::GetStyle().CellPadding.x * 2.0f;
            const float shadowQualityComboWidth = ImGui::CalcTextSize("Medium").x + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f;

            if (ImGui::BeginTable("scene_settings_table", 2, tableFlags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Background"));
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                drawScenePropertyRow<Vector4>(sceneProject, "background_color", "Background", ScenePropertyInputType::ColorRGBA);
                drawScenePropertyRow<LightState>(sceneProject, "light_state", "Lights", ScenePropertyInputType::Combo, 5 * ImGui::GetFontSize());

                ImGui::EndTable();
            }

            if (sceneProject->sceneType != SceneType::SCENE_3D) {
                ImGui::SeparatorText("Ambient Light (2D)");

                if (ImGui::BeginTable("scene_ambient2d_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Intensity"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<Vector3>(sceneProject, "ambient_light_2d_color", "Color", ScenePropertyInputType::ColorRGB);
                    drawScenePropertyRow<float>(sceneProject, "ambient_light_2d_intensity", "Intensity", ScenePropertyInputType::SliderFloat, -1.0f, 0.0f, 1.0f);

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("2D Shadows");

                if (ImGui::BeginTable("scene_shadows2d_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, shadowQualityLabelColWidth);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<ShadowQuality>(sceneProject, "shadows_2d_quality", "Filter Quality", ScenePropertyInputType::Combo, shadowQualityComboWidth, 0.0f, 1.0f,
                        "Smoothness of 2D light shadow edges. The penumbra width is set per light (Shadow Softness):\n"
                        "- None: 1 sample, no filtering, hard edges (softness is ignored)\n"
                        "- Low: 5 samples\n"
                        "- Medium: 9 samples\n"
                        "- High: 13 samples, smooth even with very wide penumbras\n"
                        "Higher values cost more per lit pixel. Raise it if wide penumbras show banding.");

                    ImGui::EndTable();
                }
            }

            LightState currentLightState = doriax::editor::Catalog::getSceneProperty<LightState>(sceneProject->scene, "light_state");
            if (currentLightState != LightState::OFF) {
                ImGui::SeparatorText("Global Illumination");

                if (ImGui::BeginTable("scene_globalillum_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Intensity"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<Vector3>(sceneProject, "global_illumination_color", "Color", ScenePropertyInputType::ColorRGB);
                    drawScenePropertyRow<float>(sceneProject, "global_illumination_intensity", "Intensity", ScenePropertyInputType::SliderFloat, -1.0f, 0.0f, 1.0f);

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Shadows");

                if (ImGui::BeginTable("scene_shadow_settings_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, shadowQualityLabelColWidth);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<ShadowQuality>(sceneProject, "shadows_quality", "Filter Quality", ScenePropertyInputType::Combo, shadowQualityComboWidth, 0.0f, 1.0f,
                        "Smoothness of shadow edges (PCF size):\n"
                        "- None: 1 sample, hard edges\n"
                        "- Low: 3x3 samples\n"
                        "- Medium: 5x5 samples\n"
                        "- High: 7x7 samples\n"
                        "Higher values cost more per shadowed pixel.");

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Mesh Detail (LOD)");

                if (ImGui::BeginTable("scene_lod_settings_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Threshold"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<bool>(sceneProject, "mesh_lod_enabled", "Enabled", ScenePropertyInputType::Checkbox, -1.0f, 0.0f, 1.0f,
                        "Draw simplified versions of meshes when their detail no longer shows on screen. Off: every mesh draws at full detail and no levels are built.");
                    if (doriax::editor::Catalog::getSceneProperty<bool>(sceneProject->scene, "mesh_lod_enabled")) {
                        drawScenePropertyRow<float>(sceneProject, "mesh_lod_threshold", "Threshold", ScenePropertyInputType::SliderFloat, -1.0f, 0.25f, 8.0f,
                            "Screen pixels of geometric error a simplified mesh may show. Lower keeps more detail.");
                    }

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Depth Prepass");

                if (ImGui::BeginTable("scene_prepass_settings_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Enabled"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<bool>(sceneProject, "depth_prepass_enabled", "Enabled", ScenePropertyInputType::Checkbox, -1.0f, 0.0f, 1.0f,
                        "Write depth before the opaque color pass so each pixel is shaded once. Pays off with heavy overdraw (dense foliage); costs an extra geometry pass.");

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Ambient Occlusion (SSAO)");

                if (ImGui::BeginTable("scene_ssao_settings_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Intensity"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<bool>(sceneProject, "ssao_enabled", "Enabled", ScenePropertyInputType::Checkbox);

                    if (doriax::editor::Catalog::getSceneProperty<bool>(sceneProject->scene, "ssao_enabled")) {
                        drawScenePropertyRow<float>(sceneProject, "ssao_radius", "Radius", ScenePropertyInputType::SliderFloat, -1.0f, 0.05f, 2.0f);
                        drawScenePropertyRow<float>(sceneProject, "ssao_intensity", "Intensity", ScenePropertyInputType::SliderFloat, -1.0f, 0.1f, 4.0f);
                        drawScenePropertyRow<float>(sceneProject, "ssao_bias", "Bias", ScenePropertyInputType::SliderFloat, -1.0f, 0.0f, 0.1f);
                        drawScenePropertyRow<bool>(sceneProject, "ssao_debug", "Debug View", ScenePropertyInputType::Checkbox);
                    }

                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Reflections (SSR)");

                if (ImGui::BeginTable("scene_ssr_settings_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Max Distance"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    drawScenePropertyRow<bool>(sceneProject, "ssr_enabled", "Enabled", ScenePropertyInputType::Checkbox);

                    if (doriax::editor::Catalog::getSceneProperty<bool>(sceneProject->scene, "ssr_enabled")) {
                        drawScenePropertyRow<float>(sceneProject, "ssr_max_distance", "Max Distance", ScenePropertyInputType::SliderFloat, -1.0f, 1.0f, 50.0f);
                        drawScenePropertyRow<float>(sceneProject, "ssr_thickness", "Thickness", ScenePropertyInputType::SliderFloat, -1.0f, 0.05f, 4.0f);
                        drawScenePropertyRow<float>(sceneProject, "ssr_intensity", "Intensity", ScenePropertyInputType::SliderFloat, -1.0f, 0.0f, 2.0f);
                        drawScenePropertyRow<float>(sceneProject, "ssr_blur", "Glossy Blur", ScenePropertyInputType::SliderFloat, -1.0f, 0.0f, 1.0f);

                        // Debug View: visualize the SSR G-buffer channels (centralized in the composite)
                        {
                            const char* ssrDebugNames[] = { "Off", "Reflection", "Normal", "Roughness", "Metallic", "Albedo", "IBL Specular" };
                            int debugMode = doriax::editor::Catalog::getSceneProperty<int>(sceneProject->scene, "ssr_debug_mode");
                            int debugModeDefault = doriax::editor::Catalog::getScenePropertyDefault<int>("ssr_debug_mode");
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::Text("Debug View");
                            if (debugMode != debugModeDefault) {
                                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, ImGui::GetStyle().ItemSpacing.y));
                                ImGui::SameLine();
                                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                                if (ImGui::Button(ICON_FA_ROTATE_LEFT"##reset_ssr_debug_mode")) {
                                    Command* resetCmd = new ScenePropertyCmd<int>(project, sceneProject->id, "ssr_debug_mode", debugModeDefault);
                                    CommandHandle::get(sceneProject->id)->addCommandNoMerge(resetCmd);
                                }
                                ImGui::PopStyleColor(2);
                                ImGui::PopStyleVar(3);
                            }
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(8 * ImGui::GetFontSize());
                            if (ImGui::Combo("##ssr_debug_mode", &debugMode, ssrDebugNames, IM_ARRAYSIZE(ssrDebugNames))) {
                                Command* cmd = new ScenePropertyCmd<int>(project, sceneProject->id, "ssr_debug_mode", debugMode);
                                CommandHandle::get(sceneProject->id)->addCommand(cmd);
                            }
                        }
                    }

                    ImGui::EndTable();
                }
            }

            if (sceneProject->sceneType != SceneType::SCENE_UI) {
                ImGui::SeparatorText("Physics");

                if (ImGui::BeginTable("scene_physics_table", 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Gravity"));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                    if (sceneProject->sceneType == SceneType::SCENE_3D) {
                        drawScenePropertyRow<Vector3>(sceneProject, "physics_gravity_3d", "Gravity", ScenePropertyInputType::DragFloat, -1.0f, 0.0f, 1.0f,
                            "Gravity applied to 3D physics bodies (meters/s^2).");
                    } else {
                        drawScenePropertyRow<Vector2>(sceneProject, "physics_gravity_2d", "Gravity", ScenePropertyInputType::DragFloat, -1.0f, 0.0f, 1.0f,
                            "Gravity applied to 2D physics bodies (meters/s^2).");
                    }

                    ImGui::EndTable();
                }
            }

            ImGui::SeparatorText("Fixed Resolution");

            if (ImGui::BeginTable("scene_fixed_resolution_table", 2, tableFlags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getLabelSize("Enabled"));
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                drawScenePropertyRow<bool>(sceneProject, "fixed_resolution_enabled", "Enabled", ScenePropertyInputType::Checkbox, -1.0f, 0.0f, 1.0f,
                    "Renders the main camera at a fixed resolution and upscales it to the view.\n"
                    "Applies only in play mode and when this scene is the main scene; layer\n"
                    "scenes stay at native resolution on top. Letterbox/crop still follows the\n"
                    "project canvas and scaling mode.");

                if (doriax::editor::Catalog::getSceneProperty<bool>(sceneProject->scene, "fixed_resolution_enabled")) {
                    drawScenePropertyRow<int>(sceneProject, "fixed_resolution_width", "Width", ScenePropertyInputType::DragInt, 6 * ImGui::GetFontSize(), 1.0f, 8192.0f);
                    drawScenePropertyRow<int>(sceneProject, "fixed_resolution_height", "Height", ScenePropertyInputType::DragInt, 6 * ImGui::GetFontSize(), 1.0f, 8192.0f);
                    drawScenePropertyRow<TextureFilter>(sceneProject, "fixed_resolution_filter", "Filter", ScenePropertyInputType::Combo, 7 * ImGui::GetFontSize(), 0.0f, 1.0f,
                        "How the low-resolution image is sampled when upscaled:\n"
                        "- Nearest: crisp pixels (pixel-art look)\n"
                        "- Linear: smooth interpolation");
                }

                ImGui::EndTable();
            }

            // Ordered fullscreen passes run after the scene color (and SSR) pass; each one
            // is a forked shader whose uniforms become rows here.
            if (sceneProject->sceneType != SceneType::SCENE_UI) {
                ImGui::SeparatorText("Post-processing");
                drawScenePostProcess(sceneProject);
            }

            // Scene default shaders: used by components of each type whose customShader is
            // empty (priority: component shader > scene default > built-in).
            ImGui::SeparatorText("Default Shaders");

            // each shader type, and a forked default's uniforms, in a table of its own
            bool afterUniforms = false;
            auto drawDefaultShader = [&](ShaderType type, const char* propertyName, const char* label) {
                // a line closes the uniforms above
                if (afterUniforms)
                    ImGui::Separator();
                afterUniforms = false;

                bool used = false;
                ShaderUniformRows rows;
                if (ImGui::BeginTable((std::string(propertyName) + "_table").c_str(), 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Points").x);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    drawSceneShaderRow(sceneProject, type, propertyName, label);
                    if (!sceneProject->scene->getDefaultCustomShader(type).empty()) {
                        rows = resolveSceneShaderUniformRows(sceneProject, type, used);
                        drawSceneShaderUniforms(sceneProject, type, rows, used, true);
                    }
                    ImGui::EndTable();
                }
                if (rows.members.empty())
                    return;

                shaderUniformsSeparator(sceneProject->scene->getDefaultCustomShader(type));
                if (ImGui::BeginTable((std::string(propertyName) + "_uniforms_table").c_str(), 2, tableFlags)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, getShaderUniformsLabelSize(rows.members, false));
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    drawSceneShaderUniforms(sceneProject, type, rows, used, false);
                    ImGui::EndTable();
                }
                afterUniforms = true;
            };

            if (sceneProject->sceneType != SceneType::SCENE_UI) {
                drawDefaultShader(ShaderType::MESH, "default_mesh_shader", "Mesh");
                if (sceneProject->sceneType == SceneType::SCENE_3D) {
                    drawDefaultShader(ShaderType::SKYBOX, "default_sky_shader", "Sky");
                    drawDefaultShader(ShaderType::WATER, "default_water_shader", "Water");
                }
            }
            drawDefaultShader(ShaderType::UI, "default_ui_shader", "UI");
            if (sceneProject->sceneType != SceneType::SCENE_UI) {
                drawDefaultShader(ShaderType::POINTS, "default_points_shader", "Points");
                drawDefaultShader(ShaderType::LINES, "default_lines_shader", "Lines");
            }
        }

        thumbnailTextures.clear();
    }

    // Clean up unused material renders whose scene is not running
    for (auto it = materialRenders.begin(); it != materialRenders.end(); ) {
        if (usedPreviewIds.find(it->first) == usedPreviewIds.end() && !Engine::isSceneRunning(it->second.getScene())) {
            it = materialRenders.erase(it);
        } else {
            ++it;
        }
    }

    // Clean up unused direction renders whose scene is not running
    for (auto it = directionRenders.begin(); it != directionRenders.end(); ) {
        if (usedPreviewIds.find(it->first) == usedPreviewIds.end() && !Engine::isSceneRunning(it->second.getScene())) {
            it = directionRenders.erase(it);
        } else {
            ++it;
        }
    }
    usedPreviewIds.clear();

    scriptCreateDialog.show();
    shaderForkDialog.show();
    textureSlicerToolDialog.show();

    ImGui::End();

    if (wasOpen && !windowOpen) {
        setOpen(false);
    }
}
