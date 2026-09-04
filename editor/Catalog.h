// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include "Scene.h"
#include "SceneSettings.h"
#include "util/Color.h"

namespace doriax::editor{
    enum UpdateFlags : uint64_t {
        UpdateFlags_None                = 0,
        UpdateFlags_Transform           = 1 << 0,
        UpdateFlags_Camera              = 1 << 1,
        UpdateFlags_Scene_Mesh_Reload   = 1 << 2,
        UpdateFlags_Mesh_Reload         = 1 << 3,
        UpdateFlags_Mesh_Texture        = 1 << 4,
        UpdateFlags_UI_Reload           = 1 << 5,
        UpdateFlags_UI_Texture          = 1 << 6,
        UpdateFlags_Image_Patches       = 1 << 7,
        UpdateFlags_Layout_Sizes        = 1 << 8,
        UpdateFlags_Layout_Anchors      = 1 << 9,
        UpdateFlags_Sprite              = 1 << 10,
        UpdateFlags_LightShadowMap      = 1 << 11,
        UpdateFlags_LightShadowCamera   = 1 << 12,
        UpdateFlags_Sky_Texture         = 1 << 13,
        UpdateFlags_Sky                 = 1 << 14,
        UpdateFlags_Text                = 1 << 15,
        UpdateFlags_Text_Atlas          = 1 << 16,
        UpdateFlags_Body2D              = 1 << 17,
        UpdateFlags_Body3D              = 1 << 18,
        UpdateFlags_Joint2D             = 1 << 19,
        UpdateFlags_Joint3D             = 1 << 20,
        UpdateFlags_Model               = 1 << 21,
        UpdateFlags_Tilemap             = 1 << 22,
        UpdateFlags_Instanced_Mesh      = 1 << 23,
        UpdateFlags_Points              = 1 << 24,
        UpdateFlags_Terrain             = 1 << 25,
        UpdateFlags_Terrain_Texture     = 1 << 26,
        UpdateFlags_Sound               = 1 << 27,
        UpdateFlags_Lines               = 1 << 28,
        UpdateFlags_Lines_Reload        = 1 << 29,
        UpdateFlags_Points_Texture      = 1 << 30,
        // Generic "reload the renderable's shader" flag, used by the per-component
        // customShader property of Mesh/UI/Points/Lines/Sky.
        UpdateFlags_Shader_Reload       = (uint64_t)1 << 31,
        UpdateFlags_Reflection_Probe_Capture  = (uint64_t)1 << 32,
        UpdateFlags_Reflection_Probe_Sampling = (uint64_t)1 << 33,
        // for changes outside the probe itself, like a mesh leaving the capture pass
        UpdateFlags_Reflection_Probe_Recapture_All = (uint64_t)1 << 34,
        UpdateFlags_Terrain_Foliage     = (uint64_t)1 << 35,
        // handlers that touch the whole scene, must be masked out when applying all flags
        UpdateFlags_SceneWide = UpdateFlags_Scene_Mesh_Reload | UpdateFlags_Reflection_Probe_Recapture_All
    };

    // the order of components here affects properties window
    enum class ComponentType : int {
        Transform,
        ScriptComponent,
        MeshComponent,
        LinesComponent,
        LightComponent,
        TerrainComponent,
        PointsComponent,
        SkyComponent,
        ModelComponent,
        UIComponent,
        UILayoutComponent,
        ActionComponent,
        TimedActionComponent,
        AlphaActionComponent,
        AnimationComponent,
        SoundComponent,
        Body2DComponent,
        Body3DComponent,
        BoneComponent,
        BundleComponent,
        ButtonComponent,
        CameraComponent,
        ColorActionComponent,
        FogComponent,
        ImageComponent,
        InstancedMeshComponent,
        Joint2DComponent,
        Joint3DComponent,
        KeyframeTracksComponent,
        MeshPolygonComponent,
        MorphTracksComponent,
        PanelComponent,
        ParticlesComponent,
        PolygonComponent,
        PositionActionComponent,
        ProgressbarComponent,
        RotateTracksComponent,
        RotationActionComponent,
        ScaleActionComponent,
        ScaleTracksComponent,
        ScrollbarComponent,
        SpriteAnimationComponent,
        SpriteComponent,
        TextComponent,
        TextEditComponent,
        TilemapComponent,
        TranslateTracksComponent,
        UIContainerComponent,
        MirrorComponent,
        Light2DComponent,
        Occluder2DComponent,
        // Keep new component types at the end: their ordinal is used as the bit
        // position in EntityBundle's in-memory component override mask.
        ReflectionProbeComponent
    };

    enum class PropertyType{
        Bool,
        String,
        Float,
        Double,
        Vector2,
        Vector3,
        Vector4,
        Quat,
        Int,
        UInt,
        Material,
        Texture,
        Font,
        Enum,
        Ease,
        Custom,
        Entity,
        EntityReference
    };

    struct PropertyData{
        PropertyType type;
        uint64_t updateFlags;
        void* def;
        void* ref;
    };

    class Catalog{
    private:

        static std::string removeComponentSuffix(std::string str);

    public:
        Catalog();

        static std::string getComponentName(ComponentType component, bool removeSuffix = false);
        static ComponentId getComponentId(const EntityRegistry* registry, ComponentType compType);
        static ComponentType getComponentType(const std::string& componentName);
        static Signature componentTypeToSignature(const EntityRegistry* registry, ComponentType compType);
        static Signature componentMaskToSignature(const EntityRegistry* registry, uint64_t mask);

        static PropertyType scriptPropertyTypeToPropertyType(ScriptPropertyType scriptType);

        static std::map<std::string, PropertyData> getProperties(ComponentType component, void* compRef);

        static std::vector<ComponentType> findComponents(EntityRegistry* registry, Entity entity);
        static std::map<std::string, PropertyData> findEntityProperties(EntityRegistry* registry, Entity entity, ComponentType component);

        static uint64_t getChangedUpdateFlags(ComponentType compType, void* oldComp, void* newComp);
        static uint64_t getComponentStructuralUpdateFlags(ComponentType compType);

        static void updateEntity(EntityRegistry* registry, Entity entity, uint64_t updateFlags);

        static void copyComponent(EntityRegistry* sourceRegistry, Entity sourceEntity,
                                EntityRegistry* targetRegistry, Entity targetEntity,
                                ComponentType compType);
        static void copyPropertyValue(EntityRegistry* sourceRegistry, Entity sourceEntity, 
                                    EntityRegistry* targetRegistry, Entity targetEntity, 
                                    ComponentType compType, const std::string& property);

        static PropertyData getProperty(EntityRegistry* registry, Entity entity, ComponentType component, std::string propertyName);

        // The same lookup without the not-found log, for callers a miss is expected on
        static PropertyData findProperty(EntityRegistry* registry, Entity entity, ComponentType component, std::string propertyName);

        // Submesh::overrideFields of a "submeshes[N].<field>" property, null for anything else,
        // with propertyFields set to the bits that property occupies. A command ORs them in after
        // writing the value, and puts the whole mask back on undo.
        static uint32_t* getSubmeshOverrideMask(EntityRegistry* registry, Entity entity, ComponentType component, const std::string& propertyName, uint32_t& propertyFields);

        template<typename T>
        static T* getPropertyRef(EntityRegistry* registry, Entity entity, ComponentType component, std::string propertyName){
            PropertyData property = Catalog::getProperty(registry, entity, component, propertyName);
            if (property.ref){
                return static_cast<T*>(property.ref);
            }

            return nullptr;
        }

        template<typename T>
        static T getSceneProperty(Scene* scene, const std::string& propertyName) {
            if (!scene) {
                // Return default values if scene is null
                if constexpr (std::is_same_v<T, Vector4>) return Vector4(0.0, 0.0, 0.0, 1.0);
                if constexpr (std::is_same_v<T, Vector3>) return Vector3(1.0, 1.0, 1.0);
                if constexpr (std::is_same_v<T, Vector2>) return Vector2(0.0, 0.0);
                if constexpr (std::is_same_v<T, bool>) return false;
                if constexpr (std::is_same_v<T, float>) return 0.0f;
                if constexpr (std::is_same_v<T, int>) return 0;
                if constexpr (std::is_same_v<T, LightState>) return LightState::AUTO;
                if constexpr (std::is_same_v<T, ShadowQuality>) return ShadowQuality::LOW;
                if constexpr (std::is_same_v<T, TextureFilter>) return TextureFilter::NEAREST;
                if constexpr (std::is_same_v<T, std::string>) return std::string();
                // Add other types as needed
            }

            if (propertyName == "background_color") {
                if constexpr (std::is_same_v<T, Vector4>) {
                    return scene->getBackgroundColor();
                }
            }
            else if (propertyName == "shadows_quality") {
                if constexpr (std::is_same_v<T, ShadowQuality>) {
                    return scene->getShadowQuality();
                }
            }
            else if (propertyName == "global_illumination_color") {
                if constexpr (std::is_same_v<T, Vector3>) {
                    return scene->getGlobalIlluminationColor();
                }
            }
            else if (propertyName == "global_illumination_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getGlobalIlluminationIntensity();
                }
            }
            else if (propertyName == "ambient_light_2d_color") {
                if constexpr (std::is_same_v<T, Vector3>) {
                    return scene->getAmbientLight2DColor();
                }
            }
            else if (propertyName == "ambient_light_2d_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getAmbientLight2DIntensity();
                }
            }
            else if (propertyName == "shadows_2d_quality") {
                if constexpr (std::is_same_v<T, ShadowQuality>) {
                    return scene->getShadow2DQuality();
                }
            }
            else if (propertyName == "physics_gravity_2d") {
                if constexpr (std::is_same_v<T, Vector2>) {
                    return scene->getGravity2D();
                }
            }
            else if (propertyName == "physics_gravity_3d") {
                if constexpr (std::is_same_v<T, Vector3>) {
                    return scene->getGravity3D();
                }
            }
            else if (propertyName == "light_state") {
                if constexpr (std::is_same_v<T, LightState>) {
                    return scene->getLightState();
                }
            }
            else if (propertyName == "ssao_enabled") {
                if constexpr (std::is_same_v<T, bool>) {
                    return scene->isSSAOEnabled();
                }
            }
            else if (propertyName == "ssao_radius") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSAORadius();
                }
            }
            else if (propertyName == "ssao_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSAOIntensity();
                }
            }
            else if (propertyName == "ssao_bias") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSAOBias();
                }
            }
            else if (propertyName == "ssao_debug") {
                if constexpr (std::is_same_v<T, bool>) {
                    return scene->isSSAODebug();
                }
            }
            else if (propertyName == "ssr_enabled") {
                if constexpr (std::is_same_v<T, bool>) {
                    return scene->isSSREnabled();
                }
            }
            else if (propertyName == "ssr_max_distance") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSRMaxDistance();
                }
            }
            else if (propertyName == "ssr_thickness") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSRThickness();
                }
            }
            else if (propertyName == "ssr_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSRIntensity();
                }
            }
            else if (propertyName == "ssr_blur") {
                if constexpr (std::is_same_v<T, float>) {
                    return scene->getSSRBlur();
                }
            }
            else if (propertyName == "ssr_max_steps") {
                if constexpr (std::is_same_v<T, int>) {
                    return scene->getSSRMaxSteps();
                }
            }
            else if (propertyName == "ssr_debug_mode") {
                if constexpr (std::is_same_v<T, int>) {
                    return scene->getSSRDebugMode();
                }
            }
            else if (propertyName == "fixed_resolution_enabled") {
                if constexpr (std::is_same_v<T, bool>) {
                    return scene->isFixedResolutionEnabled();
                }
            }
            else if (propertyName == "fixed_resolution_width") {
                if constexpr (std::is_same_v<T, int>) {
                    return (int)scene->getFixedResolutionWidth();
                }
            }
            else if (propertyName == "fixed_resolution_height") {
                if constexpr (std::is_same_v<T, int>) {
                    return (int)scene->getFixedResolutionHeight();
                }
            }
            else if (propertyName == "fixed_resolution_filter") {
                if constexpr (std::is_same_v<T, TextureFilter>) {
                    return scene->getFixedResolutionFilter();
                }
            }
            else if (propertyName == "default_mesh_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    return scene->getDefaultMeshShader();
                }
            }
            else if (propertyName == "default_ui_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    return scene->getDefaultUIShader();
                }
            }
            else if (propertyName == "default_sky_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    return scene->getDefaultSkyShader();
                }
            }
            else if (propertyName == "default_points_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    return scene->getDefaultPointsShader();
                }
            }
            else if (propertyName == "default_lines_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    return scene->getDefaultLinesShader();
                }
            }
            else if (propertyName == "post_process") {
                if constexpr (std::is_same_v<T, std::vector<PostProcessPass>>) {
                    return scene->getPostProcessPasses();
                }
            }

            // Return default value if property not found
            if constexpr (std::is_same_v<T, Vector4>) return Vector4(0.0, 0.0, 0.0, 1.0);
            if constexpr (std::is_same_v<T, Vector3>) return Vector3(1.0, 1.0, 1.0);
            if constexpr (std::is_same_v<T, Vector2>) return Vector2(0.0, 0.0);
            if constexpr (std::is_same_v<T, bool>) return false;
            if constexpr (std::is_same_v<T, float>) return 0.0f;
            if constexpr (std::is_same_v<T, int>) return 0;
            if constexpr (std::is_same_v<T, LightState>) return LightState::AUTO;
                if constexpr (std::is_same_v<T, ShadowQuality>) return ShadowQuality::LOW;
            if constexpr (std::is_same_v<T, TextureFilter>) return TextureFilter::NEAREST;
            if constexpr (std::is_same_v<T, std::string>) return std::string();
            if constexpr (std::is_same_v<T, std::vector<PostProcessPass>>) return std::vector<PostProcessPass>();
            // Add other types as needed
        }

        // Factory default for a scene property, used by the Properties panel's "reset to
        // default" arrow. Values come straight from a default-constructed SceneSettings — the
        // engine's single source of truth for scene defaults (Scene embeds one) — so they can
        // never drift from what a new scene gets. SceneSettings is plain data (no physics
        // runtime), so this is cheap; no caching needed. Values are returned in the same space
        // getSceneProperty exposes (GI/ambient colors are converted from stored linear to sRGB).
        template<typename T>
        static T getScenePropertyDefault(const std::string& propertyName) {
            static const SceneSettings d;

            if (propertyName == "background_color") {
                if constexpr (std::is_same_v<T, Vector4>) return d.backgroundColor;
            }
            else if (propertyName == "shadows_quality") {
                if constexpr (std::is_same_v<T, ShadowQuality>) return d.shadowQuality;
            }
            else if (propertyName == "global_illumination_color") {
                if constexpr (std::is_same_v<T, Vector3>) return Color::linearTosRGB(d.globalIllumColor);
            }
            else if (propertyName == "global_illumination_intensity") {
                if constexpr (std::is_same_v<T, float>) return d.globalIllumIntensity;
            }
            else if (propertyName == "ambient_light_2d_color") {
                if constexpr (std::is_same_v<T, Vector3>) return Color::linearTosRGB(d.ambientLight2DColor);
            }
            else if (propertyName == "ambient_light_2d_intensity") {
                if constexpr (std::is_same_v<T, float>) return d.ambientLight2DIntensity;
            }
            else if (propertyName == "shadows_2d_quality") {
                if constexpr (std::is_same_v<T, ShadowQuality>) return d.shadow2DQuality;
            }
            else if (propertyName == "physics_gravity_2d") {
                if constexpr (std::is_same_v<T, Vector2>) return d.gravity2D;
            }
            else if (propertyName == "physics_gravity_3d") {
                if constexpr (std::is_same_v<T, Vector3>) return d.gravity3D;
            }
            else if (propertyName == "light_state") {
                if constexpr (std::is_same_v<T, LightState>) return d.lightState;
            }
            else if (propertyName == "ssao_enabled") {
                if constexpr (std::is_same_v<T, bool>) return d.ssaoEnabled;
            }
            else if (propertyName == "ssao_radius") {
                if constexpr (std::is_same_v<T, float>) return d.ssaoRadius;
            }
            else if (propertyName == "ssao_intensity") {
                if constexpr (std::is_same_v<T, float>) return d.ssaoIntensity;
            }
            else if (propertyName == "ssao_bias") {
                if constexpr (std::is_same_v<T, float>) return d.ssaoBias;
            }
            else if (propertyName == "ssao_debug") {
                if constexpr (std::is_same_v<T, bool>) return d.ssaoDebug;
            }
            else if (propertyName == "ssr_enabled") {
                if constexpr (std::is_same_v<T, bool>) return d.ssrEnabled;
            }
            else if (propertyName == "ssr_max_distance") {
                if constexpr (std::is_same_v<T, float>) return d.ssrMaxDistance;
            }
            else if (propertyName == "ssr_thickness") {
                if constexpr (std::is_same_v<T, float>) return d.ssrThickness;
            }
            else if (propertyName == "ssr_intensity") {
                if constexpr (std::is_same_v<T, float>) return d.ssrIntensity;
            }
            else if (propertyName == "ssr_blur") {
                if constexpr (std::is_same_v<T, float>) return d.ssrBlur;
            }
            else if (propertyName == "ssr_max_steps") {
                if constexpr (std::is_same_v<T, int>) return d.ssrMaxSteps;
            }
            else if (propertyName == "ssr_debug_mode") {
                if constexpr (std::is_same_v<T, int>) return d.ssrDebugMode;
            }
            else if (propertyName == "fixed_resolution_enabled") {
                if constexpr (std::is_same_v<T, bool>) return d.fixedResolutionEnabled;
            }
            else if (propertyName == "fixed_resolution_width") {
                if constexpr (std::is_same_v<T, int>) return (int)d.fixedResolutionWidth;
            }
            else if (propertyName == "fixed_resolution_height") {
                if constexpr (std::is_same_v<T, int>) return (int)d.fixedResolutionHeight;
            }
            else if (propertyName == "fixed_resolution_filter") {
                if constexpr (std::is_same_v<T, TextureFilter>) return d.fixedResolutionFilter;
            }
            else if (propertyName == "default_mesh_shader") {
                if constexpr (std::is_same_v<T, std::string>) return d.defaultMeshShader;
            }
            else if (propertyName == "default_ui_shader") {
                if constexpr (std::is_same_v<T, std::string>) return d.defaultUIShader;
            }
            else if (propertyName == "default_sky_shader") {
                if constexpr (std::is_same_v<T, std::string>) return d.defaultSkyShader;
            }
            else if (propertyName == "default_points_shader") {
                if constexpr (std::is_same_v<T, std::string>) return d.defaultPointsShader;
            }
            else if (propertyName == "default_lines_shader") {
                if constexpr (std::is_same_v<T, std::string>) return d.defaultLinesShader;
            }
            else if (propertyName == "post_process") {
                if constexpr (std::is_same_v<T, std::vector<PostProcessPass>>) return d.postProcess;
            }

            // Unknown property (or type mismatch): fall back to the type-generic default.
            return getSceneProperty<T>(nullptr, propertyName);
        }

        template<typename T>
        static void setSceneProperty(Scene* scene, const std::string& propertyName, const T& value) {
            if (!scene) return;

            if (propertyName == "background_color") {
                if constexpr (std::is_same_v<T, Vector4>) {
                    scene->setBackgroundColor(value);
                }
            }
            else if (propertyName == "shadows_quality") {
                if constexpr (std::is_same_v<T, ShadowQuality>) {
                    scene->setShadowQuality(value);
                }
            }
            else if (propertyName == "global_illumination_color") {
                if constexpr (std::is_same_v<T, Vector3>) {
                    scene->setGlobalIllumination(value);
                }
            }
            else if (propertyName == "global_illumination_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setGlobalIllumination(value);
                }
            }
            else if (propertyName == "ambient_light_2d_color") {
                if constexpr (std::is_same_v<T, Vector3>) {
                    scene->setAmbientLight2D(value);
                }
            }
            else if (propertyName == "ambient_light_2d_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setAmbientLight2D(value);
                }
            }
            else if (propertyName == "shadows_2d_quality") {
                if constexpr (std::is_same_v<T, ShadowQuality>) {
                    scene->setShadow2DQuality(value);
                }
            }
            else if (propertyName == "physics_gravity_2d") {
                if constexpr (std::is_same_v<T, Vector2>) {
                    scene->setGravity2D(value);
                }
            }
            else if (propertyName == "physics_gravity_3d") {
                if constexpr (std::is_same_v<T, Vector3>) {
                    scene->setGravity3D(value);
                }
            }
            else if (propertyName == "light_state") {
                if constexpr (std::is_same_v<T, LightState>) {
                    scene->setLightState(value);
                }
            }
            else if (propertyName == "ssao_enabled") {
                if constexpr (std::is_same_v<T, bool>) {
                    scene->setSSAOEnabled(value);
                }
            }
            else if (propertyName == "ssao_radius") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSAORadius(value);
                }
            }
            else if (propertyName == "ssao_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSAOIntensity(value);
                }
            }
            else if (propertyName == "ssao_bias") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSAOBias(value);
                }
            }
            else if (propertyName == "ssao_debug") {
                if constexpr (std::is_same_v<T, bool>) {
                    scene->setSSAODebug(value);
                }
            }
            else if (propertyName == "ssr_enabled") {
                if constexpr (std::is_same_v<T, bool>) {
                    scene->setSSREnabled(value);
                }
            }
            else if (propertyName == "ssr_max_distance") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSRMaxDistance(value);
                }
            }
            else if (propertyName == "ssr_thickness") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSRThickness(value);
                }
            }
            else if (propertyName == "ssr_intensity") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSRIntensity(value);
                }
            }
            else if (propertyName == "ssr_blur") {
                if constexpr (std::is_same_v<T, float>) {
                    scene->setSSRBlur(value);
                }
            }
            else if (propertyName == "ssr_max_steps") {
                if constexpr (std::is_same_v<T, int>) {
                    scene->setSSRMaxSteps(value);
                }
            }
            else if (propertyName == "ssr_debug_mode") {
                if constexpr (std::is_same_v<T, int>) {
                    scene->setSSRDebugMode(value);
                }
            }
            else if (propertyName == "fixed_resolution_enabled") {
                if constexpr (std::is_same_v<T, bool>) {
                    scene->setFixedResolutionEnabled(value);
                }
            }
            else if (propertyName == "fixed_resolution_width") {
                if constexpr (std::is_same_v<T, int>) {
                    scene->setFixedResolutionWidth((unsigned int)std::max(1, (int)value));
                }
            }
            else if (propertyName == "fixed_resolution_height") {
                if constexpr (std::is_same_v<T, int>) {
                    scene->setFixedResolutionHeight((unsigned int)std::max(1, (int)value));
                }
            }
            else if (propertyName == "fixed_resolution_filter") {
                if constexpr (std::is_same_v<T, TextureFilter>) {
                    scene->setFixedResolutionFilter(value);
                }
            }
            else if (propertyName == "default_mesh_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    scene->setDefaultMeshShader(value);
                }
            }
            else if (propertyName == "default_ui_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    scene->setDefaultUIShader(value);
                }
            }
            else if (propertyName == "default_sky_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    scene->setDefaultSkyShader(value);
                }
            }
            else if (propertyName == "default_points_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    scene->setDefaultPointsShader(value);
                }
            }
            else if (propertyName == "default_lines_shader") {
                if constexpr (std::is_same_v<T, std::string>) {
                    scene->setDefaultLinesShader(value);
                }
            }
            else if (propertyName == "post_process") {
                if constexpr (std::is_same_v<T, std::vector<PostProcessPass>>) {
                    scene->setPostProcessPasses(value);
                }
            }
        }
    };

}
