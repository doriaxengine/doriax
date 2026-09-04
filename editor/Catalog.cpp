// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "Catalog.h"

#include "Stream.h"

#include <cctype>

namespace {

    using namespace doriax;
    using namespace doriax::editor;

    using FastDefGetter = void* (*)();
    using FastRefGetter = void* (*)(void*);

    struct FastPropertyDescriptor {
        const char* name;
        PropertyType type;
        uint64_t updateFlags;
        FastDefGetter getDef;
        FastRefGetter getRef;
    };

    using FastResolverFn = PropertyData (*)(void*, const std::string&);
    using FastEnumerateFn = void (*)(void*, std::map<std::string, PropertyData>&);
    using FastFindComponentFn = void* (*)(EntityRegistry*, Entity);

    struct FastComponentResolver {
        ComponentType component;
        FastFindComponentFn findComponent;
        FastResolverFn resolve;
        FastEnumerateFn enumerate;
    };

    void* nullPropertyPtr() {
        return nullptr;
    }

    template<typename Component>
    Component& getDefaultComponent() {
        static Component component{};
        return component;
    }

    template<typename Component, typename Member, Member Component::*MemberPtr>
    void* getMemberDef() {
        return &(getDefaultComponent<Component>().*MemberPtr);
    }

    template<typename Component, typename Member, Member Component::*MemberPtr>
    void* getMemberRef(void* comp) {
        return &(static_cast<Component*>(comp)->*MemberPtr);
    }

    template<typename Component, typename Member, Member Component::*MemberPtr>
    constexpr FastPropertyDescriptor makeFastProperty(const char* name, PropertyType type, uint64_t updateFlags) {
        return {name, type, updateFlags, &getMemberDef<Component, Member, MemberPtr>, &getMemberRef<Component, Member, MemberPtr>};
    }

    template<typename Component, typename Member, Member Component::*MemberPtr>
    constexpr FastPropertyDescriptor makeFastPropertyNoDefault(const char* name, PropertyType type, uint64_t updateFlags) {
        return {name, type, updateFlags, &nullPropertyPtr, &getMemberRef<Component, Member, MemberPtr>};
    }

    constexpr FastPropertyDescriptor makeCustomProperty(const char* name, PropertyType type, uint64_t updateFlags, FastDefGetter getDef, FastRefGetter getRef) {
        return {name, type, updateFlags, getDef, getRef};
    }

    template<typename Component, size_t N>
    PropertyData resolveDirectProperties(Component* comp, const std::string& propertyName, const FastPropertyDescriptor (&descriptors)[N]) {
        if (!comp) {
            return PropertyData();
        }

        for (const FastPropertyDescriptor& descriptor : descriptors) {
            if (propertyName == descriptor.name) {
                return {
                    descriptor.type,
                    descriptor.updateFlags,
                    descriptor.getDef ? descriptor.getDef() : nullptr,
                    descriptor.getRef ? descriptor.getRef(comp) : nullptr
                };
            }
        }

        return PropertyData();
    }

    template<size_t N>
    void enumerateFromDescriptors(void* comp, std::map<std::string, PropertyData>& ps,
                                  const FastPropertyDescriptor (&descriptors)[N]) {
        for (const auto& desc : descriptors) {
            ps[desc.name] = {
                desc.type,
                desc.updateFlags,
                desc.getDef ? desc.getDef() : nullptr,
                (comp && desc.getRef) ? desc.getRef(comp) : nullptr
            };
        }
    }

    template<typename Component>
    void* findComponentPtr(EntityRegistry* registry, Entity entity) {
        return registry->findComponent<Component>(entity);
    }

    bool parseIndex(const std::string& text, size_t& pos, size_t& value) {
        if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
            return false;
        }

        size_t parsedValue = 0;
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            parsedValue = parsedValue * 10 + static_cast<size_t>(text[pos] - '0');
            pos++;
        }

        value = parsedValue;
        return true;
    }

    // Submesh properties the model loaders rewrite, and the bit that keeps an edit across a
    // reload. textureRect is absent on purpose: no loader touches it.
    struct SubmeshOverrideField {
        const char* name;
        uint32_t fields;
    };

    const SubmeshOverrideField submeshOverrideFields[] = {
        {"material", SubmeshOverride_Material},
        {"material.name", SubmeshOverride_MaterialName},
        {"material.baseColorFactor", SubmeshOverride_BaseColorFactor},
        {"material.metallicFactor", SubmeshOverride_MetallicFactor},
        {"material.roughnessFactor", SubmeshOverride_RoughnessFactor},
        {"material.alphaCutoff", SubmeshOverride_AlphaCutoff},
        {"material.alphaMode", SubmeshOverride_AlphaMode},
        {"material.emissiveFactor", SubmeshOverride_EmissiveFactor},
        {"material.baseColorTexture", SubmeshOverride_BaseColorTexture},
        {"material.emissiveTexture", SubmeshOverride_EmissiveTexture},
        {"material.metallicRoughnessTexture", SubmeshOverride_MetallicRoughnessTexture},
        {"material.occlusionTexture", SubmeshOverride_OcclusionTexture},
        {"material.normalTexture", SubmeshOverride_NormalTexture},
        {"faceCulling", SubmeshOverride_FaceCulling},
        {"textureShadow", SubmeshOverride_TextureShadow},
        {"primitiveType", SubmeshOverride_PrimitiveType}
    };

    // Splits "submeshes[N].<field>" into the submesh it addresses and the override bits it writes.
    bool parseSubmeshOverrideProperty(const std::string& propertyName, size_t& submeshIndex, uint32_t& fields) {
        if (propertyName.compare(0, 10, "submeshes[") != 0) {
            return false;
        }

        size_t pos = 10;
        if (!parseIndex(propertyName, pos, submeshIndex) || (pos + 1) >= propertyName.size() ||
                propertyName[pos] != ']' || propertyName[pos + 1] != '.') {
            return false;
        }

        const std::string field = propertyName.substr(pos + 2);
        for (const auto& entry : submeshOverrideFields) {
            if (field == entry.name) {
                fields = entry.fields;
                return true;
            }
        }

        return false;
    }

    void* getLightShadowNearRef(void* comp) {
        return &static_cast<LightComponent*>(comp)->shadowCameraNearFar.x;
    }

    void* getLightShadowFarRef(void* comp) {
        return &static_cast<LightComponent*>(comp)->shadowCameraNearFar.y;
    }

    static const FastPropertyDescriptor kTransformProperties[] = {
        makeFastProperty<Transform, Vector3, &Transform::position>("position", PropertyType::Vector3, UpdateFlags_Transform | UpdateFlags_Layout_Anchors),
        makeFastProperty<Transform, Quaternion, &Transform::rotation>("rotation", PropertyType::Quat, UpdateFlags_Transform),
        makeFastProperty<Transform, Vector3, &Transform::scale>("scale", PropertyType::Vector3, UpdateFlags_Transform | UpdateFlags_Body2D | UpdateFlags_Body3D),
        makeFastProperty<Transform, bool, &Transform::visible>("visible", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<Transform, bool, &Transform::billboard>("billboard", PropertyType::Bool, UpdateFlags_Transform),
        makeFastProperty<Transform, bool, &Transform::fakeBillboard>("fakeBillboard", PropertyType::Bool, UpdateFlags_Transform),
        makeFastProperty<Transform, bool, &Transform::cylindricalBillboard>("cylindricalBillboard", PropertyType::Bool, UpdateFlags_Transform),
        makeFastProperty<Transform, Quaternion, &Transform::billboardRotation>("billboardRotation", PropertyType::Quat, UpdateFlags_Transform),
    };

    static const FastPropertyDescriptor kUIProperties[] = {
        makeFastProperty<UIComponent, Vector4, &UIComponent::color>("color", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<UIComponent, Texture, &UIComponent::texture>("texture", PropertyType::Texture, UpdateFlags_UI_Texture),
        makeFastProperty<UIComponent, std::string, &UIComponent::customShader>("customShader", PropertyType::String, UpdateFlags_Shader_Reload),
    };

    static const FastPropertyDescriptor kUILayoutProperties[] = {
        makeFastPropertyNoDefault<UILayoutComponent, unsigned int, &UILayoutComponent::width>("width", PropertyType::UInt, UpdateFlags_Layout_Sizes | UpdateFlags_Layout_Anchors),
        makeFastPropertyNoDefault<UILayoutComponent, unsigned int, &UILayoutComponent::height>("height", PropertyType::UInt, UpdateFlags_Layout_Sizes | UpdateFlags_Layout_Anchors),
        makeFastProperty<UILayoutComponent, float, &UILayoutComponent::anchorPointLeft>("anchorPointLeft", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, float, &UILayoutComponent::anchorPointTop>("anchorPointTop", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, float, &UILayoutComponent::anchorPointRight>("anchorPointRight", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, float, &UILayoutComponent::anchorPointBottom>("anchorPointBottom", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, int, &UILayoutComponent::anchorOffsetLeft>("anchorOffsetLeft", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, int, &UILayoutComponent::anchorOffsetTop>("anchorOffsetTop", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, int, &UILayoutComponent::anchorOffsetRight>("anchorOffsetRight", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, int, &UILayoutComponent::anchorOffsetBottom>("anchorOffsetBottom", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, Vector2, &UILayoutComponent::positionOffset>("positionOffset", PropertyType::Vector2, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, AnchorPreset, &UILayoutComponent::anchorPreset>("anchorPreset", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, bool, &UILayoutComponent::usingAnchors>("usingAnchors", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, bool, &UILayoutComponent::ignoreScissor>("ignoreScissor", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<UILayoutComponent, bool, &UILayoutComponent::ignoreEvents>("ignoreEvents", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kImageProperties[] = {
        makeFastProperty<ImageComponent, unsigned int, &ImageComponent::patchMarginLeft>("patchMarginLeft", PropertyType::UInt, UpdateFlags_Image_Patches),
        makeFastProperty<ImageComponent, unsigned int, &ImageComponent::patchMarginRight>("patchMarginRight", PropertyType::UInt, UpdateFlags_Image_Patches),
        makeFastProperty<ImageComponent, unsigned int, &ImageComponent::patchMarginTop>("patchMarginTop", PropertyType::UInt, UpdateFlags_Image_Patches),
        makeFastProperty<ImageComponent, unsigned int, &ImageComponent::patchMarginBottom>("patchMarginBottom", PropertyType::UInt, UpdateFlags_Image_Patches),
        makeFastProperty<ImageComponent, float, &ImageComponent::textureScaleFactor>("textureScaleFactor", PropertyType::Float, UpdateFlags_Image_Patches),
    };

    static const FastPropertyDescriptor kButtonProperties[] = {
        makeFastProperty<ButtonComponent, Entity, &ButtonComponent::label>("label", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Texture, &ButtonComponent::textureNormal>("textureNormal", PropertyType::Texture, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Texture, &ButtonComponent::textureHovered>("textureHovered", PropertyType::Texture, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Texture, &ButtonComponent::texturePressed>("texturePressed", PropertyType::Texture, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Texture, &ButtonComponent::textureDisabled>("textureDisabled", PropertyType::Texture, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorNormal>("colorNormal", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorHovered>("colorHovered", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorPressed>("colorPressed", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorDisabled>("colorDisabled", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, bool, &ButtonComponent::disabled>("disabled", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kScrollbarProperties[] = {
        makeFastProperty<ScrollbarComponent, Entity, &ScrollbarComponent::bar>("bar", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, ScrollbarType, &ScrollbarComponent::type>("type", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, float, &ScrollbarComponent::barSize>("barSize", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, float, &ScrollbarComponent::step>("step", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, int, &ScrollbarComponent::barMarginLeft>("barMarginLeft", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, int, &ScrollbarComponent::barMarginRight>("barMarginRight", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, int, &ScrollbarComponent::barMarginTop>("barMarginTop", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<ScrollbarComponent, int, &ScrollbarComponent::barMarginBottom>("barMarginBottom", PropertyType::Int, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kProgressbarProperties[] = {
        makeFastProperty<ProgressbarComponent, Entity, &ProgressbarComponent::fill>("fill", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<ProgressbarComponent, ProgressbarType, &ProgressbarComponent::type>("type", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<ProgressbarComponent, float, &ProgressbarComponent::value>("value", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<ProgressbarComponent, int, &ProgressbarComponent::fillMarginLeft>("fillMarginLeft", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<ProgressbarComponent, int, &ProgressbarComponent::fillMarginRight>("fillMarginRight", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<ProgressbarComponent, int, &ProgressbarComponent::fillMarginTop>("fillMarginTop", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<ProgressbarComponent, int, &ProgressbarComponent::fillMarginBottom>("fillMarginBottom", PropertyType::Int, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kPanelProperties[] = {
        makeFastProperty<PanelComponent, Entity, &PanelComponent::headerimage>("headerimage", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<PanelComponent, Entity, &PanelComponent::headercontainer>("headercontainer", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<PanelComponent, Entity, &PanelComponent::headertext>("headertext", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<PanelComponent, AnchorPreset, &PanelComponent::titleAnchorPreset>("titleAnchorPreset", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<PanelComponent, unsigned int, &PanelComponent::minWidth>("minWidth", PropertyType::UInt, UpdateFlags_None),
        makeFastProperty<PanelComponent, unsigned int, &PanelComponent::minHeight>("minHeight", PropertyType::UInt, UpdateFlags_None),
        makeFastProperty<PanelComponent, int, &PanelComponent::headerMarginLeft>("headerMarginLeft", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<PanelComponent, int, &PanelComponent::headerMarginTop>("headerMarginTop", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<PanelComponent, int, &PanelComponent::headerMarginRight>("headerMarginRight", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<PanelComponent, int, &PanelComponent::headerMarginBottom>("headerMarginBottom", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<PanelComponent, bool, &PanelComponent::defaultHeaderMargin>("defaultHeaderMargin", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<PanelComponent, int, &PanelComponent::resizeMargin>("resizeMargin", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<PanelComponent, bool, &PanelComponent::canMove>("canMove", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<PanelComponent, bool, &PanelComponent::canResize>("canResize", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<PanelComponent, bool, &PanelComponent::canBringToFront>("canBringToFront", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kTextEditProperties[] = {
        makeFastProperty<TextEditComponent, Entity, &TextEditComponent::text>("text", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<TextEditComponent, Entity, &TextEditComponent::selection>("selection", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<TextEditComponent, Entity, &TextEditComponent::cursor>("cursor", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<TextEditComponent, float, &TextEditComponent::cursorBlink>("cursorBlink", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<TextEditComponent, float, &TextEditComponent::cursorWidth>("cursorWidth", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<TextEditComponent, Vector4, &TextEditComponent::cursorColor>("cursorColor", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<TextEditComponent, Vector4, &TextEditComponent::selectionColor>("selectionColor", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<TextEditComponent, Vector4, &TextEditComponent::placeholderColor>("placeholderColor", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<TextEditComponent, std::string, &TextEditComponent::placeholder>("placeholder", PropertyType::String, UpdateFlags_None),
        makeFastProperty<TextEditComponent, int, &TextEditComponent::cursorIndex>("cursorIndex", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<TextEditComponent, int, &TextEditComponent::selectionAnchor>("selectionAnchor", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<TextEditComponent, bool, &TextEditComponent::disabled>("disabled", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<TextEditComponent, bool, &TextEditComponent::password>("password", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kSpriteTopProperties[] = {
        makeFastPropertyNoDefault<SpriteComponent, unsigned int, &SpriteComponent::width>("width", PropertyType::UInt, UpdateFlags_Sprite),
        makeFastPropertyNoDefault<SpriteComponent, unsigned int, &SpriteComponent::height>("height", PropertyType::UInt, UpdateFlags_Sprite),
        makeFastProperty<SpriteComponent, PivotPreset, &SpriteComponent::pivotPreset>("pivotPreset", PropertyType::Enum, UpdateFlags_Sprite),
        makeFastProperty<SpriteComponent, float, &SpriteComponent::textureScaleFactor>("textureScaleFactor", PropertyType::Float, UpdateFlags_Sprite),
        makeFastProperty<SpriteComponent, bool, &SpriteComponent::automaticFlipY>("automaticFlipY", PropertyType::Bool, UpdateFlags_Sprite),
        makeFastProperty<SpriteComponent, bool, &SpriteComponent::flipY>("flipY", PropertyType::Bool, UpdateFlags_Sprite),
    };

    static const FastPropertyDescriptor kTilemapTopProperties[] = {
        makeFastPropertyNoDefault<TilemapComponent, unsigned int, &TilemapComponent::width>("width", PropertyType::UInt, UpdateFlags_Tilemap),
        makeFastPropertyNoDefault<TilemapComponent, unsigned int, &TilemapComponent::height>("height", PropertyType::UInt, UpdateFlags_Tilemap),
        makeFastProperty<TilemapComponent, bool, &TilemapComponent::automaticFlipY>("automaticFlipY", PropertyType::Bool, UpdateFlags_Tilemap),
        makeFastProperty<TilemapComponent, bool, &TilemapComponent::flipY>("flipY", PropertyType::Bool, UpdateFlags_Tilemap),
        makeFastProperty<TilemapComponent, float, &TilemapComponent::textureScaleFactor>("textureScaleFactor", PropertyType::Float, UpdateFlags_Tilemap),
        makeFastProperty<TilemapComponent, unsigned int, &TilemapComponent::reserveTiles>("reserveTiles", PropertyType::UInt, UpdateFlags_Tilemap),
    };

    static const FastPropertyDescriptor kMeshPolygonTopProperties[] = {
        makeFastProperty<MeshPolygonComponent, bool, &MeshPolygonComponent::automaticFlipY>("automaticFlipY", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshPolygonComponent, bool, &MeshPolygonComponent::flipY>("flipY", PropertyType::Bool, UpdateFlags_Mesh_Reload),
    };

    static const FastPropertyDescriptor kTerrainProperties[] = {
        makeFastProperty<TerrainComponent, Texture, &TerrainComponent::heightMap>("heightMap", PropertyType::Texture, UpdateFlags_Terrain | UpdateFlags_Terrain_Texture | UpdateFlags_Terrain_Foliage),
        makeFastProperty<TerrainComponent, Texture, &TerrainComponent::blendMap>("blendMap", PropertyType::Texture, UpdateFlags_Terrain_Texture),
        makeFastProperty<TerrainComponent, Texture, &TerrainComponent::textureDetailRed>("textureDetailRed", PropertyType::Texture, UpdateFlags_Terrain_Texture),
        makeFastProperty<TerrainComponent, Texture, &TerrainComponent::textureDetailGreen>("textureDetailGreen", PropertyType::Texture, UpdateFlags_Terrain_Texture),
        makeFastProperty<TerrainComponent, Texture, &TerrainComponent::textureDetailBlue>("textureDetailBlue", PropertyType::Texture, UpdateFlags_Terrain_Texture),
        makeFastProperty<TerrainComponent, bool, &TerrainComponent::autoSetRanges>("autoSetRanges", PropertyType::Bool, UpdateFlags_Terrain),
        makeFastProperty<TerrainComponent, Vector2, &TerrainComponent::offset>("offset", PropertyType::Vector2, UpdateFlags_None),
        makeFastProperty<TerrainComponent, float, &TerrainComponent::terrainSize>("terrainSize", PropertyType::Float, UpdateFlags_Terrain | UpdateFlags_Terrain_Foliage),
        makeFastProperty<TerrainComponent, float, &TerrainComponent::maxHeight>("maxHeight", PropertyType::Float, UpdateFlags_Terrain | UpdateFlags_Terrain_Foliage),
        makeFastProperty<TerrainComponent, float, &TerrainComponent::resolution>("resolution", PropertyType::Float, UpdateFlags_Terrain | UpdateFlags_Mesh_Reload),
        makeFastProperty<TerrainComponent, float, &TerrainComponent::textureBaseTiles>("textureBaseTiles", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<TerrainComponent, float, &TerrainComponent::textureDetailTiles>("textureDetailTiles", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<TerrainComponent, int, &TerrainComponent::rootGridSize>("rootGridSize", PropertyType::Int, UpdateFlags_Terrain | UpdateFlags_Mesh_Reload),
        makeFastProperty<TerrainComponent, int, &TerrainComponent::levels>("levels", PropertyType::Int, UpdateFlags_Terrain | UpdateFlags_Mesh_Reload),
    };

    static const FastPropertyDescriptor kActionProperties[] = {
        makeFastPropertyNoDefault<ActionComponent, ActionState, &ActionComponent::state>("state", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<ActionComponent, float, &ActionComponent::speed>("speed", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<ActionComponent, Entity, &ActionComponent::target>("target", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<ActionComponent, bool, &ActionComponent::ownedTarget>("ownedTarget", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kTimedActionProperties[] = {
        makeFastProperty<TimedActionComponent, float, &TimedActionComponent::duration>("duration", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<TimedActionComponent, bool, &TimedActionComponent::loop>("loop", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<TimedActionComponent, Ease, &TimedActionComponent::function>("function", PropertyType::Ease, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kPositionActionProperties[] = {
        makeFastPropertyNoDefault<PositionActionComponent, Vector3, &PositionActionComponent::endPosition>("endPosition", PropertyType::Vector3, UpdateFlags_None),
        makeFastPropertyNoDefault<PositionActionComponent, Vector3, &PositionActionComponent::startPosition>("startPosition", PropertyType::Vector3, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kRotationActionProperties[] = {
        makeFastPropertyNoDefault<RotationActionComponent, Quaternion, &RotationActionComponent::endRotation>("endRotation", PropertyType::Quat, UpdateFlags_None),
        makeFastPropertyNoDefault<RotationActionComponent, Quaternion, &RotationActionComponent::startRotation>("startRotation", PropertyType::Quat, UpdateFlags_None),
        makeFastProperty<RotationActionComponent, bool, &RotationActionComponent::shortestPath>("shortestPath", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kScaleActionProperties[] = {
        makeFastPropertyNoDefault<ScaleActionComponent, Vector3, &ScaleActionComponent::endScale>("endScale", PropertyType::Vector3, UpdateFlags_None),
        makeFastPropertyNoDefault<ScaleActionComponent, Vector3, &ScaleActionComponent::startScale>("startScale", PropertyType::Vector3, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kColorActionProperties[] = {
        makeFastPropertyNoDefault<ColorActionComponent, Vector3, &ColorActionComponent::endColor>("endColor", PropertyType::Vector3, UpdateFlags_None),
        makeFastPropertyNoDefault<ColorActionComponent, Vector3, &ColorActionComponent::startColor>("startColor", PropertyType::Vector3, UpdateFlags_None),
        makeFastProperty<ColorActionComponent, bool, &ColorActionComponent::useSRGB>("useSRGB", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kAlphaActionProperties[] = {
        makeFastPropertyNoDefault<AlphaActionComponent, float, &AlphaActionComponent::endAlpha>("endAlpha", PropertyType::Float, UpdateFlags_None),
        makeFastPropertyNoDefault<AlphaActionComponent, float, &AlphaActionComponent::startAlpha>("startAlpha", PropertyType::Float, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kBundleProperties[] = {
        makeFastProperty<BundleComponent, std::string, &BundleComponent::name>("name", PropertyType::String, UpdateFlags_None),
        makeFastProperty<BundleComponent, std::string, &BundleComponent::path>("path", PropertyType::String, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kBoneProperties[] = {
        makeFastPropertyNoDefault<BoneComponent, Entity, &BoneComponent::model>("model", PropertyType::Entity, UpdateFlags_None),
        makeFastPropertyNoDefault<BoneComponent, int, &BoneComponent::index>("index", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<BoneComponent, Vector3, &BoneComponent::bindPosition>("bindPosition", PropertyType::Vector3, UpdateFlags_None),
        makeFastProperty<BoneComponent, Quaternion, &BoneComponent::bindRotation>("bindRotation", PropertyType::Quat, UpdateFlags_None),
        makeFastProperty<BoneComponent, Vector3, &BoneComponent::bindScale>("bindScale", PropertyType::Vector3, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kKeyframeTracksProperties[] = {
        makeFastPropertyNoDefault<KeyframeTracksComponent, int, &KeyframeTracksComponent::index>("index", PropertyType::Int, UpdateFlags_None),
        makeFastProperty<KeyframeTracksComponent, float, &KeyframeTracksComponent::interpolation>("interpolation", PropertyType::Float, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kSpriteAnimationProperties[] = {
        makeFastProperty<SpriteAnimationComponent, bool, &SpriteAnimationComponent::loop>("loop", PropertyType::Bool, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, unsigned int, &SpriteAnimationComponent::framesSize>("framesSize", PropertyType::UInt, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, unsigned int, &SpriteAnimationComponent::framesTimeSize>("framesTimeSize", PropertyType::UInt, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, int, &SpriteAnimationComponent::frameIndex>("frameIndex", PropertyType::Int, UpdateFlags_Sprite),
        makeFastPropertyNoDefault<SpriteAnimationComponent, int, &SpriteAnimationComponent::frameTimeIndex>("frameTimeIndex", PropertyType::Int, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, unsigned int, &SpriteAnimationComponent::spriteFrameCount>("spriteFrameCount", PropertyType::UInt, UpdateFlags_Sprite),
    };

    static const FastPropertyDescriptor kAnimationProperties[] = {
        makeFastProperty<AnimationComponent, bool, &AnimationComponent::loop>("loop", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<AnimationComponent, float, &AnimationComponent::duration>("duration", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<AnimationComponent, bool, &AnimationComponent::ownedActions>("ownedActions", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<AnimationComponent, float, &AnimationComponent::defaultFadeTime>("defaultFadeTime", PropertyType::Float, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kLightProperties[] = {
        makeFastPropertyNoDefault<LightComponent, LightType, &LightComponent::type>("type", PropertyType::Enum, UpdateFlags_LightShadowMap | UpdateFlags_LightShadowCamera | UpdateFlags_Scene_Mesh_Reload),
        makeFastProperty<LightComponent, Vector3, &LightComponent::direction>("direction", PropertyType::Vector3, UpdateFlags_Transform),
        makeFastProperty<LightComponent, bool, &LightComponent::shadows>("shadows", PropertyType::Bool, UpdateFlags_LightShadowCamera | UpdateFlags_Scene_Mesh_Reload),
        makeFastProperty<LightComponent, float, &LightComponent::intensity>("intensity", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<LightComponent, float, &LightComponent::range>("range", PropertyType::Float, UpdateFlags_LightShadowCamera),
        makeFastProperty<LightComponent, Vector3, &LightComponent::color>("color", PropertyType::Vector3, UpdateFlags_None),
        makeFastProperty<LightComponent, float, &LightComponent::innerConeCos>("innerConeCos", PropertyType::Float, UpdateFlags_LightShadowCamera),
        makeFastProperty<LightComponent, float, &LightComponent::outerConeCos>("outerConeCos", PropertyType::Float, UpdateFlags_LightShadowCamera),
        makeFastProperty<LightComponent, Texture, &LightComponent::spotMask>("spotMask", PropertyType::Texture, UpdateFlags_LightShadowCamera),
        makeFastProperty<LightComponent, float, &LightComponent::shadowBias>("shadowBias", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<LightComponent, unsigned int, &LightComponent::mapResolution>("mapResolution", PropertyType::UInt, UpdateFlags_LightShadowMap | UpdateFlags_Scene_Mesh_Reload),
        makeFastProperty<LightComponent, bool, &LightComponent::automaticShadowCamera>("automaticShadowCamera", PropertyType::Bool, UpdateFlags_LightShadowCamera),
        makeCustomProperty("shadowCameraNear", PropertyType::Float, UpdateFlags_LightShadowCamera, &nullPropertyPtr, &getLightShadowNearRef),
        makeCustomProperty("shadowCameraFar", PropertyType::Float, UpdateFlags_LightShadowCamera, &nullPropertyPtr, &getLightShadowFarRef),
        makeFastProperty<LightComponent, unsigned int, &LightComponent::numShadowCascades>("numShadowCascades", PropertyType::UInt, UpdateFlags_LightShadowCamera | UpdateFlags_Scene_Mesh_Reload),
    };

    static const FastPropertyDescriptor kLight2DProperties[] = {
        makeFastProperty<Light2DComponent, Vector3, &Light2DComponent::color>("color", PropertyType::Vector3, UpdateFlags_None),
        makeFastProperty<Light2DComponent, float, &Light2DComponent::intensity>("intensity", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<Light2DComponent, float, &Light2DComponent::range>("range", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<Light2DComponent, float, &Light2DComponent::falloff>("falloff", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<Light2DComponent, float, &Light2DComponent::height>("height", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<Light2DComponent, bool, &Light2DComponent::shadows>("shadows", PropertyType::Bool, UpdateFlags_Scene_Mesh_Reload),
        makeFastProperty<Light2DComponent, float, &Light2DComponent::shadowBias>("shadowBias", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<Light2DComponent, float, &Light2DComponent::shadowSoftness>("shadowSoftness", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<Light2DComponent, unsigned int, &Light2DComponent::mapResolution>("mapResolution", PropertyType::UInt, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kOccluder2DProperties[] = {
        makeFastProperty<Occluder2DComponent, Occluder2DShape, &Occluder2DComponent::shape>("shape", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<Occluder2DComponent, bool, &Occluder2DComponent::closed>("closed", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<Occluder2DComponent, bool, &Occluder2DComponent::enabled>("enabled", PropertyType::Bool, UpdateFlags_None),
        makeFastPropertyNoDefault<Occluder2DComponent, std::vector<Vector2>, &Occluder2DComponent::points>("points", PropertyType::Custom, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kFogProperties[] = {
        makeFastProperty<FogComponent, FogType, &FogComponent::type>("type", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<FogComponent, Vector3, &FogComponent::color>("color", PropertyType::Vector3, UpdateFlags_None),
        makeFastProperty<FogComponent, float, &FogComponent::density>("density", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<FogComponent, float, &FogComponent::linearStart>("linearStart", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<FogComponent, float, &FogComponent::linearEnd>("linearEnd", PropertyType::Float, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kMirrorProperties[] = {
        makeFastProperty<MirrorComponent, Vector3, &MirrorComponent::normal>("normal", PropertyType::Vector3, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kReflectionProbeProperties[] = {
        makeFastProperty<ReflectionProbeComponent, ReflectionProbeMode, &ReflectionProbeComponent::mode>("mode", PropertyType::Enum, UpdateFlags_Reflection_Probe_Capture),
        makeFastProperty<ReflectionProbeComponent, ReflectionProbeUpdateMode, &ReflectionProbeComponent::updateMode>("updateMode", PropertyType::Enum, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, Vector3, &ReflectionProbeComponent::boxOffset>("boxOffset", PropertyType::Vector3, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, Vector3, &ReflectionProbeComponent::boxSize>("boxSize", PropertyType::Vector3, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, float, &ReflectionProbeComponent::blendDistance>("blendDistance", PropertyType::Float, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, float, &ReflectionProbeComponent::intensity>("intensity", PropertyType::Float, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, int, &ReflectionProbeComponent::priority>("priority", PropertyType::Int, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, unsigned int, &ReflectionProbeComponent::resolution>("resolution", PropertyType::UInt, UpdateFlags_Reflection_Probe_Capture),
        makeFastProperty<ReflectionProbeComponent, float, &ReflectionProbeComponent::nearClip>("nearClip", PropertyType::Float, UpdateFlags_Reflection_Probe_Capture),
        makeFastProperty<ReflectionProbeComponent, float, &ReflectionProbeComponent::farClip>("farClip", PropertyType::Float, UpdateFlags_Reflection_Probe_Capture),
        makeFastProperty<ReflectionProbeComponent, float, &ReflectionProbeComponent::updateInterval>("updateInterval", PropertyType::Float, UpdateFlags_Reflection_Probe_Sampling),
        makeFastProperty<ReflectionProbeComponent, bool, &ReflectionProbeComponent::includeSky>("includeSky", PropertyType::Bool, UpdateFlags_Reflection_Probe_Capture),
        makeFastProperty<ReflectionProbeComponent, Texture, &ReflectionProbeComponent::texture>("texture", PropertyType::Texture, UpdateFlags_Reflection_Probe_Capture),
    };

    static const FastPropertyDescriptor kCameraProperties[] = {
        makeFastProperty<CameraComponent, CameraType, &CameraComponent::type>("type", PropertyType::Enum, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, Vector3, &CameraComponent::target>("target", PropertyType::Vector3, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, Vector3, &CameraComponent::up>("up", PropertyType::Vector3, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::leftClip>("left", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::rightClip>("right", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::bottomClip>("bottom", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::topClip>("top", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::yfov>("yfov", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::aspect>("aspect", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::nearClip>("near", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, float, &CameraComponent::farClip>("far", PropertyType::Float, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, bool, &CameraComponent::renderToTexture>("renderToTexture", PropertyType::Bool, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, bool, &CameraComponent::transparentSort>("transparentSort", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<CameraComponent, bool, &CameraComponent::useTarget>("useTarget", PropertyType::Bool, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, bool, &CameraComponent::autoResize>("autoResize", PropertyType::Bool, UpdateFlags_Camera),
        // framebuffer settings are applied by RenderSystem::update (no update flag needed)
        makeFastProperty<CameraComponent, unsigned int, &CameraComponent::framebufferWidth>("framebufferWidth", PropertyType::UInt, UpdateFlags_None),
        makeFastProperty<CameraComponent, unsigned int, &CameraComponent::framebufferHeight>("framebufferHeight", PropertyType::UInt, UpdateFlags_None),
        makeFastProperty<CameraComponent, TextureFilter, &CameraComponent::framebufferFilter>("framebufferFilter", PropertyType::Enum, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kSoundProperties[] = {
        makeFastProperty<SoundComponent, SoundState, &SoundComponent::state>("state", PropertyType::Enum, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, std::string, &SoundComponent::filename>("filename", PropertyType::String, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, bool, &SoundComponent::enableClocked>("enableClocked", PropertyType::Bool, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, double, &SoundComponent::volume>("volume", PropertyType::Double, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, float, &SoundComponent::speed>("speed", PropertyType::Float, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, float, &SoundComponent::pan>("pan", PropertyType::Float, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, bool, &SoundComponent::looping>("looping", PropertyType::Bool, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, double, &SoundComponent::loopingPoint>("loopingPoint", PropertyType::Double, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, bool, &SoundComponent::protectVoice>("protectVoice", PropertyType::Bool, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, bool, &SoundComponent::inaudibleBehaviorMustTick>("inaudibleBehaviorMustTick", PropertyType::Bool, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, bool, &SoundComponent::inaudibleBehaviorKill>("inaudibleBehaviorKill", PropertyType::Bool, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, float, &SoundComponent::minDistance>("minDistance", PropertyType::Float, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, float, &SoundComponent::maxDistance>("maxDistance", PropertyType::Float, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, SoundAttenuation, &SoundComponent::attenuationModel>("attenuationModel", PropertyType::Enum, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, float, &SoundComponent::attenuationRolloffFactor>("attenuationRolloffFactor", PropertyType::Float, UpdateFlags_Sound),
        makeFastProperty<SoundComponent, float, &SoundComponent::dopplerFactor>("dopplerFactor", PropertyType::Float, UpdateFlags_Sound),
        makeFastPropertyNoDefault<SoundComponent, double, &SoundComponent::length>("length", PropertyType::Double, UpdateFlags_None),
        makeFastPropertyNoDefault<SoundComponent, double, &SoundComponent::playingTime>("playingTime", PropertyType::Double, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kSkyProperties[] = {
        makeFastProperty<SkyComponent, Texture, &SkyComponent::texture>("texture", PropertyType::Texture, UpdateFlags_Sky_Texture),
        makeFastProperty<SkyComponent, Vector4, &SkyComponent::color>("color", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<SkyComponent, float, &SkyComponent::rotation>("rotation", PropertyType::Float, UpdateFlags_Sky),
        makeFastProperty<SkyComponent, bool, &SkyComponent::visible>("visible", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<SkyComponent, std::string, &SkyComponent::customShader>("customShader", PropertyType::String, UpdateFlags_Shader_Reload),
    };

    static const FastPropertyDescriptor kTextProperties[] = {
        makeFastProperty<TextComponent, std::string, &TextComponent::text>("text", PropertyType::String, UpdateFlags_Text),
        makeFastProperty<TextComponent, FontArray, &TextComponent::font>("font", PropertyType::Font, UpdateFlags_Text_Atlas),
        makeFastProperty<TextComponent, unsigned int, &TextComponent::fontSize>("fontSize", PropertyType::UInt, UpdateFlags_Text_Atlas),
        makeFastProperty<TextComponent, bool, &TextComponent::multiline>("multiline", PropertyType::Bool, UpdateFlags_Text),
        makeFastProperty<TextComponent, unsigned int, &TextComponent::maxTextSize>("maxTextSize", PropertyType::UInt, UpdateFlags_Text),
        makeFastProperty<TextComponent, bool, &TextComponent::fixedWidth>("fixedWidth", PropertyType::Bool, UpdateFlags_Text),
        makeFastProperty<TextComponent, bool, &TextComponent::fixedHeight>("fixedHeight", PropertyType::Bool, UpdateFlags_Text),
        makeFastProperty<TextComponent, bool, &TextComponent::pivotBaseline>("pivotBaseline", PropertyType::Bool, UpdateFlags_Text),
        makeFastProperty<TextComponent, bool, &TextComponent::pivotCentered>("pivotCentered", PropertyType::Bool, UpdateFlags_Text),
    };

    static const FastPropertyDescriptor kJoint2DProperties[] = {
        makeFastProperty<Joint2DComponent, Joint2DType, &Joint2DComponent::type>("type", PropertyType::Enum, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, Entity, &Joint2DComponent::bodyA>("bodyA", PropertyType::Entity, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, Entity, &Joint2DComponent::bodyB>("bodyB", PropertyType::Entity, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, Vector2, &Joint2DComponent::anchorA>("anchorA", PropertyType::Vector2, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, Vector2, &Joint2DComponent::anchorB>("anchorB", PropertyType::Vector2, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, Vector2, &Joint2DComponent::axis>("axis", PropertyType::Vector2, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, Vector2, &Joint2DComponent::target>("target", PropertyType::Vector2, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, bool, &Joint2DComponent::autoAnchors>("autoAnchors", PropertyType::Bool, UpdateFlags_Joint2D),
        makeFastProperty<Joint2DComponent, bool, &Joint2DComponent::rope>("rope", PropertyType::Bool, UpdateFlags_Joint2D),
    };

    static const FastPropertyDescriptor kJoint3DProperties[] = {
        makeFastProperty<Joint3DComponent, Joint3DType, &Joint3DComponent::type>("type", PropertyType::Enum, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Entity, &Joint3DComponent::bodyA>("bodyA", PropertyType::Entity, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Entity, &Joint3DComponent::bodyB>("bodyB", PropertyType::Entity, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::anchorA>("anchorA", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::anchorB>("anchorB", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::anchor>("anchor", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::axis>("axis", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::normal>("normal", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::twistAxis>("twistAxis", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::planeAxis>("planeAxis", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::axisX>("axisX", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::axisY>("axisY", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, float, &Joint3DComponent::limitsMin>("limitsMin", PropertyType::Float, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, float, &Joint3DComponent::limitsMax>("limitsMax", PropertyType::Float, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, float, &Joint3DComponent::normalHalfConeAngle>("normalHalfConeAngle", PropertyType::Float, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, float, &Joint3DComponent::planeHalfConeAngle>("planeHalfConeAngle", PropertyType::Float, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, float, &Joint3DComponent::twistMinAngle>("twistMinAngle", PropertyType::Float, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, float, &Joint3DComponent::twistMaxAngle>("twistMaxAngle", PropertyType::Float, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::fixedPointA>("fixedPointA", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::fixedPointB>("fixedPointB", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Entity, &Joint3DComponent::hingeA>("hingeA", PropertyType::Entity, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Entity, &Joint3DComponent::hingeB>("hingeB", PropertyType::Entity, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Entity, &Joint3DComponent::hinge>("hinge", PropertyType::Entity, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Entity, &Joint3DComponent::slider>("slider", PropertyType::Entity, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, int, &Joint3DComponent::numTeethGearA>("numTeethGearA", PropertyType::Int, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, int, &Joint3DComponent::numTeethGearB>("numTeethGearB", PropertyType::Int, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, int, &Joint3DComponent::numTeethRack>("numTeethRack", PropertyType::Int, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, int, &Joint3DComponent::numTeethGear>("numTeethGear", PropertyType::Int, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, int, &Joint3DComponent::rackLength>("rackLength", PropertyType::Int, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, Vector3, &Joint3DComponent::pathPosition>("pathPosition", PropertyType::Vector3, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, bool, &Joint3DComponent::isLooping>("isLooping", PropertyType::Bool, UpdateFlags_Joint3D),
        makeFastProperty<Joint3DComponent, bool, &Joint3DComponent::autoAnchors>("autoAnchors", PropertyType::Bool, UpdateFlags_Joint3D),
    };

    static const FastPropertyDescriptor kMeshTopProperties[] = {
        makeFastProperty<MeshComponent, bool, &MeshComponent::receiveIBL>("receiveIBL", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::castShadows>("castShadows", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::receiveShadows>("receiveShadows", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::renderInReflectionProbes>("renderInReflectionProbes", PropertyType::Bool, UpdateFlags_Reflection_Probe_Recapture_All),
        makeFastProperty<MeshComponent, bool, &MeshComponent::transparent>("transparent", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::autoTransparency>("autoTransparency", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, std::string, &MeshComponent::customShader>("customShader", PropertyType::String, UpdateFlags_Shader_Reload),
        makeFastProperty<MeshComponent, unsigned int, &MeshComponent::numSubmeshes>("numSubmeshes", PropertyType::UInt, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kUIContainerTopProperties[] = {
        makeFastProperty<UIContainerComponent, ContainerType, &UIContainerComponent::type>("type", PropertyType::Enum, UpdateFlags_Layout_Sizes),
        makeFastProperty<UIContainerComponent, bool, &UIContainerComponent::useAllWrapSpace>("useAllWrapSpace", PropertyType::Bool, UpdateFlags_Layout_Sizes),
        makeFastProperty<UIContainerComponent, unsigned int, &UIContainerComponent::wrapCellWidth>("wrapCellWidth", PropertyType::UInt, UpdateFlags_Layout_Sizes),
        makeFastProperty<UIContainerComponent, unsigned int, &UIContainerComponent::wrapCellHeight>("wrapCellHeight", PropertyType::UInt, UpdateFlags_Layout_Sizes),
        makeFastProperty<UIContainerComponent, unsigned int, &UIContainerComponent::numBoxes>("numBoxes", PropertyType::UInt, UpdateFlags_None),
    };

    PropertyData getMeshPropertyFast(MeshComponent* comp, const std::string& propertyName) {
        MeshComponent& def = getDefaultComponent<MeshComponent>();

        if (!comp) {
            return PropertyData();
        }

        // Flat properties
        PropertyData result = resolveDirectProperties(comp, propertyName, kMeshTopProperties);
        if (result.ref) return result;

        // Submeshes: submeshes[N].field
        if (propertyName.compare(0, 10, "submeshes[") != 0) {
            return PropertyData();
        }

        size_t pos = 10;
        size_t submeshIndex = 0;
        if (!parseIndex(propertyName, pos, submeshIndex) || pos >= propertyName.size() || propertyName[pos] != ']') {
            return PropertyData();
        }

        Submesh& sub = comp->submeshes[submeshIndex];
        Submesh& defSub = def.submeshes[0];

        pos++;
        if (pos == propertyName.size()) {
            return {PropertyType::Custom, UpdateFlags_Mesh_Texture, (void*)&defSub, (void*)&sub};
        }
        if (propertyName[pos] != '.') {
            return PropertyData();
        }

        const size_t fieldPos = pos + 1;

        // submeshes[N].material
        if (propertyName.compare(fieldPos, 8, "material") == 0) {
            if (fieldPos + 8 == propertyName.size()) {
                return {PropertyType::Material, UpdateFlags_Mesh_Texture, (void*)&defSub.material, (void*)&sub.material};
            }
            if (propertyName[fieldPos + 8] != '.') {
                return PropertyData();
            }
            const size_t matFieldPos = fieldPos + 9;
            if (propertyName.compare(matFieldPos, 4, "name") == 0 && matFieldPos + 4 == propertyName.size()) {
                return {PropertyType::String, UpdateFlags_None, (void*)&defSub.material.name, (void*)&sub.material.name};
            }
            if (propertyName.compare(matFieldPos, 15, "baseColorFactor") == 0 && matFieldPos + 15 == propertyName.size()) {
                return {PropertyType::Vector4, UpdateFlags_None, (void*)&defSub.material.baseColorFactor, (void*)&sub.material.baseColorFactor};
            }
            if (propertyName.compare(matFieldPos, 14, "metallicFactor") == 0 && matFieldPos + 14 == propertyName.size()) {
                return {PropertyType::Float, UpdateFlags_None, (void*)&defSub.material.metallicFactor, (void*)&sub.material.metallicFactor};
            }
            if (propertyName.compare(matFieldPos, 15, "roughnessFactor") == 0 && matFieldPos + 15 == propertyName.size()) {
                return {PropertyType::Float, UpdateFlags_None, (void*)&defSub.material.roughnessFactor, (void*)&sub.material.roughnessFactor};
            }
            if (propertyName.compare(matFieldPos, 11, "alphaCutoff") == 0 && matFieldPos + 11 == propertyName.size()) {
                return {PropertyType::Float, UpdateFlags_None, (void*)&defSub.material.alphaCutoff, (void*)&sub.material.alphaCutoff};
            }
            if (propertyName.compare(matFieldPos, 9, "alphaMode") == 0 && matFieldPos + 9 == propertyName.size()) {
                return {PropertyType::Enum, UpdateFlags_Mesh_Reload, (void*)&defSub.material.alphaMode, (void*)&sub.material.alphaMode};
            }
            if (propertyName.compare(matFieldPos, 14, "emissiveFactor") == 0 && matFieldPos + 14 == propertyName.size()) {
                return {PropertyType::Vector3, UpdateFlags_None, (void*)&defSub.material.emissiveFactor, (void*)&sub.material.emissiveFactor};
            }
            if (propertyName.compare(matFieldPos, 16, "baseColorTexture") == 0 && matFieldPos + 16 == propertyName.size()) {
                return {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.baseColorTexture, (void*)&sub.material.baseColorTexture};
            }
            if (propertyName.compare(matFieldPos, 15, "emissiveTexture") == 0 && matFieldPos + 15 == propertyName.size()) {
                return {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.emissiveTexture, (void*)&sub.material.emissiveTexture};
            }
            if (propertyName.compare(matFieldPos, 24, "metallicRoughnessTexture") == 0 && matFieldPos + 24 == propertyName.size()) {
                return {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.metallicRoughnessTexture, (void*)&sub.material.metallicRoughnessTexture};
            }
            if (propertyName.compare(matFieldPos, 16, "occlusionTexture") == 0 && matFieldPos + 16 == propertyName.size()) {
                return {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.occlusionTexture, (void*)&sub.material.occlusionTexture};
            }
            if (propertyName.compare(matFieldPos, 13, "normalTexture") == 0 && matFieldPos + 13 == propertyName.size()) {
                return {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.normalTexture, (void*)&sub.material.normalTexture};
            }
            return PropertyData();
        }

        // submeshes[N].primitiveType, faceCulling, textureShadow, textureRect
        if (propertyName.compare(fieldPos, 13, "primitiveType") == 0 && fieldPos + 13 == propertyName.size()) {
            return {PropertyType::Enum, UpdateFlags_Mesh_Reload, (void*)&defSub.primitiveType, (void*)&sub.primitiveType};
        }
        if (propertyName.compare(fieldPos, 11, "faceCulling") == 0 && fieldPos + 11 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Mesh_Reload, (void*)&defSub.faceCulling, (void*)&sub.faceCulling};
        }
        if (propertyName.compare(fieldPos, 13, "textureShadow") == 0 && fieldPos + 13 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Mesh_Reload, (void*)&defSub.textureShadow, (void*)&sub.textureShadow};
        }
        if (propertyName.compare(fieldPos, 11, "textureRect") == 0 && fieldPos + 11 == propertyName.size()) {
            return {PropertyType::Vector4, UpdateFlags_None, (void*)&defSub.textureRect, (void*)&sub.textureRect};
        }

        return PropertyData();
    }

    PropertyData getUIContainerPropertyFast(UIContainerComponent* comp, const std::string& propertyName) {
        UIContainerComponent& def = getDefaultComponent<UIContainerComponent>();

        if (!comp) {
            return PropertyData();
        }

        // Flat properties
        PropertyData result = resolveDirectProperties(comp, propertyName, kUIContainerTopProperties);
        if (result.ref) return result;

        // boxes[N].field
        if (propertyName.compare(0, 6, "boxes[") != 0) {
            return PropertyData();
        }

        size_t pos = 6;
        size_t boxIndex = 0;
        if (!parseIndex(propertyName, pos, boxIndex) || pos >= propertyName.size() || propertyName[pos] != ']') {
            return PropertyData();
        }
        if (boxIndex >= MAX_CONTAINER_BOXES) {
            return PropertyData();
        }

        ContainerBox& box = comp->boxes[boxIndex];
        ContainerBox& defBox = def.boxes[0];

        pos++;
        if (pos == propertyName.size()) {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&defBox, (void*)&box};
        }
        if (propertyName[pos] != '.') {
            return PropertyData();
        }

        const size_t fieldPos = pos + 1;

        if (propertyName.compare(fieldPos, 6, "layout") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::UInt, UpdateFlags_None, (void*)&defBox.layout, (void*)&box.layout};
        }
        if (propertyName.compare(fieldPos, 6, "expand") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Layout_Sizes, (void*)&defBox.expand, (void*)&box.expand};
        }
        if (propertyName.compare(fieldPos, 6, "rect.x") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_None, (void*)&defBox.rect.ptr()[0], (void*)&box.rect.ptr()[0]};
        }
        if (propertyName.compare(fieldPos, 6, "rect.y") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_None, (void*)&defBox.rect.ptr()[1], (void*)&box.rect.ptr()[1]};
        }
        if (propertyName.compare(fieldPos, 10, "rect.width") == 0 && fieldPos + 10 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_None, (void*)&defBox.rect.ptr()[2], (void*)&box.rect.ptr()[2]};
        }
        if (propertyName.compare(fieldPos, 11, "rect.height") == 0 && fieldPos + 11 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_None, (void*)&defBox.rect.ptr()[3], (void*)&box.rect.ptr()[3]};
        }

        return PropertyData();
    }

    PropertyData getScriptPropertyFast(ScriptComponent* comp, const std::string& propertyName) {
        if (!comp) {
            return PropertyData();
        }

        if (propertyName == "scripts") {
            return {PropertyType::Custom, UpdateFlags_None, nullptr, (void*)&comp->scripts};
        }

        // Parse "scripts[N].fieldName"
        if (propertyName.compare(0, 8, "scripts[") != 0) {
            return PropertyData();
        }

        size_t pos = 8;
        size_t index = 0;
        if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
            return PropertyData();
        }
        pos++;
        if (pos >= propertyName.size() || propertyName[pos] != '.') {
            return PropertyData();
        }
        pos++;

        if (index >= comp->scripts.size()) {
            return PropertyData();
        }
        auto& scriptEntry = comp->scripts[index];

        std::string fieldName = propertyName.substr(pos);

        if (fieldName == "enabled") {
            return {PropertyType::Bool, UpdateFlags_None, nullptr, (void*)&scriptEntry.enabled};
        }

        for (auto& prop : scriptEntry.properties) {
            if (prop.name == fieldName) {
                PropertyData result;
                result.type = Catalog::scriptPropertyTypeToPropertyType(prop.type);
                result.updateFlags = UpdateFlags_None;

                switch (prop.type) {
                    case ScriptPropertyType::Bool:
                        result.ref = &prop.valueRef<bool>();
                        result.def = &prop.defaultValueRef<bool>();
                        break;
                    case ScriptPropertyType::Int:
                        result.ref = &prop.valueRef<int>();
                        result.def = &prop.defaultValueRef<int>();
                        break;
                    case ScriptPropertyType::Float:
                        result.ref = &prop.valueRef<float>();
                        result.def = &prop.defaultValueRef<float>();
                        break;
                    case ScriptPropertyType::String:
                        result.ref = &prop.valueRef<std::string>();
                        result.def = &prop.defaultValueRef<std::string>();
                        break;
                    case ScriptPropertyType::Vector2:
                        result.ref = &prop.valueRef<Vector2>();
                        result.def = &prop.defaultValueRef<Vector2>();
                        break;
                    case ScriptPropertyType::Vector3:
                    case ScriptPropertyType::Color3:
                        result.ref = &prop.valueRef<Vector3>();
                        result.def = &prop.defaultValueRef<Vector3>();
                        break;
                    case ScriptPropertyType::Vector4:
                    case ScriptPropertyType::Color4:
                        result.ref = &prop.valueRef<Vector4>();
                        result.def = &prop.defaultValueRef<Vector4>();
                        break;
                    case ScriptPropertyType::EntityReference:
                        result.ref = &prop.valueRef<EntityReference>().entity;
                        result.def = &prop.defaultValueRef<EntityReference>().entity;
                        break;
                    default:
                        result.ref = nullptr;
                        result.def = nullptr;
                        break;
                }
                return result;
            }

            // Handle "propName.sceneId" for EntityReference properties
            if (prop.type == ScriptPropertyType::EntityReference) {
                std::string sceneIdField = prop.name + ".sceneId";
                if (fieldName == sceneIdField) {
                    static uint32_t defSceneId = 0;
                    return {PropertyType::UInt, UpdateFlags_None, (void*)&defSceneId, (void*)&prop.valueRef<EntityReference>().sceneId};
                }
            }
        }

        return PropertyData();
    }

    PropertyData getBody2DPropertyFast(Body2DComponent* comp, const std::string& propertyName) {
        Body2DComponent& defaultBody2D = getDefaultComponent<Body2DComponent>();

        if (!comp) {
            return PropertyData();
        }

        if (propertyName == "type") {
            return {PropertyType::Enum, UpdateFlags_Body2D, (void*)&defaultBody2D.type, (void*)&comp->type};
        }
        if (propertyName == "numShapes") {
            return {PropertyType::UInt, UpdateFlags_Body2D, (void*)&defaultBody2D.numShapes, (void*)&comp->numShapes};
        }

        constexpr const char* shapePrefix = "shapes[";
        if (propertyName.compare(0, 7, shapePrefix) != 0) {
            return PropertyData();
        }

        size_t pos = 7;
        size_t shapeIndex = 0;
        if (!parseIndex(propertyName, pos, shapeIndex) || pos >= propertyName.size() || propertyName[pos] != ']') {
            return PropertyData();
        }

        Shape2D& shape = comp->shapes[shapeIndex];
        Shape2D& defaultShape = defaultBody2D.shapes[0];

        pos++;
        if (pos == propertyName.size()) {
            return {PropertyType::Custom, UpdateFlags_Body2D, (void*)&defaultShape, (void*)&shape};
        }

        if (propertyName[pos] != '.') {
            return PropertyData();
        }

        const size_t fieldPos = pos + 1;
        if (propertyName.compare(fieldPos, 4, "type") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::Enum, UpdateFlags_Body2D, (void*)&defaultShape.type, (void*)&shape.type};
        }
        if (propertyName.compare(fieldPos, 6, "pointA") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Vector2, UpdateFlags_Body2D, (void*)&defaultShape.pointA, (void*)&shape.pointA};
        }
        if (propertyName.compare(fieldPos, 6, "pointB") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Vector2, UpdateFlags_Body2D, (void*)&defaultShape.pointB, (void*)&shape.pointB};
        }
        if (propertyName.compare(fieldPos, 6, "radius") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body2D, (void*)&defaultShape.radius, (void*)&shape.radius};
        }
        if (propertyName.compare(fieldPos, 4, "loop") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defaultShape.loop, (void*)&shape.loop};
        }
        if (propertyName.compare(fieldPos, 7, "density") == 0 && fieldPos + 7 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body2D, (void*)&defaultShape.density, (void*)&shape.density};
        }
        if (propertyName.compare(fieldPos, 8, "friction") == 0 && fieldPos + 8 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body2D, (void*)&defaultShape.friction, (void*)&shape.friction};
        }
        if (propertyName.compare(fieldPos, 11, "restitution") == 0 && fieldPos + 11 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body2D, (void*)&defaultShape.restitution, (void*)&shape.restitution};
        }
        if (propertyName.compare(fieldPos, 15, "enableHitEvents") == 0 && fieldPos + 15 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defaultShape.enableHitEvents, (void*)&shape.enableHitEvents};
        }
        if (propertyName.compare(fieldPos, 13, "contactEvents") == 0 && fieldPos + 13 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defaultShape.contactEvents, (void*)&shape.contactEvents};
        }
        if (propertyName.compare(fieldPos, 14, "preSolveEvents") == 0 && fieldPos + 14 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defaultShape.preSolveEvents, (void*)&shape.preSolveEvents};
        }
        if (propertyName.compare(fieldPos, 12, "sensorEvents") == 0 && fieldPos + 12 == propertyName.size()) {
            return {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defaultShape.sensorEvents, (void*)&shape.sensorEvents};
        }

        constexpr const char* verticesPrefix = "vertices[";
        if (propertyName.compare(fieldPos, 9, verticesPrefix) == 0) {
            size_t vertexPos = fieldPos + 9;
            size_t vertexIndex = 0;
            if (!parseIndex(propertyName, vertexPos, vertexIndex) || vertexPos >= propertyName.size() || propertyName[vertexPos] != ']') {
                return PropertyData();
            }

            if (vertexPos + 1 != propertyName.size()) {
                return PropertyData();
            }

            return {PropertyType::Vector2, UpdateFlags_Body2D, (void*)&defaultShape.vertices[0], (void*)&shape.vertices[vertexIndex]};
        }

        return PropertyData();
    }

    doriax::editor::PropertyData getBody3DPropertyFast(doriax::Body3DComponent* comp, const std::string& propertyName) {
        Body3DComponent& defaultBody3D = getDefaultComponent<Body3DComponent>();

        if (!comp) {
            return PropertyData();
        }

        if (propertyName == "type") {
            return {PropertyType::Enum, UpdateFlags_Body3D, (void*)&defaultBody3D.type, (void*)&comp->type};
        }
        if (propertyName == "motionQuality") {
            return {PropertyType::Enum, UpdateFlags_Body3D, (void*)&defaultBody3D.motionQuality, (void*)&comp->motionQuality};
        }
        if (propertyName == "sensor") {
            return {PropertyType::Bool, UpdateFlags_Body3D, (void*)&defaultBody3D.sensor, (void*)&comp->sensor};
        }
        if (propertyName == "numShapes") {
            return {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defaultBody3D.numShapes, (void*)&comp->numShapes};
        }
        if (propertyName == "overrideMassProperties") {
            return {PropertyType::Bool, UpdateFlags_Body3D, (void*)&defaultBody3D.overrideMassProperties, (void*)&comp->overrideMassProperties};
        }
        if (propertyName == "solidBoxSize") {
            return {PropertyType::Vector3, UpdateFlags_Body3D, (void*)&defaultBody3D.solidBoxSize, (void*)&comp->solidBoxSize};
        }
        if (propertyName == "solidBoxDensity") {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultBody3D.solidBoxDensity, (void*)&comp->solidBoxDensity};
        }

        constexpr const char* shapePrefix = "shapes[";
        if (propertyName.compare(0, 7, shapePrefix) != 0) {
            return PropertyData();
        }

        size_t pos = 7;
        size_t shapeIndex = 0;
        if (!parseIndex(propertyName, pos, shapeIndex) || pos >= propertyName.size() || propertyName[pos] != ']') {
            return PropertyData();
        }

        Shape3D& shape = comp->shapes[shapeIndex];
        Shape3D& defaultShape = defaultBody3D.shapes[0];

        pos++;
        if (pos == propertyName.size()) {
            return {PropertyType::Custom, UpdateFlags_Body3D, (void*)&defaultShape, (void*)&shape};
        }

        if (propertyName[pos] != '.') {
            return PropertyData();
        }

        const size_t fieldPos = pos + 1;

        if (propertyName.compare(fieldPos, 4, "type") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::Enum, UpdateFlags_Body3D, (void*)&defaultShape.type, (void*)&shape.type};
        }
        if (propertyName.compare(fieldPos, 8, "position") == 0 && fieldPos + 8 == propertyName.size()) {
            return {PropertyType::Vector3, UpdateFlags_Body3D, (void*)&defaultShape.position, (void*)&shape.position};
        }
        if (propertyName.compare(fieldPos, 8, "rotation") == 0 && fieldPos + 8 == propertyName.size()) {
            return {PropertyType::Quat, UpdateFlags_Body3D, (void*)&defaultShape.rotation, (void*)&shape.rotation};
        }
        if (propertyName.compare(fieldPos, 5, "width") == 0 && fieldPos + 5 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.width, (void*)&shape.width};
        }
        if (propertyName.compare(fieldPos, 6, "height") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.height, (void*)&shape.height};
        }
        if (propertyName.compare(fieldPos, 5, "depth") == 0 && fieldPos + 5 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.depth, (void*)&shape.depth};
        }
        if (propertyName.compare(fieldPos, 6, "radius") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.radius, (void*)&shape.radius};
        }
        if (propertyName.compare(fieldPos, 10, "halfHeight") == 0 && fieldPos + 10 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.halfHeight, (void*)&shape.halfHeight};
        }
        if (propertyName.compare(fieldPos, 9, "topRadius") == 0 && fieldPos + 9 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.topRadius, (void*)&shape.topRadius};
        }
        if (propertyName.compare(fieldPos, 12, "bottomRadius") == 0 && fieldPos + 12 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.bottomRadius, (void*)&shape.bottomRadius};
        }
        if (propertyName.compare(fieldPos, 7, "density") == 0 && fieldPos + 7 == propertyName.size()) {
            return {PropertyType::Float, UpdateFlags_Body3D, (void*)&defaultShape.density, (void*)&shape.density};
        }
        if (propertyName.compare(fieldPos, 6, "source") == 0 && fieldPos + 6 == propertyName.size()) {
            return {PropertyType::Enum, UpdateFlags_Body3D, (void*)&defaultShape.source, (void*)&shape.source};
        }
        if (propertyName.compare(fieldPos, 12, "sourceEntity") == 0 && fieldPos + 12 == propertyName.size()) {
            return {PropertyType::Entity, UpdateFlags_Body3D, (void*)&defaultShape.sourceEntity, (void*)&shape.sourceEntity};
        }
        if (propertyName.compare(fieldPos, 11, "samplesSize") == 0 && fieldPos + 11 == propertyName.size()) {
            return {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defaultShape.samplesSize, (void*)&shape.samplesSize};
        }
        if (propertyName.compare(fieldPos, 11, "numVertices") == 0 && fieldPos + 11 == propertyName.size()) {
            return {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defaultShape.numVertices, (void*)&shape.numVertices};
        }
        if (propertyName.compare(fieldPos, 10, "numIndices") == 0 && fieldPos + 10 == propertyName.size()) {
            return {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defaultShape.numIndices, (void*)&shape.numIndices};
        }

        constexpr const char* verticesPrefix = "vertices[";
        if (propertyName.compare(fieldPos, 9, verticesPrefix) == 0) {
            size_t vertexPos = fieldPos + 9;
            size_t vertexIndex = 0;
            if (!parseIndex(propertyName, vertexPos, vertexIndex) || vertexPos >= propertyName.size() || propertyName[vertexPos] != ']') {
                return PropertyData();
            }

            if (vertexPos + 1 != propertyName.size()) {
                return PropertyData();
            }

            return {PropertyType::Vector3, UpdateFlags_Body3D, (void*)&defaultShape.vertices[0], (void*)&shape.vertices[vertexIndex]};
        }

        return PropertyData();
    }

    // ── Resolve wrappers (single-property lookup) ──

    PropertyData resolveTransformPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<Transform*>(comp), propertyName, kTransformProperties);
    }

    PropertyData resolveUIPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<UIComponent*>(comp), propertyName, kUIProperties);
    }

    PropertyData resolveUILayoutPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<UILayoutComponent*>(comp), propertyName, kUILayoutProperties);
    }

    PropertyData resolveImagePropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ImageComponent*>(comp), propertyName, kImageProperties);
    }

    PropertyData resolveButtonPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ButtonComponent*>(comp), propertyName, kButtonProperties);
    }

    PropertyData resolveScrollbarPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ScrollbarComponent*>(comp), propertyName, kScrollbarProperties);
    }

    PropertyData resolveProgressbarPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ProgressbarComponent*>(comp), propertyName, kProgressbarProperties);
    }

    PropertyData resolveTextEditPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<TextEditComponent*>(comp), propertyName, kTextEditProperties);
    }

    PropertyData resolvePanelPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<PanelComponent*>(comp), propertyName, kPanelProperties);
    }

    PropertyData getSpritePropertyFast(SpriteComponent* comp, const std::string& propertyName) {
        SpriteComponent& def = getDefaultComponent<SpriteComponent>();

        if (!comp) {
            return PropertyData();
        }

        // Flat properties
        PropertyData result = resolveDirectProperties(comp, propertyName, kSpriteTopProperties);
        if (result.ref) return result;

        // numFramesRect
        if (propertyName == "numFramesRect") {
            // Frame definitions are animation metadata. They do not change the
            // sprite quad, so rebuilding it here only causes the texture to be
            // loaded again after applying a large slice operation.
            return {PropertyType::UInt, UpdateFlags_None, (void*)&def.numFramesRect, (void*)&comp->numFramesRect};
        }

        // framesRect[N].field
        if (propertyName.compare(0, 11, "framesRect[") != 0) {
            return PropertyData();
        }

        size_t pos = 11;
        size_t frameIndex = 0;
        if (!parseIndex(propertyName, pos, frameIndex) || pos >= propertyName.size() || propertyName[pos] != ']') {
            return PropertyData();
        }

        SpriteFrameData& frame = comp->framesRect[frameIndex];
        SpriteFrameData& defFrame = def.framesRect[0];

        pos++;
        if (pos == propertyName.size()) {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&defFrame, (void*)&frame};
        }
        if (propertyName[pos] != '.') {
            return PropertyData();
        }

        const size_t fieldPos = pos + 1;

        if (propertyName.compare(fieldPos, 4, "name") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::String, UpdateFlags_None, (void*)&defFrame.name, (void*)&frame.name};
        }
        if (propertyName.compare(fieldPos, 4, "rect") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::Vector4, UpdateFlags_None, (void*)&defFrame.rect, (void*)&frame.rect};
        }

        return PropertyData();
    }

    PropertyData resolveSpritePropertyFast(void* comp, const std::string& propertyName) {
        return getSpritePropertyFast(static_cast<SpriteComponent*>(comp), propertyName);
    }

    PropertyData getTilemapPropertyFast(TilemapComponent* comp, const std::string& propertyName) {
        TilemapComponent& def = getDefaultComponent<TilemapComponent>();

        if (!comp) {
            return PropertyData();
        }

        // Flat properties
        PropertyData result = resolveDirectProperties(comp, propertyName, kTilemapTopProperties);
        if (result.ref) return result;

        // numTilesRect
        if (propertyName == "numTilesRect") {
            return {PropertyType::UInt, UpdateFlags_Tilemap, (void*)&def.numTilesRect, (void*)&comp->numTilesRect};
        }

        // numTiles
        if (propertyName == "numTiles") {
            return {PropertyType::UInt, UpdateFlags_Tilemap, (void*)&def.numTiles, (void*)&comp->numTiles};
        }

        // tilesRect[N].field
        if (propertyName.compare(0, 10, "tilesRect[") == 0) {
            size_t pos = 10;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }

            TileRectData& tileRect = comp->tilesRect[index];
            TileRectData& defTileRect = def.tilesRect[0];

            pos++;
            if (pos == propertyName.size()) {
                return {PropertyType::Custom, UpdateFlags_Tilemap, (void*)&defTileRect, (void*)&tileRect};
            }
            if (propertyName[pos] != '.') {
                return PropertyData();
            }

            const size_t fieldPos = pos + 1;

            if (propertyName.compare(fieldPos, 4, "name") == 0 && fieldPos + 4 == propertyName.size()) {
                return {PropertyType::String, UpdateFlags_Tilemap, (void*)&defTileRect.name, (void*)&tileRect.name};
            }
            if (propertyName.compare(fieldPos, 9, "submeshId") == 0 && fieldPos + 9 == propertyName.size()) {
                return {PropertyType::Int, UpdateFlags_Tilemap, (void*)&defTileRect.submeshId, (void*)&tileRect.submeshId};
            }
            if (propertyName.compare(fieldPos, 4, "rect") == 0 && fieldPos + 4 == propertyName.size()) {
                return {PropertyType::Vector4, UpdateFlags_Tilemap, (void*)&defTileRect.rect, (void*)&tileRect.rect};
            }

            return PropertyData();
        }

        // tiles[N].field
        if (propertyName.compare(0, 6, "tiles[") == 0) {
            size_t pos = 6;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }

            TileData& tile = comp->tiles[index];
            TileData& defTile = def.tiles[0];

            pos++;
            if (pos == propertyName.size()) {
                return {PropertyType::Custom, UpdateFlags_Tilemap, (void*)&defTile, (void*)&tile};
            }
            if (propertyName[pos] != '.') {
                return PropertyData();
            }

            const size_t fieldPos = pos + 1;

            if (propertyName.compare(fieldPos, 4, "name") == 0 && fieldPos + 4 == propertyName.size()) {
                return {PropertyType::String, UpdateFlags_Tilemap, (void*)&defTile.name, (void*)&tile.name};
            }
            if (propertyName.compare(fieldPos, 6, "rectId") == 0 && fieldPos + 6 == propertyName.size()) {
                return {PropertyType::Int, UpdateFlags_Tilemap, (void*)&defTile.rectId, (void*)&tile.rectId};
            }
            if (propertyName.compare(fieldPos, 8, "position") == 0 && fieldPos + 8 == propertyName.size()) {
                return {PropertyType::Vector2, UpdateFlags_Tilemap, (void*)&defTile.position, (void*)&tile.position};
            }
            if (propertyName.compare(fieldPos, 5, "width") == 0 && fieldPos + 5 == propertyName.size()) {
                return {PropertyType::Float, UpdateFlags_Tilemap, (void*)&defTile.width, (void*)&tile.width};
            }
            if (propertyName.compare(fieldPos, 6, "height") == 0 && fieldPos + 6 == propertyName.size()) {
                return {PropertyType::Float, UpdateFlags_Tilemap, (void*)&defTile.height, (void*)&tile.height};
            }

            return PropertyData();
        }

        return PropertyData();
    }

    PropertyData resolveTilemapPropertyFast(void* comp, const std::string& propertyName) {
        return getTilemapPropertyFast(static_cast<TilemapComponent*>(comp), propertyName);
    }

    PropertyData getTerrainFoliageLayerPropertyFast(TerrainFoliageLayer& layer, const std::string& fieldName) {
        static TerrainFoliageLayer def;
        if (fieldName == ".meshPath") return {PropertyType::String, UpdateFlags_Terrain_Foliage, &def.meshPath, &layer.meshPath};
        if (fieldName == ".densityMap") return {PropertyType::Texture, UpdateFlags_Terrain_Foliage, &def.densityMap, &layer.densityMap};
        if (fieldName == ".density") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.density, &layer.density};
        if (fieldName == ".minScale") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.minScale, &layer.minScale};
        if (fieldName == ".maxScale") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.maxScale, &layer.maxScale};
        if (fieldName == ".rotationJitter") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.rotationJitter, &layer.rotationJitter};
        if (fieldName == ".alignToNormal") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.alignToNormal, &layer.alignToNormal};
        if (fieldName == ".minSlope") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.minSlope, &layer.minSlope};
        if (fieldName == ".maxSlope") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.maxSlope, &layer.maxSlope};
        if (fieldName == ".drawDistance") return {PropertyType::Float, UpdateFlags_Terrain_Foliage, &def.drawDistance, &layer.drawDistance};
        if (fieldName == ".seed") return {PropertyType::UInt, UpdateFlags_Terrain_Foliage, &def.seed, &layer.seed};

        return PropertyData();
    }

    PropertyData resolveTerrainPropertyFast(void* compRef, const std::string& propertyName) {
        TerrainComponent* comp = static_cast<TerrainComponent*>(compRef);
        if (!comp) return PropertyData();

        PropertyData result = resolveDirectProperties(comp, propertyName, kTerrainProperties);
        if (result.ref) return result;

        TerrainComponent& def = getDefaultComponent<TerrainComponent>();

        if (propertyName == "ranges") {
            return {PropertyType::Custom, UpdateFlags_Terrain, (void*)&def.ranges, (void*)&comp->ranges};
        }

        if (propertyName.compare(0, 7, "ranges[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= comp->ranges.size()) {
                return PropertyData();
            }
            static float defValue = 0.0f;
            return {PropertyType::Float, UpdateFlags_Terrain, (void*)&defValue, (void*)&comp->ranges[index]};
        }

        if (propertyName == "foliageLayers") {
            return {PropertyType::Custom, UpdateFlags_Terrain_Foliage, (void*)&def.foliageLayers, (void*)&comp->foliageLayers};
        }

        if (propertyName.compare(0, 14, "foliageLayers[") == 0) {
            size_t pos = 14;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (index >= comp->foliageLayers.size()) {
                return PropertyData();
            }
            return getTerrainFoliageLayerPropertyFast(comp->foliageLayers[index], propertyName.substr(pos + 1));
        }

        return PropertyData();
    }

    PropertyData resolveLightPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<LightComponent*>(comp), propertyName, kLightProperties);
    }

    PropertyData resolveFogPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<FogComponent*>(comp), propertyName, kFogProperties);
    }

    PropertyData resolveMirrorPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<MirrorComponent*>(comp), propertyName, kMirrorProperties);
    }

    PropertyData resolveReflectionProbePropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ReflectionProbeComponent*>(comp), propertyName, kReflectionProbeProperties);
    }

    PropertyData resolveLight2DPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<Light2DComponent*>(comp), propertyName, kLight2DProperties);
    }

    PropertyData resolveOccluder2DPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<Occluder2DComponent*>(comp), propertyName, kOccluder2DProperties);
    }

    PropertyData resolveCameraPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<CameraComponent*>(comp), propertyName, kCameraProperties);
    }

    PropertyData resolveAudioPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<SoundComponent*>(comp), propertyName, kSoundProperties);
    }

    PropertyData resolveSkyPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<SkyComponent*>(comp), propertyName, kSkyProperties);
    }

    PropertyData resolveTextPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<TextComponent*>(comp), propertyName, kTextProperties);
    }

    PropertyData resolveJoint2DPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<Joint2DComponent*>(comp), propertyName, kJoint2DProperties);
    }

    PropertyData resolveJoint3DPropertyFast(void* comp, const std::string& propertyName) {
        Joint3DComponent* joint = static_cast<Joint3DComponent*>(comp);
        if (!joint) return PropertyData();

        // Flat properties from descriptor array
        PropertyData result = resolveDirectProperties(joint, propertyName, kJoint3DProperties);
        if (result.ref) return result;

        Joint3DComponent& def = getDefaultComponent<Joint3DComponent>();

        if (propertyName == "pathPoints") {
            return {PropertyType::Custom, UpdateFlags_Joint3D, (void*)&def.pathPoints, (void*)&joint->pathPoints};
        }

        // pathPoints[N]
        if (propertyName.compare(0, 11, "pathPoints[") == 0) {
            size_t pos = 11;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= joint->pathPoints.size()) {
                return PropertyData();
            }
            return {PropertyType::Vector3, UpdateFlags_Joint3D, (void*)&def.pathPosition, (void*)&joint->pathPoints[index]};
        }

        return PropertyData();
    }

    PropertyData resolveBody2DPropertyFast(void* comp, const std::string& propertyName) {
        return getBody2DPropertyFast(static_cast<Body2DComponent*>(comp), propertyName);
    }

    PropertyData resolveBody3DPropertyFast(void* comp, const std::string& propertyName) {
        return getBody3DPropertyFast(static_cast<Body3DComponent*>(comp), propertyName);
    }

    PropertyData getParticlesPropertyFast(ParticlesComponent* comp, const std::string& propertyName) {
        ParticlesComponent& def = getDefaultComponent<ParticlesComponent>();
        if (!comp) return PropertyData();

        // Top-level properties
        if (propertyName == "maxParticles") return {PropertyType::UInt, UpdateFlags_None, &def.maxParticles, &comp->maxParticles};
        if (propertyName == "emitter") return {PropertyType::Bool, UpdateFlags_None, &def.emitter, &comp->emitter};
        if (propertyName == "loop") return {PropertyType::Bool, UpdateFlags_None, &def.loop, &comp->loop};
        if (propertyName == "localSpace") return {PropertyType::Bool, UpdateFlags_None, &def.localSpace, &comp->localSpace};
        if (propertyName == "rate") return {PropertyType::Int, UpdateFlags_None, &def.rate, &comp->rate};
        if (propertyName == "maxPerUpdate") return {PropertyType::Int, UpdateFlags_None, &def.maxPerUpdate, &comp->maxPerUpdate};
        if (propertyName == "bursts") return {PropertyType::Custom, UpdateFlags_None, &def.bursts, &comp->bursts};

        // Life Initializer
        if (propertyName == "lifeInitializer.minLife") return {PropertyType::Float, UpdateFlags_None, &def.lifeInitializer.minLife, &comp->lifeInitializer.minLife};
        if (propertyName == "lifeInitializer.maxLife") return {PropertyType::Float, UpdateFlags_None, &def.lifeInitializer.maxLife, &comp->lifeInitializer.maxLife};

        // Position Initializer
        if (propertyName == "positionInitializer.shape") return {PropertyType::Enum, UpdateFlags_None, &def.positionInitializer.shape, &comp->positionInitializer.shape};
        if (propertyName == "positionInitializer.minPosition") return {PropertyType::Vector3, UpdateFlags_None, &def.positionInitializer.minPosition, &comp->positionInitializer.minPosition};
        if (propertyName == "positionInitializer.maxPosition") return {PropertyType::Vector3, UpdateFlags_None, &def.positionInitializer.maxPosition, &comp->positionInitializer.maxPosition};
        if (propertyName == "positionInitializer.radius") return {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.radius, &comp->positionInitializer.radius};
        if (propertyName == "positionInitializer.innerRadius") return {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.innerRadius, &comp->positionInitializer.innerRadius};
        if (propertyName == "positionInitializer.coneAngle") return {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.coneAngle, &comp->positionInitializer.coneAngle};
        if (propertyName == "positionInitializer.coneHeight") return {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.coneHeight, &comp->positionInitializer.coneHeight};

        // Position Modifier
        if (propertyName == "positionModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.positionModifier.fromTime, &comp->positionModifier.fromTime};
        if (propertyName == "positionModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.positionModifier.toTime, &comp->positionModifier.toTime};
        if (propertyName == "positionModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.positionModifier.function, &comp->positionModifier.function};
        if (propertyName == "positionModifier.fromPosition") return {PropertyType::Vector3, UpdateFlags_None, &def.positionModifier.fromPosition, &comp->positionModifier.fromPosition};
        if (propertyName == "positionModifier.toPosition") return {PropertyType::Vector3, UpdateFlags_None, &def.positionModifier.toPosition, &comp->positionModifier.toPosition};

        // Velocity Initializer
        if (propertyName == "velocityInitializer.minVelocity") return {PropertyType::Vector3, UpdateFlags_None, &def.velocityInitializer.minVelocity, &comp->velocityInitializer.minVelocity};
        if (propertyName == "velocityInitializer.maxVelocity") return {PropertyType::Vector3, UpdateFlags_None, &def.velocityInitializer.maxVelocity, &comp->velocityInitializer.maxVelocity};

        // Velocity Modifier
        if (propertyName == "velocityModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.velocityModifier.fromTime, &comp->velocityModifier.fromTime};
        if (propertyName == "velocityModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.velocityModifier.toTime, &comp->velocityModifier.toTime};
        if (propertyName == "velocityModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.velocityModifier.function, &comp->velocityModifier.function};
        if (propertyName == "velocityModifier.fromVelocity") return {PropertyType::Vector3, UpdateFlags_None, &def.velocityModifier.fromVelocity, &comp->velocityModifier.fromVelocity};
        if (propertyName == "velocityModifier.toVelocity") return {PropertyType::Vector3, UpdateFlags_None, &def.velocityModifier.toVelocity, &comp->velocityModifier.toVelocity};

        // Acceleration Initializer
        if (propertyName == "accelerationInitializer.minAcceleration") return {PropertyType::Vector3, UpdateFlags_None, &def.accelerationInitializer.minAcceleration, &comp->accelerationInitializer.minAcceleration};
        if (propertyName == "accelerationInitializer.maxAcceleration") return {PropertyType::Vector3, UpdateFlags_None, &def.accelerationInitializer.maxAcceleration, &comp->accelerationInitializer.maxAcceleration};

        // Acceleration Modifier
        if (propertyName == "accelerationModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.accelerationModifier.fromTime, &comp->accelerationModifier.fromTime};
        if (propertyName == "accelerationModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.accelerationModifier.toTime, &comp->accelerationModifier.toTime};
        if (propertyName == "accelerationModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.accelerationModifier.function, &comp->accelerationModifier.function};
        if (propertyName == "accelerationModifier.fromAcceleration") return {PropertyType::Vector3, UpdateFlags_None, &def.accelerationModifier.fromAcceleration, &comp->accelerationModifier.fromAcceleration};
        if (propertyName == "accelerationModifier.toAcceleration") return {PropertyType::Vector3, UpdateFlags_None, &def.accelerationModifier.toAcceleration, &comp->accelerationModifier.toAcceleration};

        // Color Initializer
        if (propertyName == "colorInitializer.minColor") return {PropertyType::Vector3, UpdateFlags_None, &def.colorInitializer.minColor, &comp->colorInitializer.minColor};
        if (propertyName == "colorInitializer.maxColor") return {PropertyType::Vector3, UpdateFlags_None, &def.colorInitializer.maxColor, &comp->colorInitializer.maxColor};
        if (propertyName == "colorInitializer.useSRGB") return {PropertyType::Bool, UpdateFlags_None, &def.colorInitializer.useSRGB, &comp->colorInitializer.useSRGB};

        // Color Modifier
        if (propertyName == "colorModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.colorModifier.fromTime, &comp->colorModifier.fromTime};
        if (propertyName == "colorModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.colorModifier.toTime, &comp->colorModifier.toTime};
        if (propertyName == "colorModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.colorModifier.function, &comp->colorModifier.function};
        if (propertyName == "colorModifier.fromColor") return {PropertyType::Vector3, UpdateFlags_None, &def.colorModifier.fromColor, &comp->colorModifier.fromColor};
        if (propertyName == "colorModifier.toColor") return {PropertyType::Vector3, UpdateFlags_None, &def.colorModifier.toColor, &comp->colorModifier.toColor};
        if (propertyName == "colorModifier.useSRGB") return {PropertyType::Bool, UpdateFlags_None, &def.colorModifier.useSRGB, &comp->colorModifier.useSRGB};

        // Color Gradient (overrides colorModifier when non-empty)
        if (propertyName == "colorGradient") return {PropertyType::Custom, UpdateFlags_None, &def.colorGradient, &comp->colorGradient};

        // Alpha Initializer
        if (propertyName == "alphaInitializer.minAlpha") return {PropertyType::Float, UpdateFlags_None, &def.alphaInitializer.minAlpha, &comp->alphaInitializer.minAlpha};
        if (propertyName == "alphaInitializer.maxAlpha") return {PropertyType::Float, UpdateFlags_None, &def.alphaInitializer.maxAlpha, &comp->alphaInitializer.maxAlpha};

        // Alpha Modifier
        if (propertyName == "alphaModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.fromTime, &comp->alphaModifier.fromTime};
        if (propertyName == "alphaModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.toTime, &comp->alphaModifier.toTime};
        if (propertyName == "alphaModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.alphaModifier.function, &comp->alphaModifier.function};
        if (propertyName == "alphaModifier.fromAlpha") return {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.fromAlpha, &comp->alphaModifier.fromAlpha};
        if (propertyName == "alphaModifier.toAlpha") return {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.toAlpha, &comp->alphaModifier.toAlpha};

        // Size Initializer
        if (propertyName == "sizeInitializer.minSize") return {PropertyType::Float, UpdateFlags_None, &def.sizeInitializer.minSize, &comp->sizeInitializer.minSize};
        if (propertyName == "sizeInitializer.maxSize") return {PropertyType::Float, UpdateFlags_None, &def.sizeInitializer.maxSize, &comp->sizeInitializer.maxSize};

        // Size Modifier
        if (propertyName == "sizeModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.fromTime, &comp->sizeModifier.fromTime};
        if (propertyName == "sizeModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.toTime, &comp->sizeModifier.toTime};
        if (propertyName == "sizeModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.sizeModifier.function, &comp->sizeModifier.function};
        if (propertyName == "sizeModifier.fromSize") return {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.fromSize, &comp->sizeModifier.fromSize};
        if (propertyName == "sizeModifier.toSize") return {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.toSize, &comp->sizeModifier.toSize};

        // Sprite Initializer
        if (propertyName == "spriteInitializer.frames") return {PropertyType::Custom, UpdateFlags_None, &def.spriteInitializer.frames, &comp->spriteInitializer.frames};

        // Sprite Modifier
        if (propertyName == "spriteModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.spriteModifier.fromTime, &comp->spriteModifier.fromTime};
        if (propertyName == "spriteModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.spriteModifier.toTime, &comp->spriteModifier.toTime};
        if (propertyName == "spriteModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.spriteModifier.function, &comp->spriteModifier.function};
        if (propertyName == "spriteModifier.frames") return {PropertyType::Custom, UpdateFlags_None, &def.spriteModifier.frames, &comp->spriteModifier.frames};

        // Rotation Initializer
        if (propertyName == "rotationInitializer.minRotation") return {PropertyType::Quat, UpdateFlags_None, &def.rotationInitializer.minRotation, &comp->rotationInitializer.minRotation};
        if (propertyName == "rotationInitializer.maxRotation") return {PropertyType::Quat, UpdateFlags_None, &def.rotationInitializer.maxRotation, &comp->rotationInitializer.maxRotation};
        if (propertyName == "rotationInitializer.shortestPath") return {PropertyType::Bool, UpdateFlags_None, &def.rotationInitializer.shortestPath, &comp->rotationInitializer.shortestPath};

        // Rotation Modifier
        if (propertyName == "rotationModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.rotationModifier.fromTime, &comp->rotationModifier.fromTime};
        if (propertyName == "rotationModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.rotationModifier.toTime, &comp->rotationModifier.toTime};
        if (propertyName == "rotationModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.rotationModifier.function, &comp->rotationModifier.function};
        if (propertyName == "rotationModifier.fromRotation") return {PropertyType::Quat, UpdateFlags_None, &def.rotationModifier.fromRotation, &comp->rotationModifier.fromRotation};
        if (propertyName == "rotationModifier.toRotation") return {PropertyType::Quat, UpdateFlags_None, &def.rotationModifier.toRotation, &comp->rotationModifier.toRotation};
        if (propertyName == "rotationModifier.shortestPath") return {PropertyType::Bool, UpdateFlags_None, &def.rotationModifier.shortestPath, &comp->rotationModifier.shortestPath};

        // Scale Initializer
        if (propertyName == "scaleInitializer.minScale") return {PropertyType::Vector3, UpdateFlags_None, &def.scaleInitializer.minScale, &comp->scaleInitializer.minScale};
        if (propertyName == "scaleInitializer.maxScale") return {PropertyType::Vector3, UpdateFlags_None, &def.scaleInitializer.maxScale, &comp->scaleInitializer.maxScale};
        if (propertyName == "scaleInitializer.linearSort") return {PropertyType::Bool, UpdateFlags_None, &def.scaleInitializer.linearSort, &comp->scaleInitializer.linearSort};

        // Scale Modifier
        if (propertyName == "scaleModifier.fromTime") return {PropertyType::Float, UpdateFlags_None, &def.scaleModifier.fromTime, &comp->scaleModifier.fromTime};
        if (propertyName == "scaleModifier.toTime") return {PropertyType::Float, UpdateFlags_None, &def.scaleModifier.toTime, &comp->scaleModifier.toTime};
        if (propertyName == "scaleModifier.function") return {PropertyType::Ease, UpdateFlags_None, &def.scaleModifier.function, &comp->scaleModifier.function};
        if (propertyName == "scaleModifier.fromScale") return {PropertyType::Vector3, UpdateFlags_None, &def.scaleModifier.fromScale, &comp->scaleModifier.fromScale};
        if (propertyName == "scaleModifier.toScale") return {PropertyType::Vector3, UpdateFlags_None, &def.scaleModifier.toScale, &comp->scaleModifier.toScale};

        return PropertyData();
    }

    PropertyData resolveParticlesPropertyFast(void* comp, const std::string& propertyName) {
        return getParticlesPropertyFast(static_cast<ParticlesComponent*>(comp), propertyName);
    }

    PropertyData getInstancedMeshPropertyFast(InstancedMeshComponent* comp, const std::string& propertyName) {
        InstancedMeshComponent& def = getDefaultComponent<InstancedMeshComponent>();
        if (propertyName == "maxInstances") return {PropertyType::UInt, UpdateFlags_Mesh_Reload, &def.maxInstances, &comp->maxInstances};
        if (propertyName == "instancedBillboard") return {PropertyType::Bool, UpdateFlags_Mesh_Reload, &def.instancedBillboard, &comp->instancedBillboard};
        if (propertyName == "instancedCylindricalBillboard") return {PropertyType::Bool, UpdateFlags_Mesh_Reload, &def.instancedCylindricalBillboard, &comp->instancedCylindricalBillboard};

        if (propertyName == "instances") {
            static std::vector<InstanceData> defInstances;
            return {PropertyType::Custom, UpdateFlags_Instanced_Mesh, (void*)&defInstances, (void*)&comp->instances};
        }

        if (propertyName.compare(0, 10, "instances[") == 0) {
            size_t pos = 10;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (pos + 1 >= propertyName.size()) return PropertyData();
            std::string fieldName = propertyName.substr(pos + 1);
            if (index >= comp->instances.size()) return PropertyData();
            if (fieldName == ".position") {
                static Vector3 defPos = Vector3(0.0f, 0.0f, 0.0f);
                return {PropertyType::Vector3, UpdateFlags_Instanced_Mesh, (void*)&defPos, (void*)&comp->instances[index].position};
            }
            if (fieldName == ".rotation") {
                static Quaternion defRot;
                return {PropertyType::Quat, UpdateFlags_Instanced_Mesh, (void*)&defRot, (void*)&comp->instances[index].rotation};
            }
            if (fieldName == ".scale") {
                static Vector3 defScale = Vector3(1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector3, UpdateFlags_Instanced_Mesh, (void*)&defScale, (void*)&comp->instances[index].scale};
            }
            if (fieldName == ".color") {
                static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_Instanced_Mesh, (void*)&defColor, (void*)&comp->instances[index].color};
            }
            if (fieldName == ".visible") {
                static bool defVisible = true;
                return {PropertyType::Bool, UpdateFlags_Instanced_Mesh, (void*)&defVisible, (void*)&comp->instances[index].visible};
            }
        }

        return PropertyData();
    }

    PropertyData resolveInstancedMeshPropertyFast(void* comp, const std::string& propertyName) {
        return getInstancedMeshPropertyFast(static_cast<InstancedMeshComponent*>(comp), propertyName);
    }

    void enumerateInstancedMeshProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        InstancedMeshComponent* comp = static_cast<InstancedMeshComponent*>(compRef);
        InstancedMeshComponent& def = getDefaultComponent<InstancedMeshComponent>();
        ps["maxInstances"] = {PropertyType::UInt, UpdateFlags_Mesh_Reload, &def.maxInstances, comp ? &comp->maxInstances : nullptr};
        ps["instancedBillboard"] = {PropertyType::Bool, UpdateFlags_Mesh_Reload, &def.instancedBillboard, comp ? &comp->instancedBillboard : nullptr};
        ps["instancedCylindricalBillboard"] = {PropertyType::Bool, UpdateFlags_Mesh_Reload, &def.instancedCylindricalBillboard, comp ? &comp->instancedCylindricalBillboard : nullptr};
        static std::vector<InstanceData> defInstances;
        ps["instances"] = {PropertyType::Custom, UpdateFlags_Instanced_Mesh, (void*)&defInstances, comp ? (void*)&comp->instances : nullptr};
        if (comp) {
            static Vector3 defPos = Vector3(0.0f, 0.0f, 0.0f);
            static Quaternion defRot;
            static Vector3 defScale = Vector3(1.0f, 1.0f, 1.0f);
            static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            static bool defVisible = true;
            for (size_t i = 0; i < comp->instances.size(); i++) {
                std::string prefix = "instances[" + std::to_string(i) + "]";
                ps[prefix + ".position"] = {PropertyType::Vector3, UpdateFlags_Instanced_Mesh, (void*)&defPos, (void*)&comp->instances[i].position};
                ps[prefix + ".rotation"] = {PropertyType::Quat, UpdateFlags_Instanced_Mesh, (void*)&defRot, (void*)&comp->instances[i].rotation};
                ps[prefix + ".scale"] = {PropertyType::Vector3, UpdateFlags_Instanced_Mesh, (void*)&defScale, (void*)&comp->instances[i].scale};
                ps[prefix + ".color"] = {PropertyType::Vector4, UpdateFlags_Instanced_Mesh, (void*)&defColor, (void*)&comp->instances[i].color};
                ps[prefix + ".visible"] = {PropertyType::Bool, UpdateFlags_Instanced_Mesh, (void*)&defVisible, (void*)&comp->instances[i].visible};
            }
        }
    }

    PropertyData resolveActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ActionComponent*>(comp), propertyName, kActionProperties);
    }

    PropertyData resolveTimedActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<TimedActionComponent*>(comp), propertyName, kTimedActionProperties);
    }

    PropertyData resolvePositionActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<PositionActionComponent*>(comp), propertyName, kPositionActionProperties);
    }

    PropertyData resolveRotationActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<RotationActionComponent*>(comp), propertyName, kRotationActionProperties);
    }

    PropertyData resolveScaleActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ScaleActionComponent*>(comp), propertyName, kScaleActionProperties);
    }

    PropertyData resolveColorActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<ColorActionComponent*>(comp), propertyName, kColorActionProperties);
    }

    PropertyData resolveAlphaActionPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<AlphaActionComponent*>(comp), propertyName, kAlphaActionProperties);
    }

    PropertyData resolveSpriteAnimationPropertyFast(void* compRef, const std::string& propertyName) {
        SpriteAnimationComponent* comp = static_cast<SpriteAnimationComponent*>(compRef);
        if (!comp) return PropertyData();

        PropertyData result = resolveDirectProperties(comp, propertyName, kSpriteAnimationProperties);
        if (result.ref) return result;

        SpriteAnimationComponent& def = getDefaultComponent<SpriteAnimationComponent>();

        if (propertyName.compare(0, 7, "frames[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (index < MAX_SPRITE_FRAMES) {
                return {PropertyType::Int, UpdateFlags_None, (void*)&def.frames[index], (void*)&comp->frames[index]};
            }
        }
        if (propertyName.compare(0, 11, "framesTime[") == 0) {
            size_t pos = 11;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (index < MAX_SPRITE_FRAMES) {
                return {PropertyType::Int, UpdateFlags_None, (void*)&def.framesTime[index], (void*)&comp->framesTime[index]};
            }
        }
        return PropertyData();
    }

    PropertyData resolveAnimationPropertyFast(void* compRef, const std::string& propertyName) {
        AnimationComponent* comp = static_cast<AnimationComponent*>(compRef);
        if (!comp) return PropertyData();

        PropertyData result = resolveDirectProperties(comp, propertyName, kAnimationProperties);
        if (result.ref) return result;

        AnimationComponent& def = getDefaultComponent<AnimationComponent>();

        if (propertyName == "actions") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.actions, (void*)&comp->actions};
        }

        if (propertyName.compare(0, 8, "actions[") == 0) {
            size_t pos = 8;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index)) return PropertyData();
            if (pos + 1 >= propertyName.length() || propertyName[pos] != ']') return PropertyData();
            if (index < comp->actions.size()) {
                std::string fieldName = propertyName.substr(pos + 1);
                if (fieldName == ".startTime") {
                    static float defStartTime = 0;
                    return {PropertyType::Float, UpdateFlags_None, (void*)&defStartTime, (void*)&comp->actions[index].startTime};
                }
                if (fieldName == ".duration") {
                    static float defDuration = 0;
                    return {PropertyType::Float, UpdateFlags_None, (void*)&defDuration, (void*)&comp->actions[index].duration};
                }
                if (fieldName == ".action") {
                    static Entity defAction = NULL_ENTITY;
                    return {PropertyType::Entity, UpdateFlags_None, (void*)&defAction, (void*)&comp->actions[index].action};
                }
                if (fieldName == ".track") {
                    static uint32_t defTrack = 0;
                    return {PropertyType::UInt, UpdateFlags_None, (void*)&defTrack, (void*)&comp->actions[index].track};
                }
            }
        }

        return PropertyData();
    }

    PropertyData resolveModelPropertyFast(void* compRef, const std::string& propertyName) {
        ModelComponent* comp = static_cast<ModelComponent*>(compRef);
        if (!comp) return PropertyData();

        ModelComponent& def = getDefaultComponent<ModelComponent>();

        if (propertyName == "filename") {
            return {PropertyType::String, UpdateFlags_None, (void*)&def.filename, (void*)&comp->filename};
        }

        if (propertyName == "mergeStaticMeshes") {
            return {PropertyType::Bool, UpdateFlags_Model, (void*)&def.mergeStaticMeshes, (void*)&comp->mergeStaticMeshes};
        }

        if (propertyName == "skeleton") {
            return {PropertyType::Entity, UpdateFlags_Mesh_Reload, (void*)&def.skeleton, (void*)&comp->skeleton};
        }

        if (propertyName == "animations") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.animations, (void*)&comp->animations};
        }

        if (propertyName.compare(0, 11, "animations[") == 0) {
            size_t pos = 11;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= comp->animations.size()) {
                return PropertyData();
            }
            static Entity defEntity = NULL_ENTITY;
            return {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&comp->animations[index]};
        }

        return PropertyData();
    }

    PropertyData resolveMeshPropertyFast(void* comp, const std::string& propertyName) {
        return getMeshPropertyFast(static_cast<MeshComponent*>(comp), propertyName);
    }

    PropertyData resolveUIContainerPropertyFast(void* comp, const std::string& propertyName) {
        return getUIContainerPropertyFast(static_cast<UIContainerComponent*>(comp), propertyName);
    }

    PropertyData resolveScriptPropertyFast(void* comp, const std::string& propertyName) {
        return getScriptPropertyFast(static_cast<ScriptComponent*>(comp), propertyName);
    }

    PropertyData resolveBundlePropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<BundleComponent*>(comp), propertyName, kBundleProperties);
    }

    PropertyData resolveBonePropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<BoneComponent*>(comp), propertyName, kBoneProperties);
    }

    PropertyData resolveKeyframeTracksPropertyFast(void* comp, const std::string& propertyName) {
        KeyframeTracksComponent* trackComp = static_cast<KeyframeTracksComponent*>(comp);
        if (!trackComp) return PropertyData();

        PropertyData result = resolveDirectProperties(trackComp, propertyName, kKeyframeTracksProperties);
        if (result.ref) return result;

        KeyframeTracksComponent& def = getDefaultComponent<KeyframeTracksComponent>();

        if (propertyName == "times") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.times, (void*)&trackComp->times};
        }

        if (propertyName.compare(0, 6, "times[") == 0) {
            size_t pos = 6;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= trackComp->times.size()) {
                return PropertyData();
            }
            static float defValue = 0.0f;
            return {PropertyType::Float, UpdateFlags_None, (void*)&defValue, (void*)&trackComp->times[index]};
        }

        if (propertyName == "easings") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.easings, (void*)&trackComp->easings};
        }

        if (propertyName.compare(0, 8, "easings[") == 0) {
            size_t pos = 8;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= trackComp->easings.size()) {
                return PropertyData();
            }
            static EaseType defValue = EaseType::LINEAR;
            return {PropertyType::Enum, UpdateFlags_None, (void*)&defValue, (void*)&trackComp->easings[index]};
        }

        return PropertyData();
    }

    PropertyData resolveTranslateTracksPropertyFast(void* comp, const std::string& propertyName) {
        TranslateTracksComponent* trackComp = static_cast<TranslateTracksComponent*>(comp);
        if (!trackComp) return PropertyData();

        TranslateTracksComponent& def = getDefaultComponent<TranslateTracksComponent>();

        if (propertyName == "values") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, (void*)&trackComp->values};
        }

        // Hermite tangents (GLTF CUBICSPLINE), kept mirrored with values by key-edit commands
        if (propertyName == "inTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, (void*)&trackComp->inTangents};
        }
        if (propertyName == "outTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, (void*)&trackComp->outTangents};
        }

        if (propertyName.compare(0, 7, "values[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= trackComp->values.size()) {
                return PropertyData();
            }
            static Vector3 defValue = Vector3::ZERO;
            return {PropertyType::Vector3, UpdateFlags_None, (void*)&defValue, (void*)&trackComp->values[index]};
        }

        return PropertyData();
    }

    PropertyData resolveRotateTracksPropertyFast(void* comp, const std::string& propertyName) {
        RotateTracksComponent* trackComp = static_cast<RotateTracksComponent*>(comp);
        if (!trackComp) return PropertyData();

        RotateTracksComponent& def = getDefaultComponent<RotateTracksComponent>();

        if (propertyName == "values") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, (void*)&trackComp->values};
        }

        // Hermite tangents (GLTF CUBICSPLINE), kept mirrored with values by key-edit commands
        if (propertyName == "inTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, (void*)&trackComp->inTangents};
        }
        if (propertyName == "outTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, (void*)&trackComp->outTangents};
        }

        if (propertyName.compare(0, 7, "values[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= trackComp->values.size()) {
                return PropertyData();
            }
            static Quaternion defValue = Quaternion::IDENTITY;
            return {PropertyType::Quat, UpdateFlags_None, (void*)&defValue, (void*)&trackComp->values[index]};
        }

        return PropertyData();
    }

    PropertyData resolveScaleTracksPropertyFast(void* comp, const std::string& propertyName) {
        ScaleTracksComponent* trackComp = static_cast<ScaleTracksComponent*>(comp);
        if (!trackComp) return PropertyData();

        ScaleTracksComponent& def = getDefaultComponent<ScaleTracksComponent>();

        if (propertyName == "values") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, (void*)&trackComp->values};
        }

        // Hermite tangents (GLTF CUBICSPLINE), kept mirrored with values by key-edit commands
        if (propertyName == "inTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, (void*)&trackComp->inTangents};
        }
        if (propertyName == "outTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, (void*)&trackComp->outTangents};
        }

        if (propertyName.compare(0, 7, "values[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= trackComp->values.size()) {
                return PropertyData();
            }
            static Vector3 defValue = Vector3::ZERO;
            return {PropertyType::Vector3, UpdateFlags_None, (void*)&defValue, (void*)&trackComp->values[index]};
        }

        return PropertyData();
    }

    PropertyData resolveMorphTracksPropertyFast(void* comp, const std::string& propertyName) {
        MorphTracksComponent* trackComp = static_cast<MorphTracksComponent*>(comp);
        if (!trackComp) return PropertyData();

        MorphTracksComponent& def = getDefaultComponent<MorphTracksComponent>();

        if (propertyName == "values") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, (void*)&trackComp->values};
        }

        // Hermite tangents (GLTF CUBICSPLINE), kept mirrored with values by key-edit commands
        if (propertyName == "inTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, (void*)&trackComp->inTangents};
        }
        if (propertyName == "outTangents") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, (void*)&trackComp->outTangents};
        }

        if (propertyName.compare(0, 7, "values[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            if (pos + 1 != propertyName.size() || index >= trackComp->values.size()) {
                return PropertyData();
            }
            static std::vector<float> defValue;
            return {PropertyType::Custom, UpdateFlags_None, (void*)&defValue, (void*)&trackComp->values[index]};
        }

        return PropertyData();
    }

    // ── Enumerate functions (build full property map) ──

    void enumerateActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kActionProperties);
    }

    void enumerateTimedActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kTimedActionProperties);
    }

    void enumeratePositionActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kPositionActionProperties);
    }

    void enumerateRotationActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kRotationActionProperties);
    }

    void enumerateScaleActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kScaleActionProperties);
    }

    void enumerateColorActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kColorActionProperties);
    }

    void enumerateAlphaActionProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kAlphaActionProperties);
    }

    void enumerateModelProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        ModelComponent* comp = static_cast<ModelComponent*>(compRef);
        ModelComponent& def = getDefaultComponent<ModelComponent>();

        ps["filename"] = {PropertyType::String, UpdateFlags_Model, (void*)&def.filename, compRef ? (void*)&comp->filename : nullptr};
        ps["mergeStaticMeshes"] = {PropertyType::Bool, UpdateFlags_Model, (void*)&def.mergeStaticMeshes, compRef ? (void*)&comp->mergeStaticMeshes : nullptr};
        ps["skeleton"] = {PropertyType::Entity, UpdateFlags_Mesh_Reload, (void*)&def.skeleton, compRef ? (void*)&comp->skeleton : nullptr};
        ps["animations"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.animations, compRef ? (void*)&comp->animations : nullptr};

        for (size_t i = 0; i < (compRef ? comp->animations.size() : 0); i++) {
            std::string idx = std::to_string(i);
            static Entity defEntity = NULL_ENTITY;
            ps["animations[" + idx + "]"] = {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&comp->animations[i]};
        }

        if (compRef) {
            static Entity defEntity = NULL_ENTITY;
            for (auto& [boneId, boneEntity] : comp->bonesIdMapping) {
                ps["bonesIdMapping[" + std::to_string(boneId) + "]"] = {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&boneEntity};
            }
            for (auto& [boneName, boneEntity] : comp->bonesNameMapping) {
                ps["bonesNameMapping[" + boneName + "]"] = {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&boneEntity};
            }
            // The per-node child mesh entities are entity references too: they must be remapped
            // when the model is duplicated so the copy points at its own generated children
            // rather than sharing (and reloading into) the source model's meshes.
            for (auto& [nodeIdx, nodeEntity] : comp->meshNodesMapping) {
                ps["meshNodesMapping[" + std::to_string(nodeIdx) + "]"] = {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&nodeEntity};
            }
            for (auto& [nodeIdx, nodeEntity] : comp->nodesIdMapping) {
                ps["nodesIdMapping[" + std::to_string(nodeIdx) + "]"] = {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&nodeEntity};
            }
        }
    }

    void enumerateBundleProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kBundleProperties);
    }

    void enumerateBoneProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kBoneProperties);
    }

    void enumerateKeyframeTracksProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        KeyframeTracksComponent* comp = static_cast<KeyframeTracksComponent*>(compRef);
        KeyframeTracksComponent& def = getDefaultComponent<KeyframeTracksComponent>();

        enumerateFromDescriptors(compRef, ps, kKeyframeTracksProperties);

        ps["times"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.times, compRef ? (void*)&comp->times : nullptr};

        static float defValue = 0.0f;
        for (size_t i = 0; i < (compRef ? comp->times.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["times[" + idx + "]"] = {PropertyType::Float, UpdateFlags_None, (void*)&defValue, compRef ? (void*)&comp->times[i] : nullptr};
        }

        ps["easings"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.easings, compRef ? (void*)&comp->easings : nullptr};

        static EaseType defEase = EaseType::LINEAR;
        for (size_t i = 0; i < (compRef ? comp->easings.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["easings[" + idx + "]"] = {PropertyType::Enum, UpdateFlags_None, (void*)&defEase, compRef ? (void*)&comp->easings[i] : nullptr};
        }
    }

    void enumerateTranslateTracksProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        TranslateTracksComponent* comp = static_cast<TranslateTracksComponent*>(compRef);
        TranslateTracksComponent& def = getDefaultComponent<TranslateTracksComponent>();

        ps["values"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, compRef ? (void*)&comp->values : nullptr};
        ps["inTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, compRef ? (void*)&comp->inTangents : nullptr};
        ps["outTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, compRef ? (void*)&comp->outTangents : nullptr};

        static Vector3 defValue = Vector3::ZERO;
        for (size_t i = 0; i < (compRef ? comp->values.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["values[" + idx + "]"] = {PropertyType::Vector3, UpdateFlags_None, (void*)&defValue, compRef ? (void*)&comp->values[i] : nullptr};
        }
    }

    void enumerateRotateTracksProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        RotateTracksComponent* comp = static_cast<RotateTracksComponent*>(compRef);
        RotateTracksComponent& def = getDefaultComponent<RotateTracksComponent>();

        ps["values"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, compRef ? (void*)&comp->values : nullptr};
        ps["inTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, compRef ? (void*)&comp->inTangents : nullptr};
        ps["outTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, compRef ? (void*)&comp->outTangents : nullptr};

        static Quaternion defValue = Quaternion::IDENTITY;
        for (size_t i = 0; i < (compRef ? comp->values.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["values[" + idx + "]"] = {PropertyType::Quat, UpdateFlags_None, (void*)&defValue, compRef ? (void*)&comp->values[i] : nullptr};
        }
    }

    void enumerateScaleTracksProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        ScaleTracksComponent* comp = static_cast<ScaleTracksComponent*>(compRef);
        ScaleTracksComponent& def = getDefaultComponent<ScaleTracksComponent>();

        ps["values"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, compRef ? (void*)&comp->values : nullptr};
        ps["inTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, compRef ? (void*)&comp->inTangents : nullptr};
        ps["outTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, compRef ? (void*)&comp->outTangents : nullptr};

        static Vector3 defValue = Vector3::ZERO;
        for (size_t i = 0; i < (compRef ? comp->values.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["values[" + idx + "]"] = {PropertyType::Vector3, UpdateFlags_None, (void*)&defValue, compRef ? (void*)&comp->values[i] : nullptr};
        }
    }

    void enumerateMorphTracksProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        MorphTracksComponent* comp = static_cast<MorphTracksComponent*>(compRef);
        MorphTracksComponent& def = getDefaultComponent<MorphTracksComponent>();

        ps["values"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, compRef ? (void*)&comp->values : nullptr};
        ps["inTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.inTangents, compRef ? (void*)&comp->inTangents : nullptr};
        ps["outTangents"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.outTangents, compRef ? (void*)&comp->outTangents : nullptr};

        static std::vector<float> defValue;
        for (size_t i = 0; i < (compRef ? comp->values.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["values[" + idx + "]"] = {PropertyType::Custom, UpdateFlags_None, (void*)&defValue, compRef ? (void*)&comp->values[i] : nullptr};
        }
    }

    void enumerateSpriteAnimationProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kSpriteAnimationProperties);
    }

    void enumerateAnimationProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        AnimationComponent* comp = static_cast<AnimationComponent*>(compRef);

        enumerateFromDescriptors(compRef, ps, kAnimationProperties);

        size_t actionCount = compRef ? comp->actions.size() : 0;
        static size_t defActionCount = 0;
        ps["actionFrameCount"] = {PropertyType::UInt, UpdateFlags_None, (void*)&defActionCount, compRef ? (void*)&actionCount : nullptr};

        if (compRef) {
            static Entity defEntity = NULL_ENTITY;
            for (size_t i = 0; i < comp->actions.size(); i++) {
                ps["actions[" + std::to_string(i) + "].action"] = {PropertyType::Entity, UpdateFlags_None, (void*)&defEntity, (void*)&comp->actions[i].action};
            }
        }
    }

    void enumerateTransformProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kTransformProperties);
    }

    void enumerateUIProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kUIProperties);
    }

    void enumerateUILayoutProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kUILayoutProperties);
    }

    void enumerateImageProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kImageProperties);
    }

    void enumerateButtonProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kButtonProperties);
    }

    void enumerateScrollbarProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kScrollbarProperties);
    }

    void enumerateProgressbarProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kProgressbarProperties);
    }

    void enumerateTextEditProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kTextEditProperties);
    }

    void enumeratePanelProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kPanelProperties);
    }

    void enumerateSpriteProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        SpriteComponent* comp = static_cast<SpriteComponent*>(compRef);
        SpriteComponent& def = getDefaultComponent<SpriteComponent>();

        enumerateFromDescriptors(compRef, ps, kSpriteTopProperties);

        ps["numFramesRect"] = {PropertyType::UInt, UpdateFlags_None, (void*)&def.numFramesRect, compRef ? (void*)&comp->numFramesRect : nullptr};

        for (unsigned int i = 0; i < (compRef ? comp->numFramesRect : 0); i++) {
            std::string idx = std::to_string(i);

            SpriteFrameData& frame = comp->framesRect[i];
            SpriteFrameData& defFrame = def.framesRect[0];

            ps["framesRect[" + idx + "].name"] = {PropertyType::String, UpdateFlags_None, (void*)&defFrame.name, (void*)&frame.name};
            ps["framesRect[" + idx + "].rect"] = {PropertyType::Vector4, UpdateFlags_None, (void*)&defFrame.rect, (void*)&frame.rect};
        }
    }

    void enumerateTilemapProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        TilemapComponent* comp = static_cast<TilemapComponent*>(compRef);
        TilemapComponent& def = getDefaultComponent<TilemapComponent>();

        enumerateFromDescriptors(compRef, ps, kTilemapTopProperties);

        ps["numTilesRect"] = {PropertyType::UInt, UpdateFlags_Tilemap, (void*)&def.numTilesRect, compRef ? (void*)&comp->numTilesRect : nullptr};
        ps["numTiles"] = {PropertyType::UInt, UpdateFlags_Tilemap, (void*)&def.numTiles, compRef ? (void*)&comp->numTiles : nullptr};

        for (unsigned int i = 0; i < (compRef ? comp->numTilesRect : 0); i++) {
            std::string idx = std::to_string(i);

            TileRectData& tileRect = comp->tilesRect[i];
            TileRectData& defTileRect = def.tilesRect[0];

            ps["tilesRect[" + idx + "].name"] = {PropertyType::String, UpdateFlags_Tilemap, (void*)&defTileRect.name, (void*)&tileRect.name};
            ps["tilesRect[" + idx + "].submeshId"] = {PropertyType::Int, UpdateFlags_Tilemap, (void*)&defTileRect.submeshId, (void*)&tileRect.submeshId};
            ps["tilesRect[" + idx + "].rect"] = {PropertyType::Vector4, UpdateFlags_Tilemap, (void*)&defTileRect.rect, (void*)&tileRect.rect};
        }

        for (unsigned int i = 0; i < (compRef ? comp->numTiles : 0); i++) {
            std::string idx = std::to_string(i);

            TileData& tile = comp->tiles[i];
            TileData& defTile = def.tiles[0];

            ps["tiles[" + idx + "].name"] = {PropertyType::String, UpdateFlags_Tilemap, (void*)&defTile.name, (void*)&tile.name};
            ps["tiles[" + idx + "].rectId"] = {PropertyType::Int, UpdateFlags_Tilemap, (void*)&defTile.rectId, (void*)&tile.rectId};
            ps["tiles[" + idx + "].position"] = {PropertyType::Vector2, UpdateFlags_Tilemap, (void*)&defTile.position, (void*)&tile.position};
            ps["tiles[" + idx + "].width"] = {PropertyType::Float, UpdateFlags_Tilemap, (void*)&defTile.width, (void*)&tile.width};
            ps["tiles[" + idx + "].height"] = {PropertyType::Float, UpdateFlags_Tilemap, (void*)&defTile.height, (void*)&tile.height};
        }
    }

    void enumerateTerrainProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        TerrainComponent* comp = static_cast<TerrainComponent*>(compRef);
        TerrainComponent& def = getDefaultComponent<TerrainComponent>();

        enumerateFromDescriptors(compRef, ps, kTerrainProperties);

        ps["ranges"] = {PropertyType::Custom, UpdateFlags_Terrain, (void*)&def.ranges, compRef ? (void*)&comp->ranges : nullptr};

        static float defValue = 0.0f;
        for (size_t i = 0; i < (compRef ? comp->ranges.size() : 1); i++) {
            std::string idx = compRef ? std::to_string(i) : "";
            ps["ranges[" + idx + "]"] = {PropertyType::Float, UpdateFlags_Terrain, (void*)&defValue, compRef ? (void*)&comp->ranges[i] : nullptr};
        }

        ps["foliageLayers"] = {PropertyType::Custom, UpdateFlags_Terrain_Foliage, (void*)&def.foliageLayers, compRef ? (void*)&comp->foliageLayers : nullptr};

        static TerrainFoliageLayer defLayer;
        for (size_t i = 0; i < (compRef ? comp->foliageLayers.size() : 1); i++) {
            std::string prefix = "foliageLayers[" + (compRef ? std::to_string(i) : "") + "]";
            TerrainFoliageLayer* layer = compRef ? &comp->foliageLayers[i] : nullptr;
            auto field = [&](const char* name, PropertyType type, void* defRef, void* ref) {
                ps[prefix + name] = {type, UpdateFlags_Terrain_Foliage, defRef, layer ? ref : nullptr};
            };
            field(".meshPath", PropertyType::String, (void*)&defLayer.meshPath, layer ? (void*)&layer->meshPath : nullptr);
            field(".densityMap", PropertyType::Texture, (void*)&defLayer.densityMap, layer ? (void*)&layer->densityMap : nullptr);
            field(".density", PropertyType::Float, (void*)&defLayer.density, layer ? (void*)&layer->density : nullptr);
            field(".minScale", PropertyType::Float, (void*)&defLayer.minScale, layer ? (void*)&layer->minScale : nullptr);
            field(".maxScale", PropertyType::Float, (void*)&defLayer.maxScale, layer ? (void*)&layer->maxScale : nullptr);
            field(".rotationJitter", PropertyType::Float, (void*)&defLayer.rotationJitter, layer ? (void*)&layer->rotationJitter : nullptr);
            field(".alignToNormal", PropertyType::Float, (void*)&defLayer.alignToNormal, layer ? (void*)&layer->alignToNormal : nullptr);
            field(".minSlope", PropertyType::Float, (void*)&defLayer.minSlope, layer ? (void*)&layer->minSlope : nullptr);
            field(".maxSlope", PropertyType::Float, (void*)&defLayer.maxSlope, layer ? (void*)&layer->maxSlope : nullptr);
            field(".drawDistance", PropertyType::Float, (void*)&defLayer.drawDistance, layer ? (void*)&layer->drawDistance : nullptr);
            field(".seed", PropertyType::UInt, (void*)&defLayer.seed, layer ? (void*)&layer->seed : nullptr);
        }
    }

    void enumerateLightProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kLightProperties);
    }

    void enumerateFogProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kFogProperties);
    }

    void enumerateMirrorProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kMirrorProperties);
    }

    void enumerateReflectionProbeProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kReflectionProbeProperties);
    }

    void enumerateLight2DProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kLight2DProperties);
    }

    void enumerateOccluder2DProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kOccluder2DProperties);
    }

    void enumerateCameraProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kCameraProperties);
    }

    void enumerateAudioProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kSoundProperties);
    }

    void enumerateSkyProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kSkyProperties);
    }

    void enumerateTextProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kTextProperties);
    }

    void enumerateJoint2DProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kJoint2DProperties);
    }

    void enumerateJoint3DProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        Joint3DComponent* comp = static_cast<Joint3DComponent*>(compRef);
        Joint3DComponent& def = getDefaultComponent<Joint3DComponent>();

        enumerateFromDescriptors(compRef, ps, kJoint3DProperties);

        ps["pathPoints"] = {PropertyType::Custom, UpdateFlags_Joint3D, (void*)&def.pathPoints, compRef ? (void*)&comp->pathPoints : nullptr};

        for (size_t p = 0; p < (compRef ? comp->pathPoints.size() : 1); p++) {
            std::string idx = compRef ? std::to_string(p) : "";
            ps["pathPoints[" + idx + "]"] = {PropertyType::Vector3, UpdateFlags_Joint3D, (void*)&def.pathPosition, compRef ? (void*)&comp->pathPoints[p] : nullptr};
        }
    }

    void enumerateBody2DProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        Body2DComponent* comp = static_cast<Body2DComponent*>(compRef);
        Body2DComponent& def = getDefaultComponent<Body2DComponent>();

        ps["type"] = {PropertyType::Enum, UpdateFlags_Body2D, (void*)&def.type, compRef ? (void*)&comp->type : nullptr};
        ps["numShapes"] = {PropertyType::UInt, UpdateFlags_Body2D, (void*)&def.numShapes, compRef ? (void*)&comp->numShapes : nullptr};

        for (int s = 0; s < (compRef ? (int)comp->numShapes : 1); s++) {
            std::string idx = compRef ? std::to_string(s) : "";

            Shape2D& shape = compRef ? comp->shapes[s] : def.shapes[0];
            Shape2D& defShape = def.shapes[0];

            ps["shapes[" + idx + "]"] = {PropertyType::Custom, UpdateFlags_Body2D, (void*)&defShape, compRef ? (void*)&shape : nullptr};
            ps["shapes[" + idx + "].type"] = {PropertyType::Enum, UpdateFlags_Body2D, (void*)&defShape.type, compRef ? (void*)&shape.type : nullptr};
            ps["shapes[" + idx + "].pointA"] = {PropertyType::Vector2, UpdateFlags_Body2D, (void*)&defShape.pointA, compRef ? (void*)&shape.pointA : nullptr};
            ps["shapes[" + idx + "].pointB"] = {PropertyType::Vector2, UpdateFlags_Body2D, (void*)&defShape.pointB, compRef ? (void*)&shape.pointB : nullptr};
            ps["shapes[" + idx + "].radius"] = {PropertyType::Float, UpdateFlags_Body2D, (void*)&defShape.radius, compRef ? (void*)&shape.radius : nullptr};
            ps["shapes[" + idx + "].loop"] = {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defShape.loop, compRef ? (void*)&shape.loop : nullptr};
            ps["shapes[" + idx + "].density"] = {PropertyType::Float, UpdateFlags_Body2D, (void*)&defShape.density, compRef ? (void*)&shape.density : nullptr};
            ps["shapes[" + idx + "].friction"] = {PropertyType::Float, UpdateFlags_Body2D, (void*)&defShape.friction, compRef ? (void*)&shape.friction : nullptr};
            ps["shapes[" + idx + "].restitution"] = {PropertyType::Float, UpdateFlags_Body2D, (void*)&defShape.restitution, compRef ? (void*)&shape.restitution : nullptr};
            ps["shapes[" + idx + "].enableHitEvents"] = {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defShape.enableHitEvents, compRef ? (void*)&shape.enableHitEvents : nullptr};
            ps["shapes[" + idx + "].contactEvents"] = {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defShape.contactEvents, compRef ? (void*)&shape.contactEvents : nullptr};
            ps["shapes[" + idx + "].preSolveEvents"] = {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defShape.preSolveEvents, compRef ? (void*)&shape.preSolveEvents : nullptr};
            ps["shapes[" + idx + "].sensorEvents"] = {PropertyType::Bool, UpdateFlags_Body2D, (void*)&defShape.sensorEvents, compRef ? (void*)&shape.sensorEvents : nullptr};

            for (int v = 0; v < (compRef ? (int)shape.numVertices : 1); v++) {
                std::string vidx = compRef ? std::to_string(v) : "";
                ps["shapes[" + idx + "].vertices[" + vidx + "]"] = {PropertyType::Vector2, UpdateFlags_Body2D, (void*)&defShape.vertices[0], compRef ? (void*)&shape.vertices[v] : nullptr};
            }
        }
    }

    void enumerateBody3DProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        Body3DComponent* comp = static_cast<Body3DComponent*>(compRef);
        Body3DComponent& def = getDefaultComponent<Body3DComponent>();

        ps["type"] = {PropertyType::Enum, UpdateFlags_Body3D, (void*)&def.type, compRef ? (void*)&comp->type : nullptr};
        ps["motionQuality"] = {PropertyType::Enum, UpdateFlags_Body3D, (void*)&def.motionQuality, compRef ? (void*)&comp->motionQuality : nullptr};
        ps["sensor"] = {PropertyType::Bool, UpdateFlags_Body3D, (void*)&def.sensor, compRef ? (void*)&comp->sensor : nullptr};
        ps["numShapes"] = {PropertyType::UInt, UpdateFlags_Body3D, (void*)&def.numShapes, compRef ? (void*)&comp->numShapes : nullptr};
        ps["overrideMassProperties"] = {PropertyType::Bool, UpdateFlags_Body3D, (void*)&def.overrideMassProperties, compRef ? (void*)&comp->overrideMassProperties : nullptr};
        ps["solidBoxSize"] = {PropertyType::Vector3, UpdateFlags_Body3D, (void*)&def.solidBoxSize, compRef ? (void*)&comp->solidBoxSize : nullptr};
        ps["solidBoxDensity"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&def.solidBoxDensity, compRef ? (void*)&comp->solidBoxDensity : nullptr};

        for (int s = 0; s < (compRef ? (int)comp->numShapes : 1); s++) {
            std::string idx = compRef ? std::to_string(s) : "";

            Shape3D& shape = compRef ? comp->shapes[s] : def.shapes[0];
            Shape3D& defShape = def.shapes[0];

            ps["shapes[" + idx + "]"] = {PropertyType::Custom, UpdateFlags_Body3D, (void*)&defShape, compRef ? (void*)&shape : nullptr};
            ps["shapes[" + idx + "].type"] = {PropertyType::Enum, UpdateFlags_Body3D, (void*)&defShape.type, compRef ? (void*)&shape.type : nullptr};
            ps["shapes[" + idx + "].position"] = {PropertyType::Vector3, UpdateFlags_Body3D, (void*)&defShape.position, compRef ? (void*)&shape.position : nullptr};
            ps["shapes[" + idx + "].rotation"] = {PropertyType::Quat, UpdateFlags_Body3D, (void*)&defShape.rotation, compRef ? (void*)&shape.rotation : nullptr};
            ps["shapes[" + idx + "].width"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.width, compRef ? (void*)&shape.width : nullptr};
            ps["shapes[" + idx + "].height"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.height, compRef ? (void*)&shape.height : nullptr};
            ps["shapes[" + idx + "].depth"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.depth, compRef ? (void*)&shape.depth : nullptr};
            ps["shapes[" + idx + "].radius"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.radius, compRef ? (void*)&shape.radius : nullptr};
            ps["shapes[" + idx + "].halfHeight"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.halfHeight, compRef ? (void*)&shape.halfHeight : nullptr};
            ps["shapes[" + idx + "].topRadius"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.topRadius, compRef ? (void*)&shape.topRadius : nullptr};
            ps["shapes[" + idx + "].bottomRadius"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.bottomRadius, compRef ? (void*)&shape.bottomRadius : nullptr};
            ps["shapes[" + idx + "].density"] = {PropertyType::Float, UpdateFlags_Body3D, (void*)&defShape.density, compRef ? (void*)&shape.density : nullptr};
            ps["shapes[" + idx + "].source"] = {PropertyType::Enum, UpdateFlags_Body3D, (void*)&defShape.source, compRef ? (void*)&shape.source : nullptr};
            ps["shapes[" + idx + "].sourceEntity"] = {PropertyType::Entity, UpdateFlags_Body3D, (void*)&defShape.sourceEntity, compRef ? (void*)&shape.sourceEntity : nullptr};
            ps["shapes[" + idx + "].samplesSize"] = {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defShape.samplesSize, compRef ? (void*)&shape.samplesSize : nullptr};
            ps["shapes[" + idx + "].numVertices"] = {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defShape.numVertices, compRef ? (void*)&shape.numVertices : nullptr};
            ps["shapes[" + idx + "].numIndices"] = {PropertyType::UInt, UpdateFlags_Body3D, (void*)&defShape.numIndices, compRef ? (void*)&shape.numIndices : nullptr};

            for (int v = 0; v < (compRef ? (int)shape.numVertices : 1); v++) {
                std::string vidx = compRef ? std::to_string(v) : "";
                ps["shapes[" + idx + "].vertices[" + vidx + "]"] = {PropertyType::Vector3, UpdateFlags_Body3D, (void*)&defShape.vertices[0], compRef ? (void*)&shape.vertices[v] : nullptr};
            }
        }
    }

    void enumerateMeshProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        MeshComponent* comp = static_cast<MeshComponent*>(compRef);
        MeshComponent& def = getDefaultComponent<MeshComponent>();

        enumerateFromDescriptors(compRef, ps, kMeshTopProperties);

        for (int s = 0; s < (compRef ? (int)comp->submeshes.size() : 1); s++) {
            std::string idx = compRef ? std::to_string(s) : "";

            Submesh& sub = compRef ? comp->submeshes[s] : def.submeshes[0];
            Submesh& defSub = def.submeshes[0];

            ps["submeshes[" + idx + "].material"] = {PropertyType::Material, UpdateFlags_Mesh_Texture, (void*)&defSub.material, compRef ? (void*)&sub.material : nullptr};
            ps["submeshes[" + idx + "].material.name"] = {PropertyType::String, UpdateFlags_None, (void*)&defSub.material.name, compRef ? (void*)&sub.material.name : nullptr};
            ps["submeshes[" + idx + "].material.baseColorFactor"] = {PropertyType::Vector4, UpdateFlags_None, (void*)&defSub.material.baseColorFactor, compRef ? (void*)&sub.material.baseColorFactor : nullptr};
            ps["submeshes[" + idx + "].material.metallicFactor"] = {PropertyType::Float, UpdateFlags_None, (void*)&defSub.material.metallicFactor, compRef ? (void*)&sub.material.metallicFactor : nullptr};
            ps["submeshes[" + idx + "].material.roughnessFactor"] = {PropertyType::Float, UpdateFlags_None, (void*)&defSub.material.roughnessFactor, compRef ? (void*)&sub.material.roughnessFactor : nullptr};
            ps["submeshes[" + idx + "].material.alphaCutoff"] = {PropertyType::Float, UpdateFlags_None, (void*)&defSub.material.alphaCutoff, compRef ? (void*)&sub.material.alphaCutoff : nullptr};
            ps["submeshes[" + idx + "].material.alphaMode"] = {PropertyType::Enum, UpdateFlags_Mesh_Reload, (void*)&defSub.material.alphaMode, compRef ? (void*)&sub.material.alphaMode : nullptr};
            ps["submeshes[" + idx + "].material.emissiveFactor"] = {PropertyType::Vector3, UpdateFlags_None, (void*)&defSub.material.emissiveFactor, compRef ? (void*)&sub.material.emissiveFactor : nullptr};
            ps["submeshes[" + idx + "].material.baseColorTexture"] = {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.baseColorTexture, compRef ? (void*)&sub.material.baseColorTexture : nullptr};
            ps["submeshes[" + idx + "].material.emissiveTexture"] = {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.emissiveTexture, compRef ? (void*)&sub.material.emissiveTexture : nullptr};
            ps["submeshes[" + idx + "].material.metallicRoughnessTexture"] = {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.metallicRoughnessTexture, compRef ? (void*)&sub.material.metallicRoughnessTexture : nullptr};
            ps["submeshes[" + idx + "].material.occlusionTexture"] = {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.occlusionTexture, compRef ? (void*)&sub.material.occlusionTexture : nullptr};
            ps["submeshes[" + idx + "].material.normalTexture"] = {PropertyType::Texture, UpdateFlags_Mesh_Texture, (void*)&defSub.material.normalTexture, compRef ? (void*)&sub.material.normalTexture : nullptr};

            ps["submeshes[" + idx + "].primitiveType"] = {PropertyType::Enum, UpdateFlags_Mesh_Reload, (void*)&defSub.primitiveType, compRef ? (void*)&sub.primitiveType : nullptr};
            ps["submeshes[" + idx + "].faceCulling"] = {PropertyType::Bool, UpdateFlags_Mesh_Reload, (void*)&defSub.faceCulling, compRef ? (void*)&sub.faceCulling : nullptr};
            ps["submeshes[" + idx + "].textureShadow"] = {PropertyType::Bool, UpdateFlags_Mesh_Reload, (void*)&defSub.textureShadow, compRef ? (void*)&sub.textureShadow : nullptr};
            ps["submeshes[" + idx + "].textureRect"] = {PropertyType::Vector4, UpdateFlags_None, (void*)&defSub.textureRect, compRef ? (void*)&sub.textureRect : nullptr};
        }
    }

    void enumerateUIContainerProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        UIContainerComponent* comp = static_cast<UIContainerComponent*>(compRef);
        UIContainerComponent& def = getDefaultComponent<UIContainerComponent>();

        enumerateFromDescriptors(compRef, ps, kUIContainerTopProperties);

        for (int b = 0; b < (compRef ? MAX_CONTAINER_BOXES : 1); b++) {
            std::string idx = compRef ? std::to_string(b) : "";

            ContainerBox& box = compRef ? comp->boxes[b] : def.boxes[0];
            ContainerBox& defBox = def.boxes[0];
            float* defRect = defBox.rect.ptr();
            float* boxRect = compRef ? box.rect.ptr() : nullptr;

            ps["boxes[" + idx + "].layout"] = {PropertyType::UInt, UpdateFlags_None, (void*)&defBox.layout, compRef ? (void*)&box.layout : nullptr};
            ps["boxes[" + idx + "].expand"] = {PropertyType::Bool, UpdateFlags_Layout_Sizes, (void*)&defBox.expand, compRef ? (void*)&box.expand : nullptr};
            ps["boxes[" + idx + "].rect.x"] = {PropertyType::Float, UpdateFlags_None, (void*)&defRect[0], compRef ? (void*)&boxRect[0] : nullptr};
            ps["boxes[" + idx + "].rect.y"] = {PropertyType::Float, UpdateFlags_None, (void*)&defRect[1], compRef ? (void*)&boxRect[1] : nullptr};
            ps["boxes[" + idx + "].rect.width"] = {PropertyType::Float, UpdateFlags_None, (void*)&defRect[2], compRef ? (void*)&boxRect[2] : nullptr};
            ps["boxes[" + idx + "].rect.height"] = {PropertyType::Float, UpdateFlags_None, (void*)&defRect[3], compRef ? (void*)&boxRect[3] : nullptr};
        }
    }

    void enumerateScriptProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        ScriptComponent* comp = static_cast<ScriptComponent*>(compRef);

        ps["scripts"] = {PropertyType::Custom, UpdateFlags_None, nullptr, compRef ? (void*)&comp->scripts : nullptr};

        if (compRef && comp) {
            for (size_t i = 0; i < comp->scripts.size(); i++) {
                auto& script = comp->scripts[i];

                std::string enabledKey = "scripts[" + std::to_string(i) + "].enabled";
                ps[enabledKey] = {PropertyType::Bool, UpdateFlags_None, nullptr, (void*)&script.enabled};

                for (auto& prop : script.properties) {
                    std::string key = "scripts[" + std::to_string(i) + "]." + prop.name;
                    PropertyData propData;
                    propData.type = Catalog::scriptPropertyTypeToPropertyType(prop.type);
                    propData.updateFlags = UpdateFlags_None;

                    switch (prop.type) {
                        case ScriptPropertyType::Bool:
                            propData.ref = &prop.valueRef<bool>();
                            propData.def = &prop.defaultValueRef<bool>();
                            break;
                        case ScriptPropertyType::Int:
                            propData.ref = &prop.valueRef<int>();
                            propData.def = &prop.defaultValueRef<int>();
                            break;
                        case ScriptPropertyType::Float:
                            propData.ref = &prop.valueRef<float>();
                            propData.def = &prop.defaultValueRef<float>();
                            break;
                        case ScriptPropertyType::String:
                            propData.ref = &prop.valueRef<std::string>();
                            propData.def = &prop.defaultValueRef<std::string>();
                            break;
                        case ScriptPropertyType::Vector2:
                            propData.ref = &prop.valueRef<Vector2>();
                            propData.def = &prop.defaultValueRef<Vector2>();
                            break;
                        case ScriptPropertyType::Vector3:
                        case ScriptPropertyType::Color3:
                            propData.ref = &prop.valueRef<Vector3>();
                            propData.def = &prop.defaultValueRef<Vector3>();
                            break;
                        case ScriptPropertyType::Vector4:
                        case ScriptPropertyType::Color4:
                            propData.ref = &prop.valueRef<Vector4>();
                            propData.def = &prop.defaultValueRef<Vector4>();
                            break;
                        case ScriptPropertyType::EntityReference:
                            propData.ref = &prop.valueRef<EntityReference>().entity;
                            propData.def = &prop.defaultValueRef<EntityReference>().entity;
                            break;
                        default:
                            propData.ref = nullptr;
                            propData.def = nullptr;
                            break;
                    }

                    ps[key] = propData;

                    // Also expose sceneId for EntityReference properties
                    if (prop.type == ScriptPropertyType::EntityReference) {
                        static uint32_t defSceneId = 0;
                        std::string sceneIdKey = key + ".sceneId";
                        ps[sceneIdKey] = {PropertyType::UInt, UpdateFlags_None, (void*)&defSceneId, (void*)&prop.valueRef<EntityReference>().sceneId};
                    }
                }
            }
        }
    }

    void enumerateParticlesProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        ParticlesComponent* comp = static_cast<ParticlesComponent*>(compRef);
        ParticlesComponent& def = getDefaultComponent<ParticlesComponent>();

        ps["maxParticles"] = {PropertyType::UInt, UpdateFlags_None, &def.maxParticles, comp ? &comp->maxParticles : nullptr};
        ps["emitter"] = {PropertyType::Bool, UpdateFlags_None, &def.emitter, comp ? &comp->emitter : nullptr};
        ps["loop"] = {PropertyType::Bool, UpdateFlags_None, &def.loop, comp ? &comp->loop : nullptr};
        ps["localSpace"] = {PropertyType::Bool, UpdateFlags_None, &def.localSpace, comp ? &comp->localSpace : nullptr};
        ps["rate"] = {PropertyType::Int, UpdateFlags_None, &def.rate, comp ? &comp->rate : nullptr};
        ps["maxPerUpdate"] = {PropertyType::Int, UpdateFlags_None, &def.maxPerUpdate, comp ? &comp->maxPerUpdate : nullptr};
        ps["bursts"] = {PropertyType::Custom, UpdateFlags_None, &def.bursts, comp ? &comp->bursts : nullptr};

        ps["lifeInitializer.minLife"] = {PropertyType::Float, UpdateFlags_None, &def.lifeInitializer.minLife, comp ? &comp->lifeInitializer.minLife : nullptr};
        ps["lifeInitializer.maxLife"] = {PropertyType::Float, UpdateFlags_None, &def.lifeInitializer.maxLife, comp ? &comp->lifeInitializer.maxLife : nullptr};

        ps["positionInitializer.shape"] = {PropertyType::Enum, UpdateFlags_None, &def.positionInitializer.shape, comp ? &comp->positionInitializer.shape : nullptr};
        ps["positionInitializer.minPosition"] = {PropertyType::Vector3, UpdateFlags_None, &def.positionInitializer.minPosition, comp ? &comp->positionInitializer.minPosition : nullptr};
        ps["positionInitializer.maxPosition"] = {PropertyType::Vector3, UpdateFlags_None, &def.positionInitializer.maxPosition, comp ? &comp->positionInitializer.maxPosition : nullptr};
        ps["positionInitializer.radius"] = {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.radius, comp ? &comp->positionInitializer.radius : nullptr};
        ps["positionInitializer.innerRadius"] = {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.innerRadius, comp ? &comp->positionInitializer.innerRadius : nullptr};
        ps["positionInitializer.coneAngle"] = {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.coneAngle, comp ? &comp->positionInitializer.coneAngle : nullptr};
        ps["positionInitializer.coneHeight"] = {PropertyType::Float, UpdateFlags_None, &def.positionInitializer.coneHeight, comp ? &comp->positionInitializer.coneHeight : nullptr};
        ps["positionModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.positionModifier.fromTime, comp ? &comp->positionModifier.fromTime : nullptr};
        ps["positionModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.positionModifier.toTime, comp ? &comp->positionModifier.toTime : nullptr};
        ps["positionModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.positionModifier.function, comp ? &comp->positionModifier.function : nullptr};
        ps["positionModifier.fromPosition"] = {PropertyType::Vector3, UpdateFlags_None, &def.positionModifier.fromPosition, comp ? &comp->positionModifier.fromPosition : nullptr};
        ps["positionModifier.toPosition"] = {PropertyType::Vector3, UpdateFlags_None, &def.positionModifier.toPosition, comp ? &comp->positionModifier.toPosition : nullptr};

        ps["velocityInitializer.minVelocity"] = {PropertyType::Vector3, UpdateFlags_None, &def.velocityInitializer.minVelocity, comp ? &comp->velocityInitializer.minVelocity : nullptr};
        ps["velocityInitializer.maxVelocity"] = {PropertyType::Vector3, UpdateFlags_None, &def.velocityInitializer.maxVelocity, comp ? &comp->velocityInitializer.maxVelocity : nullptr};
        ps["velocityModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.velocityModifier.fromTime, comp ? &comp->velocityModifier.fromTime : nullptr};
        ps["velocityModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.velocityModifier.toTime, comp ? &comp->velocityModifier.toTime : nullptr};
        ps["velocityModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.velocityModifier.function, comp ? &comp->velocityModifier.function : nullptr};
        ps["velocityModifier.fromVelocity"] = {PropertyType::Vector3, UpdateFlags_None, &def.velocityModifier.fromVelocity, comp ? &comp->velocityModifier.fromVelocity : nullptr};
        ps["velocityModifier.toVelocity"] = {PropertyType::Vector3, UpdateFlags_None, &def.velocityModifier.toVelocity, comp ? &comp->velocityModifier.toVelocity : nullptr};

        ps["accelerationInitializer.minAcceleration"] = {PropertyType::Vector3, UpdateFlags_None, &def.accelerationInitializer.minAcceleration, comp ? &comp->accelerationInitializer.minAcceleration : nullptr};
        ps["accelerationInitializer.maxAcceleration"] = {PropertyType::Vector3, UpdateFlags_None, &def.accelerationInitializer.maxAcceleration, comp ? &comp->accelerationInitializer.maxAcceleration : nullptr};
        ps["accelerationModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.accelerationModifier.fromTime, comp ? &comp->accelerationModifier.fromTime : nullptr};
        ps["accelerationModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.accelerationModifier.toTime, comp ? &comp->accelerationModifier.toTime : nullptr};
        ps["accelerationModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.accelerationModifier.function, comp ? &comp->accelerationModifier.function : nullptr};
        ps["accelerationModifier.fromAcceleration"] = {PropertyType::Vector3, UpdateFlags_None, &def.accelerationModifier.fromAcceleration, comp ? &comp->accelerationModifier.fromAcceleration : nullptr};
        ps["accelerationModifier.toAcceleration"] = {PropertyType::Vector3, UpdateFlags_None, &def.accelerationModifier.toAcceleration, comp ? &comp->accelerationModifier.toAcceleration : nullptr};

        ps["colorInitializer.minColor"] = {PropertyType::Vector3, UpdateFlags_None, &def.colorInitializer.minColor, comp ? &comp->colorInitializer.minColor : nullptr};
        ps["colorInitializer.maxColor"] = {PropertyType::Vector3, UpdateFlags_None, &def.colorInitializer.maxColor, comp ? &comp->colorInitializer.maxColor : nullptr};
        ps["colorInitializer.useSRGB"] = {PropertyType::Bool, UpdateFlags_None, &def.colorInitializer.useSRGB, comp ? &comp->colorInitializer.useSRGB : nullptr};
        ps["colorModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.colorModifier.fromTime, comp ? &comp->colorModifier.fromTime : nullptr};
        ps["colorModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.colorModifier.toTime, comp ? &comp->colorModifier.toTime : nullptr};
        ps["colorModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.colorModifier.function, comp ? &comp->colorModifier.function : nullptr};
        ps["colorModifier.fromColor"] = {PropertyType::Vector3, UpdateFlags_None, &def.colorModifier.fromColor, comp ? &comp->colorModifier.fromColor : nullptr};
        ps["colorModifier.toColor"] = {PropertyType::Vector3, UpdateFlags_None, &def.colorModifier.toColor, comp ? &comp->colorModifier.toColor : nullptr};
        ps["colorModifier.useSRGB"] = {PropertyType::Bool, UpdateFlags_None, &def.colorModifier.useSRGB, comp ? &comp->colorModifier.useSRGB : nullptr};

        ps["colorGradient"] = {PropertyType::Custom, UpdateFlags_None, &def.colorGradient, comp ? &comp->colorGradient : nullptr};

        ps["alphaInitializer.minAlpha"] = {PropertyType::Float, UpdateFlags_None, &def.alphaInitializer.minAlpha, comp ? &comp->alphaInitializer.minAlpha : nullptr};
        ps["alphaInitializer.maxAlpha"] = {PropertyType::Float, UpdateFlags_None, &def.alphaInitializer.maxAlpha, comp ? &comp->alphaInitializer.maxAlpha : nullptr};
        ps["alphaModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.fromTime, comp ? &comp->alphaModifier.fromTime : nullptr};
        ps["alphaModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.toTime, comp ? &comp->alphaModifier.toTime : nullptr};
        ps["alphaModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.alphaModifier.function, comp ? &comp->alphaModifier.function : nullptr};
        ps["alphaModifier.fromAlpha"] = {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.fromAlpha, comp ? &comp->alphaModifier.fromAlpha : nullptr};
        ps["alphaModifier.toAlpha"] = {PropertyType::Float, UpdateFlags_None, &def.alphaModifier.toAlpha, comp ? &comp->alphaModifier.toAlpha : nullptr};

        ps["sizeInitializer.minSize"] = {PropertyType::Float, UpdateFlags_None, &def.sizeInitializer.minSize, comp ? &comp->sizeInitializer.minSize : nullptr};
        ps["sizeInitializer.maxSize"] = {PropertyType::Float, UpdateFlags_None, &def.sizeInitializer.maxSize, comp ? &comp->sizeInitializer.maxSize : nullptr};
        ps["sizeModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.fromTime, comp ? &comp->sizeModifier.fromTime : nullptr};
        ps["sizeModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.toTime, comp ? &comp->sizeModifier.toTime : nullptr};
        ps["sizeModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.sizeModifier.function, comp ? &comp->sizeModifier.function : nullptr};
        ps["sizeModifier.fromSize"] = {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.fromSize, comp ? &comp->sizeModifier.fromSize : nullptr};
        ps["sizeModifier.toSize"] = {PropertyType::Float, UpdateFlags_None, &def.sizeModifier.toSize, comp ? &comp->sizeModifier.toSize : nullptr};

        ps["spriteInitializer.frames"] = {PropertyType::Custom, UpdateFlags_None, &def.spriteInitializer.frames, comp ? &comp->spriteInitializer.frames : nullptr};
        ps["spriteModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.spriteModifier.fromTime, comp ? &comp->spriteModifier.fromTime : nullptr};
        ps["spriteModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.spriteModifier.toTime, comp ? &comp->spriteModifier.toTime : nullptr};
        ps["spriteModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.spriteModifier.function, comp ? &comp->spriteModifier.function : nullptr};
        ps["spriteModifier.frames"] = {PropertyType::Custom, UpdateFlags_None, &def.spriteModifier.frames, comp ? &comp->spriteModifier.frames : nullptr};

        ps["rotationInitializer.minRotation"] = {PropertyType::Quat, UpdateFlags_None, &def.rotationInitializer.minRotation, comp ? &comp->rotationInitializer.minRotation : nullptr};
        ps["rotationInitializer.maxRotation"] = {PropertyType::Quat, UpdateFlags_None, &def.rotationInitializer.maxRotation, comp ? &comp->rotationInitializer.maxRotation : nullptr};
        ps["rotationInitializer.shortestPath"] = {PropertyType::Bool, UpdateFlags_None, &def.rotationInitializer.shortestPath, comp ? &comp->rotationInitializer.shortestPath : nullptr};
        ps["rotationModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.rotationModifier.fromTime, comp ? &comp->rotationModifier.fromTime : nullptr};
        ps["rotationModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.rotationModifier.toTime, comp ? &comp->rotationModifier.toTime : nullptr};
        ps["rotationModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.rotationModifier.function, comp ? &comp->rotationModifier.function : nullptr};
        ps["rotationModifier.fromRotation"] = {PropertyType::Quat, UpdateFlags_None, &def.rotationModifier.fromRotation, comp ? &comp->rotationModifier.fromRotation : nullptr};
        ps["rotationModifier.toRotation"] = {PropertyType::Quat, UpdateFlags_None, &def.rotationModifier.toRotation, comp ? &comp->rotationModifier.toRotation : nullptr};
        ps["rotationModifier.shortestPath"] = {PropertyType::Bool, UpdateFlags_None, &def.rotationModifier.shortestPath, comp ? &comp->rotationModifier.shortestPath : nullptr};

        ps["scaleInitializer.minScale"] = {PropertyType::Vector3, UpdateFlags_None, &def.scaleInitializer.minScale, comp ? &comp->scaleInitializer.minScale : nullptr};
        ps["scaleInitializer.maxScale"] = {PropertyType::Vector3, UpdateFlags_None, &def.scaleInitializer.maxScale, comp ? &comp->scaleInitializer.maxScale : nullptr};
        ps["scaleInitializer.linearSort"] = {PropertyType::Bool, UpdateFlags_None, &def.scaleInitializer.linearSort, comp ? &comp->scaleInitializer.linearSort : nullptr};
        ps["scaleModifier.fromTime"] = {PropertyType::Float, UpdateFlags_None, &def.scaleModifier.fromTime, comp ? &comp->scaleModifier.fromTime : nullptr};
        ps["scaleModifier.toTime"] = {PropertyType::Float, UpdateFlags_None, &def.scaleModifier.toTime, comp ? &comp->scaleModifier.toTime : nullptr};
        ps["scaleModifier.function"] = {PropertyType::Ease, UpdateFlags_None, &def.scaleModifier.function, comp ? &comp->scaleModifier.function : nullptr};
        ps["scaleModifier.fromScale"] = {PropertyType::Vector3, UpdateFlags_None, &def.scaleModifier.fromScale, comp ? &comp->scaleModifier.fromScale : nullptr};
        ps["scaleModifier.toScale"] = {PropertyType::Vector3, UpdateFlags_None, &def.scaleModifier.toScale, comp ? &comp->scaleModifier.toScale : nullptr};
    }

    PropertyData getPointsPropertyFast(PointsComponent* comp, const std::string& propertyName) {
        PointsComponent& def = getDefaultComponent<PointsComponent>();
        if (!comp) return PropertyData();

        if (propertyName == "maxPoints") return {PropertyType::UInt, UpdateFlags_None, &def.maxPoints, &comp->maxPoints};
        if (propertyName == "transparent") return {PropertyType::Bool, UpdateFlags_None, &def.transparent, &comp->transparent};
        if (propertyName == "autoTransparency") return {PropertyType::Bool, UpdateFlags_None, &def.autoTransparency, &comp->autoTransparency};
        if (propertyName == "texture") return {PropertyType::Texture, UpdateFlags_Points_Texture, &def.texture, &comp->texture};
        if (propertyName == "customShader") return {PropertyType::String, UpdateFlags_Shader_Reload, &def.customShader, &comp->customShader};

        if (propertyName == "numFramesRect") {
            return {PropertyType::UInt, UpdateFlags_None, (void*)&def.numFramesRect, (void*)&comp->numFramesRect};
        }

        if (propertyName.compare(0, 11, "framesRect[") == 0) {
            size_t pos = 11;
            size_t frameIndex = 0;
            if (!parseIndex(propertyName, pos, frameIndex) || pos >= propertyName.size() || propertyName[pos] != ']') {
                return PropertyData();
            }
            SpriteFrameData& frame = comp->framesRect[frameIndex];
            SpriteFrameData& defFrame = def.framesRect[0];
            pos++;
            if (pos == propertyName.size()) {
                return {PropertyType::Custom, UpdateFlags_None, (void*)&defFrame, (void*)&frame};
            }
            if (propertyName[pos] != '.') return PropertyData();
            const size_t fieldPos = pos + 1;
            if (propertyName.compare(fieldPos, 4, "name") == 0 && fieldPos + 4 == propertyName.size()) {
                return {PropertyType::String, UpdateFlags_None, (void*)&defFrame.name, (void*)&frame.name};
            }
            if (propertyName.compare(fieldPos, 4, "rect") == 0 && fieldPos + 4 == propertyName.size()) {
                return {PropertyType::Vector4, UpdateFlags_None, (void*)&defFrame.rect, (void*)&frame.rect};
            }
            return PropertyData();
        }

        if (propertyName == "points") {
            static std::vector<PointData> defPoints;
            return {PropertyType::Custom, UpdateFlags_Points, (void*)&defPoints, (void*)&comp->points};
        }

        if (propertyName.compare(0, 7, "points[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (index >= comp->points.size()) return PropertyData();
            if (pos + 1 >= propertyName.size()) return PropertyData();
            std::string fieldName = propertyName.substr(pos + 1);
            if (fieldName == ".position") {
                static Vector3 defPos = Vector3(0.0f, 0.0f, 0.0f);
                return {PropertyType::Vector3, UpdateFlags_Points, (void*)&defPos, (void*)&comp->points[index].position};
            }
            if (fieldName == ".color") {
                static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_Points, (void*)&defColor, (void*)&comp->points[index].color};
            }
            if (fieldName == ".size") {
                static float defSize = 1.0f;
                return {PropertyType::Float, UpdateFlags_Points, (void*)&defSize, (void*)&comp->points[index].size};
            }
            if (fieldName == ".rotation") {
                static float defRotation = 0.0f;
                return {PropertyType::Float, UpdateFlags_Points, (void*)&defRotation, (void*)&comp->points[index].rotation};
            }
            if (fieldName == ".textureRect") {
                static Rect defRect = Rect(0.0f, 0.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_Points, (void*)&defRect, (void*)&comp->points[index].textureRect};
            }
            if (fieldName == ".visible") {
                static bool defVisible = true;
                return {PropertyType::Bool, UpdateFlags_Points, (void*)&defVisible, (void*)&comp->points[index].visible};
            }
        }

        return PropertyData();
    }

    PropertyData resolvePointsPropertyFast(void* comp, const std::string& propertyName) {
        return getPointsPropertyFast(static_cast<PointsComponent*>(comp), propertyName);
    }

    void enumeratePointsProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        PointsComponent* comp = static_cast<PointsComponent*>(compRef);
        PointsComponent& def = getDefaultComponent<PointsComponent>();

        ps["maxPoints"] = {PropertyType::UInt, UpdateFlags_None, &def.maxPoints, comp ? &comp->maxPoints : nullptr};
        ps["transparent"] = {PropertyType::Bool, UpdateFlags_None, &def.transparent, comp ? &comp->transparent : nullptr};
        ps["autoTransparency"] = {PropertyType::Bool, UpdateFlags_None, &def.autoTransparency, comp ? &comp->autoTransparency : nullptr};
        ps["texture"] = {PropertyType::Texture, UpdateFlags_Points_Texture, &def.texture, comp ? &comp->texture : nullptr};
        ps["customShader"] = {PropertyType::String, UpdateFlags_Shader_Reload, &def.customShader, comp ? &comp->customShader : nullptr};

        static std::vector<PointData> defPoints;
        ps["points"] = {PropertyType::Custom, UpdateFlags_Points, (void*)&defPoints, comp ? (void*)&comp->points : nullptr};

        if (comp) {
            ps["numFramesRect"] = {PropertyType::UInt, UpdateFlags_None, (void*)&def.numFramesRect, (void*)&comp->numFramesRect};
            for (unsigned int i = 0; i < comp->numFramesRect; i++) {
                std::string idx = std::to_string(i);
                ps["framesRect[" + idx + "].name"] = {PropertyType::String, UpdateFlags_None, (void*)&def.framesRect[0].name, (void*)&comp->framesRect[i].name};
                ps["framesRect[" + idx + "].rect"] = {PropertyType::Vector4, UpdateFlags_None, (void*)&def.framesRect[0].rect, (void*)&comp->framesRect[i].rect};
            }
            static Vector3 defPos = Vector3(0.0f, 0.0f, 0.0f);
            static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            static float defSize = 1.0f;
            static float defRotation = 0.0f;
            static Rect defRect = Rect(0.0f, 0.0f, 1.0f, 1.0f);
            static bool defVisible = true;
            for (size_t i = 0; i < comp->points.size(); i++) {
                std::string idx = std::to_string(i);
                ps["points[" + idx + "].position"] = {PropertyType::Vector3, UpdateFlags_Points, (void*)&defPos, (void*)&comp->points[i].position};
                ps["points[" + idx + "].color"] = {PropertyType::Vector4, UpdateFlags_Points, (void*)&defColor, (void*)&comp->points[i].color};
                ps["points[" + idx + "].size"] = {PropertyType::Float, UpdateFlags_Points, (void*)&defSize, (void*)&comp->points[i].size};
                ps["points[" + idx + "].rotation"] = {PropertyType::Float, UpdateFlags_Points, (void*)&defRotation, (void*)&comp->points[i].rotation};
                ps["points[" + idx + "].textureRect"] = {PropertyType::Vector4, UpdateFlags_Points, (void*)&defRect, (void*)&comp->points[i].textureRect};
                ps["points[" + idx + "].visible"] = {PropertyType::Bool, UpdateFlags_Points, (void*)&defVisible, (void*)&comp->points[i].visible};
            }
        }
    }

    PropertyData getLinesPropertyFast(LinesComponent* comp, const std::string& propertyName) {
        LinesComponent& def = getDefaultComponent<LinesComponent>();
        if (!comp) return PropertyData();

        if (propertyName == "maxLines") return {PropertyType::UInt, UpdateFlags_Lines_Reload, &def.maxLines, &comp->maxLines};
        if (propertyName == "customShader") return {PropertyType::String, UpdateFlags_Shader_Reload, &def.customShader, &comp->customShader};

        if (propertyName == "lines") {
            static std::vector<LineData> defLines;
            return {PropertyType::Custom, UpdateFlags_Lines, (void*)&defLines, (void*)&comp->lines};
        }

        if (propertyName.compare(0, 6, "lines[") == 0) {
            size_t pos = 6;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (index >= comp->lines.size()) return PropertyData();
            if (pos + 1 >= propertyName.size()) return PropertyData();

            std::string fieldName = propertyName.substr(pos + 1);
            if (fieldName == ".pointA") {
                static Vector3 defPoint = Vector3(0.0f, 0.0f, 0.0f);
                return {PropertyType::Vector3, UpdateFlags_Lines, (void*)&defPoint, (void*)&comp->lines[index].pointA};
            }
            if (fieldName == ".colorA") {
                static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_Lines, (void*)&defColor, (void*)&comp->lines[index].colorA};
            }
            if (fieldName == ".pointB") {
                static Vector3 defPoint = Vector3(0.0f, 0.0f, 0.0f);
                return {PropertyType::Vector3, UpdateFlags_Lines, (void*)&defPoint, (void*)&comp->lines[index].pointB};
            }
            if (fieldName == ".colorB") {
                static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_Lines, (void*)&defColor, (void*)&comp->lines[index].colorB};
            }
        }

        return PropertyData();
    }

    PropertyData resolveLinesPropertyFast(void* comp, const std::string& propertyName) {
        return getLinesPropertyFast(static_cast<LinesComponent*>(comp), propertyName);
    }

    void enumerateLinesProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        LinesComponent* comp = static_cast<LinesComponent*>(compRef);
        LinesComponent& def = getDefaultComponent<LinesComponent>();

        ps["maxLines"] = {PropertyType::UInt, UpdateFlags_Lines_Reload, &def.maxLines, comp ? &comp->maxLines : nullptr};
        ps["customShader"] = {PropertyType::String, UpdateFlags_Shader_Reload, &def.customShader, comp ? &comp->customShader : nullptr};

        static std::vector<LineData> defLines;
        ps["lines"] = {PropertyType::Custom, UpdateFlags_Lines, (void*)&defLines, comp ? (void*)&comp->lines : nullptr};

        if (comp) {
            static Vector3 defPoint = Vector3(0.0f, 0.0f, 0.0f);
            static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            for (size_t i = 0; i < comp->lines.size(); i++) {
                std::string idx = std::to_string(i);
                ps["lines[" + idx + "].pointA"] = {PropertyType::Vector3, UpdateFlags_Lines, (void*)&defPoint, (void*)&comp->lines[i].pointA};
                ps["lines[" + idx + "].colorA"] = {PropertyType::Vector4, UpdateFlags_Lines, (void*)&defColor, (void*)&comp->lines[i].colorA};
                ps["lines[" + idx + "].pointB"] = {PropertyType::Vector3, UpdateFlags_Lines, (void*)&defPoint, (void*)&comp->lines[i].pointB};
                ps["lines[" + idx + "].colorB"] = {PropertyType::Vector4, UpdateFlags_Lines, (void*)&defColor, (void*)&comp->lines[i].colorB};
            }
        }
    }

    PropertyData getPolygonPropertyFast(PolygonComponent* comp, const std::string& propertyName) {
        PolygonComponent& def = getDefaultComponent<PolygonComponent>();
        if (!comp) return PropertyData();

        if (propertyName == "points") {
            static std::vector<PolygonPoint> defPoints;
            return {PropertyType::Custom, UpdateFlags_UI_Reload, (void*)&defPoints, (void*)&comp->points};
        }

        if (propertyName.compare(0, 7, "points[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (index >= comp->points.size()) return PropertyData();
            if (pos + 1 >= propertyName.size()) return PropertyData();

            std::string fieldName = propertyName.substr(pos + 1);
            if (fieldName == ".position") {
                static Vector3 defPosition = Vector3(0.0f, 0.0f, 0.0f);
                return {PropertyType::Vector3, UpdateFlags_UI_Reload, (void*)&defPosition, (void*)&comp->points[index].position};
            }
            if (fieldName == ".color") {
                static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_UI_Reload, (void*)&defColor, (void*)&comp->points[index].color};
            }
        }

        return PropertyData();
    }

    PropertyData resolvePolygonPropertyFast(void* comp, const std::string& propertyName) {
        return getPolygonPropertyFast(static_cast<PolygonComponent*>(comp), propertyName);
    }

    void enumeratePolygonProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        PolygonComponent* comp = static_cast<PolygonComponent*>(compRef);
        PolygonComponent& def = getDefaultComponent<PolygonComponent>();

        static std::vector<PolygonPoint> defPoints;
        ps["points"] = {PropertyType::Custom, UpdateFlags_UI_Reload, (void*)&defPoints, comp ? (void*)&comp->points : nullptr};

        if (comp) {
            static Vector3 defPosition = Vector3(0.0f, 0.0f, 0.0f);
            static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            for (size_t i = 0; i < comp->points.size(); i++) {
                std::string idx = std::to_string(i);
                ps["points[" + idx + "].position"] = {PropertyType::Vector3, UpdateFlags_UI_Reload, (void*)&defPosition, (void*)&comp->points[i].position};
                ps["points[" + idx + "].color"] = {PropertyType::Vector4, UpdateFlags_UI_Reload, (void*)&defColor, (void*)&comp->points[i].color};
            }
        }
    }

    PropertyData getMeshPolygonPropertyFast(MeshPolygonComponent* comp, const std::string& propertyName) {
        if (!comp) return PropertyData();

        PropertyData result = resolveDirectProperties(comp, propertyName, kMeshPolygonTopProperties);
        if (result.ref) return result;

        if (propertyName == "points") {
            static std::vector<PolygonPoint> defPoints;
            return {PropertyType::Custom, UpdateFlags_Mesh_Reload, (void*)&defPoints, (void*)&comp->points};
        }

        if (propertyName.compare(0, 7, "points[") == 0) {
            size_t pos = 7;
            size_t index = 0;
            if (!parseIndex(propertyName, pos, index) || pos >= propertyName.size() || propertyName[pos] != ']') return PropertyData();
            if (index >= comp->points.size()) return PropertyData();
            if (pos + 1 >= propertyName.size()) return PropertyData();

            std::string fieldName = propertyName.substr(pos + 1);
            if (fieldName == ".position") {
                static Vector3 defPosition = Vector3(0.0f, 0.0f, 0.0f);
                return {PropertyType::Vector3, UpdateFlags_Mesh_Reload, (void*)&defPosition, (void*)&comp->points[index].position};
            }
            if (fieldName == ".color") {
                static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
                return {PropertyType::Vector4, UpdateFlags_Mesh_Reload, (void*)&defColor, (void*)&comp->points[index].color};
            }
        }

        return PropertyData();
    }

    PropertyData resolveMeshPolygonPropertyFast(void* comp, const std::string& propertyName) {
        return getMeshPolygonPropertyFast(static_cast<MeshPolygonComponent*>(comp), propertyName);
    }

    void enumerateMeshPolygonProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        MeshPolygonComponent* comp = static_cast<MeshPolygonComponent*>(compRef);

        enumerateFromDescriptors(compRef, ps, kMeshPolygonTopProperties);

        static std::vector<PolygonPoint> defPoints;
        ps["points"] = {PropertyType::Custom, UpdateFlags_Mesh_Reload, (void*)&defPoints, comp ? (void*)&comp->points : nullptr};

        if (comp) {
            static Vector3 defPosition = Vector3(0.0f, 0.0f, 0.0f);
            static Vector4 defColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            for (size_t i = 0; i < comp->points.size(); i++) {
                std::string idx = std::to_string(i);
                ps["points[" + idx + "].position"] = {PropertyType::Vector3, UpdateFlags_Mesh_Reload, (void*)&defPosition, (void*)&comp->points[i].position};
                ps["points[" + idx + "].color"] = {PropertyType::Vector4, UpdateFlags_Mesh_Reload, (void*)&defColor, (void*)&comp->points[i].color};
            }
        }
    }

    // ── Resolver dispatch table ──

    static const FastComponentResolver kFastComponentResolvers[] = {
        {ComponentType::Transform, &findComponentPtr<Transform>, &resolveTransformPropertyFast, &enumerateTransformProperties},
        {ComponentType::MeshComponent, &findComponentPtr<MeshComponent>, &resolveMeshPropertyFast, &enumerateMeshProperties},
        {ComponentType::UIComponent, &findComponentPtr<UIComponent>, &resolveUIPropertyFast, &enumerateUIProperties},
        {ComponentType::UILayoutComponent, &findComponentPtr<UILayoutComponent>, &resolveUILayoutPropertyFast, &enumerateUILayoutProperties},
        {ComponentType::UIContainerComponent, &findComponentPtr<UIContainerComponent>, &resolveUIContainerPropertyFast, &enumerateUIContainerProperties},
        {ComponentType::ImageComponent, &findComponentPtr<ImageComponent>, &resolveImagePropertyFast, &enumerateImageProperties},
        {ComponentType::ButtonComponent, &findComponentPtr<ButtonComponent>, &resolveButtonPropertyFast, &enumerateButtonProperties},
        {ComponentType::ScrollbarComponent, &findComponentPtr<ScrollbarComponent>, &resolveScrollbarPropertyFast, &enumerateScrollbarProperties},
        {ComponentType::ProgressbarComponent, &findComponentPtr<ProgressbarComponent>, &resolveProgressbarPropertyFast, &enumerateProgressbarProperties},
        {ComponentType::TextEditComponent, &findComponentPtr<TextEditComponent>, &resolveTextEditPropertyFast, &enumerateTextEditProperties},
        {ComponentType::PanelComponent, &findComponentPtr<PanelComponent>, &resolvePanelPropertyFast, &enumeratePanelProperties},
        {ComponentType::PolygonComponent, &findComponentPtr<PolygonComponent>, &resolvePolygonPropertyFast, &enumeratePolygonProperties},
        {ComponentType::MeshPolygonComponent, &findComponentPtr<MeshPolygonComponent>, &resolveMeshPolygonPropertyFast, &enumerateMeshPolygonProperties},
        {ComponentType::SpriteComponent, &findComponentPtr<SpriteComponent>, &resolveSpritePropertyFast, &enumerateSpriteProperties},
        {ComponentType::TilemapComponent, &findComponentPtr<TilemapComponent>, &resolveTilemapPropertyFast, &enumerateTilemapProperties},
        {ComponentType::TerrainComponent, &findComponentPtr<TerrainComponent>, &resolveTerrainPropertyFast, &enumerateTerrainProperties},
        {ComponentType::LightComponent, &findComponentPtr<LightComponent>, &resolveLightPropertyFast, &enumerateLightProperties},
        {ComponentType::FogComponent, &findComponentPtr<FogComponent>, &resolveFogPropertyFast, &enumerateFogProperties},
        {ComponentType::MirrorComponent, &findComponentPtr<MirrorComponent>, &resolveMirrorPropertyFast, &enumerateMirrorProperties},
        {ComponentType::ReflectionProbeComponent, &findComponentPtr<ReflectionProbeComponent>, &resolveReflectionProbePropertyFast, &enumerateReflectionProbeProperties},
        {ComponentType::Light2DComponent, &findComponentPtr<Light2DComponent>, &resolveLight2DPropertyFast, &enumerateLight2DProperties},
        {ComponentType::Occluder2DComponent, &findComponentPtr<Occluder2DComponent>, &resolveOccluder2DPropertyFast, &enumerateOccluder2DProperties},
        {ComponentType::CameraComponent, &findComponentPtr<CameraComponent>, &resolveCameraPropertyFast, &enumerateCameraProperties},
        {ComponentType::SoundComponent, &findComponentPtr<SoundComponent>, &resolveAudioPropertyFast, &enumerateAudioProperties},
        {ComponentType::SkyComponent, &findComponentPtr<SkyComponent>, &resolveSkyPropertyFast, &enumerateSkyProperties},
        {ComponentType::TextComponent, &findComponentPtr<TextComponent>, &resolveTextPropertyFast, &enumerateTextProperties},
        {ComponentType::ScriptComponent, &findComponentPtr<ScriptComponent>, &resolveScriptPropertyFast, &enumerateScriptProperties},
        {ComponentType::Joint2DComponent, &findComponentPtr<Joint2DComponent>, &resolveJoint2DPropertyFast, &enumerateJoint2DProperties},
        {ComponentType::Joint3DComponent, &findComponentPtr<Joint3DComponent>, &resolveJoint3DPropertyFast, &enumerateJoint3DProperties},
        {ComponentType::Body2DComponent, &findComponentPtr<Body2DComponent>, &resolveBody2DPropertyFast, &enumerateBody2DProperties},
        {ComponentType::Body3DComponent, &findComponentPtr<Body3DComponent>, &resolveBody3DPropertyFast, &enumerateBody3DProperties},
        {ComponentType::InstancedMeshComponent, &findComponentPtr<InstancedMeshComponent>, &resolveInstancedMeshPropertyFast, &enumerateInstancedMeshProperties},
        {ComponentType::ParticlesComponent, &findComponentPtr<ParticlesComponent>, &resolveParticlesPropertyFast, &enumerateParticlesProperties},
        {ComponentType::PointsComponent, &findComponentPtr<PointsComponent>, &resolvePointsPropertyFast, &enumeratePointsProperties},
        {ComponentType::LinesComponent, &findComponentPtr<LinesComponent>, &resolveLinesPropertyFast, &enumerateLinesProperties},
        {ComponentType::ActionComponent, &findComponentPtr<ActionComponent>, &resolveActionPropertyFast, &enumerateActionProperties},
        {ComponentType::TimedActionComponent, &findComponentPtr<TimedActionComponent>, &resolveTimedActionPropertyFast, &enumerateTimedActionProperties},
        {ComponentType::PositionActionComponent, &findComponentPtr<PositionActionComponent>, &resolvePositionActionPropertyFast, &enumeratePositionActionProperties},
        {ComponentType::RotationActionComponent, &findComponentPtr<RotationActionComponent>, &resolveRotationActionPropertyFast, &enumerateRotationActionProperties},
        {ComponentType::ScaleActionComponent, &findComponentPtr<ScaleActionComponent>, &resolveScaleActionPropertyFast, &enumerateScaleActionProperties},
        {ComponentType::ColorActionComponent, &findComponentPtr<ColorActionComponent>, &resolveColorActionPropertyFast, &enumerateColorActionProperties},
        {ComponentType::AlphaActionComponent, &findComponentPtr<AlphaActionComponent>, &resolveAlphaActionPropertyFast, &enumerateAlphaActionProperties},
        {ComponentType::SpriteAnimationComponent, &findComponentPtr<SpriteAnimationComponent>, &resolveSpriteAnimationPropertyFast, &enumerateSpriteAnimationProperties},
        {ComponentType::AnimationComponent, &findComponentPtr<AnimationComponent>, &resolveAnimationPropertyFast, &enumerateAnimationProperties},
        {ComponentType::ModelComponent, &findComponentPtr<ModelComponent>, &resolveModelPropertyFast, &enumerateModelProperties},
        {ComponentType::BundleComponent, &findComponentPtr<BundleComponent>, &resolveBundlePropertyFast, &enumerateBundleProperties},
        {ComponentType::BoneComponent, &findComponentPtr<BoneComponent>, &resolveBonePropertyFast, &enumerateBoneProperties},
        {ComponentType::KeyframeTracksComponent, &findComponentPtr<KeyframeTracksComponent>, &resolveKeyframeTracksPropertyFast, &enumerateKeyframeTracksProperties},
        {ComponentType::TranslateTracksComponent, &findComponentPtr<TranslateTracksComponent>, &resolveTranslateTracksPropertyFast, &enumerateTranslateTracksProperties},
        {ComponentType::RotateTracksComponent, &findComponentPtr<RotateTracksComponent>, &resolveRotateTracksPropertyFast, &enumerateRotateTracksProperties},
        {ComponentType::ScaleTracksComponent, &findComponentPtr<ScaleTracksComponent>, &resolveScaleTracksPropertyFast, &enumerateScaleTracksProperties},
        {ComponentType::MorphTracksComponent, &findComponentPtr<MorphTracksComponent>, &resolveMorphTracksPropertyFast, &enumerateMorphTracksProperties},
    };

    PropertyData tryGetFastProperty(EntityRegistry* registry, Entity entity, ComponentType component, const std::string& propertyName) {
        for (const FastComponentResolver& resolver : kFastComponentResolvers) {
            if (resolver.component != component) {
                continue;
            }

            void* comp = resolver.findComponent(registry, entity);
            if (!comp) {
                return PropertyData();
            }

            return resolver.resolve(comp, propertyName);
        }

        return PropertyData();
    }

    bool tryEnumerateProperties(ComponentType component, void* compRef, std::map<std::string, PropertyData>& ps) {
        for (const FastComponentResolver& resolver : kFastComponentResolvers) {
            if (resolver.component == component) {
                resolver.enumerate(compRef, ps);
                return true;
            }
        }
        return false;
    }

    bool tryEnumerateEntityProperties(EntityRegistry* registry, Entity entity, ComponentType component, std::map<std::string, PropertyData>& ps) {
        for (const FastComponentResolver& resolver : kFastComponentResolvers) {
            if (resolver.component == component) {
                void* comp = resolver.findComponent(registry, entity);
                if (!comp) return false;
                resolver.enumerate(comp, ps);
                return true;
            }
        }
        return false;
    }

}

using namespace doriax;

editor::Catalog::Catalog(){
}

std::string editor::Catalog::removeComponentSuffix(std::string str) {
    // Convert to lowercase first
    std::transform(str.begin(), str.end(), str.begin(), 
                   [](unsigned char c) { return std::tolower(c); });

    // Check if string ends with "component"
    const std::string suffix = "component";
    if (str.length() >= suffix.length() && 
        str.substr(str.length() - suffix.length()) == suffix) {
        str.erase(str.length() - suffix.length());
    }

    return str;
}

std::string editor::Catalog::getComponentName(ComponentType component, bool removeSuffix){
    std::string name;

    if(component == ComponentType::ActionComponent){
        name = "ActionComponent";
    }else if(component == ComponentType::AlphaActionComponent){
        name = "AlphaActionComponent";
    }else if(component == ComponentType::AnimationComponent){
        name = "AnimationComponent";
    }else if(component == ComponentType::SoundComponent){
        name = "SoundComponent";
    }else if(component == ComponentType::Body2DComponent){
        name = "Body2DComponent";
    }else if(component == ComponentType::Body3DComponent){
        name = "Body3DComponent";
    }else if(component == ComponentType::BoneComponent){
        name = "BoneComponent";
    }else if(component == ComponentType::BundleComponent){
        name = "BundleComponent";
    }else if(component == ComponentType::ButtonComponent){
        name = "ButtonComponent";
    }else if(component == ComponentType::CameraComponent){
        name = "CameraComponent";
    }else if(component == ComponentType::ColorActionComponent){
        name = "ColorActionComponent";
    }else if(component == ComponentType::FogComponent){
        name = "FogComponent";
    }else if(component == ComponentType::MirrorComponent){
        name = "MirrorComponent";
    }else if(component == ComponentType::ReflectionProbeComponent){
        name = "ReflectionProbeComponent";
    }else if(component == ComponentType::Light2DComponent){
        name = "Light2DComponent";
    }else if(component == ComponentType::Occluder2DComponent){
        name = "Occluder2DComponent";
    }else if(component == ComponentType::ImageComponent){
        name = "ImageComponent";
    }else if(component == ComponentType::InstancedMeshComponent){
        name = "InstancedMeshComponent";
    }else if(component == ComponentType::Joint2DComponent){
        name = "Joint2DComponent";
    }else if(component == ComponentType::Joint3DComponent){
        name = "Joint3DComponent";
    }else if(component == ComponentType::KeyframeTracksComponent){
        name = "KeyframeTracksComponent";
    }else if(component == ComponentType::LightComponent){
        name = "LightComponent";
    }else if(component == ComponentType::LinesComponent){
        name = "LinesComponent";
    }else if(component == ComponentType::MeshComponent){
        name = "MeshComponent";
    }else if(component == ComponentType::MeshPolygonComponent){
        name = "MeshPolygonComponent";
    }else if(component == ComponentType::ModelComponent){
        name = "ModelComponent";
    }else if(component == ComponentType::MorphTracksComponent){
        name = "MorphTracksComponent";
    }else if(component == ComponentType::PanelComponent){
        name = "PanelComponent";
    }else if(component == ComponentType::ParticlesComponent){
        name = "ParticlesComponent";
    }else if(component == ComponentType::PointsComponent){
        name = "PointsComponent";
    }else if(component == ComponentType::PolygonComponent){
        name = "PolygonComponent";
    }else if(component == ComponentType::PositionActionComponent){
        name = "PositionActionComponent";
    }else if(component == ComponentType::ProgressbarComponent){
        name = "ProgressbarComponent";
    }else if(component == ComponentType::RotateTracksComponent){
        name = "RotateTracksComponent";
    }else if(component == ComponentType::RotationActionComponent){
        name = "RotationActionComponent";
    }else if(component == ComponentType::ScaleActionComponent){
        name = "ScaleActionComponent";
    }else if(component == ComponentType::ScaleTracksComponent){
        name = "ScaleTracksComponent";
    }else if(component == ComponentType::ScriptComponent){
        name = "ScriptComponent";
    }else if(component == ComponentType::ScrollbarComponent){
        name = "ScrollbarComponent";
    }else if(component == ComponentType::SkyComponent){
        name = "SkyComponent";
    }else if(component == ComponentType::SpriteAnimationComponent){
        name = "SpriteAnimationComponent";
    }else if(component == ComponentType::SpriteComponent){
        name = "SpriteComponent";
    }else if(component == ComponentType::TerrainComponent){
        name = "TerrainComponent";
    }else if(component == ComponentType::TextComponent){
        name = "TextComponent";
    }else if(component == ComponentType::TextEditComponent){
        name = "TextEditComponent";
    }else if(component == ComponentType::TilemapComponent){
        name = "TilemapComponent";

    }else if(component == ComponentType::TimedActionComponent){
        name = "TimedActionComponent";
    }else if(component == ComponentType::Transform){
        name = "Transform";
    }else if(component == ComponentType::TranslateTracksComponent){
        name = "TranslateTracksComponent";
    }else if(component == ComponentType::UIComponent){
        name = "UIComponent";
    }else if(component == ComponentType::UIContainerComponent){
        name = "UIContainerComponent";
    }else if(component == ComponentType::UILayoutComponent){
        name = "UILayoutComponent";
    }else{
        return "";
    }

    if(removeSuffix){
        return removeComponentSuffix(name);
    }

    return name;
}

ComponentId editor::Catalog::getComponentId(const EntityRegistry* registry, ComponentType compType) {
    using namespace doriax;
    switch (compType) {
        case ComponentType::Transform:
            return registry->getComponentId<Transform>();
        case ComponentType::MeshComponent:
            return registry->getComponentId<MeshComponent>();
        case ComponentType::UIComponent:
            return registry->getComponentId<UIComponent>();
        case ComponentType::UILayoutComponent:
            return registry->getComponentId<UILayoutComponent>();
        case ComponentType::ActionComponent:
            return registry->getComponentId<ActionComponent>();
        case ComponentType::AlphaActionComponent:
            return registry->getComponentId<AlphaActionComponent>();
        case ComponentType::AnimationComponent:
            return registry->getComponentId<AnimationComponent>();
        case ComponentType::SoundComponent:
            return registry->getComponentId<SoundComponent>();
        case ComponentType::Body2DComponent:
            return registry->getComponentId<Body2DComponent>();
        case ComponentType::Body3DComponent:
            return registry->getComponentId<Body3DComponent>();
        case ComponentType::BoneComponent:
            return registry->getComponentId<BoneComponent>();
        case ComponentType::BundleComponent:
            return registry->getComponentId<BundleComponent>();
        case ComponentType::ButtonComponent:
            return registry->getComponentId<ButtonComponent>();
        case ComponentType::CameraComponent:
            return registry->getComponentId<CameraComponent>();
        case ComponentType::ColorActionComponent:
            return registry->getComponentId<ColorActionComponent>();
        case ComponentType::FogComponent:
            return registry->getComponentId<FogComponent>();
        case ComponentType::MirrorComponent:
            return registry->getComponentId<MirrorComponent>();
        case ComponentType::ReflectionProbeComponent:
            return registry->getComponentId<ReflectionProbeComponent>();
        case ComponentType::Light2DComponent:
            return registry->getComponentId<Light2DComponent>();
        case ComponentType::Occluder2DComponent:
            return registry->getComponentId<Occluder2DComponent>();
        case ComponentType::ImageComponent:
            return registry->getComponentId<ImageComponent>();
        case ComponentType::InstancedMeshComponent:
            return registry->getComponentId<InstancedMeshComponent>();
        case ComponentType::Joint2DComponent:
            return registry->getComponentId<Joint2DComponent>();
        case ComponentType::Joint3DComponent:
            return registry->getComponentId<Joint3DComponent>();
        case ComponentType::KeyframeTracksComponent:
            return registry->getComponentId<KeyframeTracksComponent>();
        case ComponentType::LightComponent:
            return registry->getComponentId<LightComponent>();
        case ComponentType::LinesComponent:
            return registry->getComponentId<LinesComponent>();
        case ComponentType::MeshPolygonComponent:
            return registry->getComponentId<MeshPolygonComponent>();
        case ComponentType::ModelComponent:
            return registry->getComponentId<ModelComponent>();
        case ComponentType::MorphTracksComponent:
            return registry->getComponentId<MorphTracksComponent>();
        case ComponentType::PanelComponent:
            return registry->getComponentId<PanelComponent>();
        case ComponentType::ParticlesComponent:
            return registry->getComponentId<ParticlesComponent>();
        case ComponentType::PointsComponent:
            return registry->getComponentId<PointsComponent>();
        case ComponentType::PolygonComponent:
            return registry->getComponentId<PolygonComponent>();
        case ComponentType::PositionActionComponent:
            return registry->getComponentId<PositionActionComponent>();
        case ComponentType::ProgressbarComponent:
            return registry->getComponentId<ProgressbarComponent>();
        case ComponentType::RotateTracksComponent:
            return registry->getComponentId<RotateTracksComponent>();
        case ComponentType::RotationActionComponent:
            return registry->getComponentId<RotationActionComponent>();
        case ComponentType::ScaleActionComponent:
            return registry->getComponentId<ScaleActionComponent>();
        case ComponentType::ScaleTracksComponent:
            return registry->getComponentId<ScaleTracksComponent>();
        case ComponentType::ScriptComponent:
            return registry->getComponentId<ScriptComponent>();
        case ComponentType::ScrollbarComponent:
            return registry->getComponentId<ScrollbarComponent>();
        case ComponentType::SkyComponent:
            return registry->getComponentId<SkyComponent>();
        case ComponentType::SpriteAnimationComponent:
            return registry->getComponentId<SpriteAnimationComponent>();
        case ComponentType::SpriteComponent:
            return registry->getComponentId<SpriteComponent>();
        case ComponentType::TerrainComponent:
            return registry->getComponentId<TerrainComponent>();
        case ComponentType::TextComponent:
            return registry->getComponentId<TextComponent>();
        case ComponentType::TextEditComponent:
            return registry->getComponentId<TextEditComponent>();
        case ComponentType::TilemapComponent:
            return registry->getComponentId<TilemapComponent>();
        case ComponentType::TimedActionComponent:
            return registry->getComponentId<TimedActionComponent>();
        case ComponentType::TranslateTracksComponent:
            return registry->getComponentId<TranslateTracksComponent>();
        case ComponentType::UIContainerComponent:
            return registry->getComponentId<UIContainerComponent>();
        default:
            return 0;
    }
}

editor::ComponentType editor::Catalog::getComponentType(const std::string& componentName) {
    std::string normalizedName = removeComponentSuffix(componentName);

    if(normalizedName == "action"){
        return ComponentType::ActionComponent;
    }else if(normalizedName == "alphaaction"){
        return ComponentType::AlphaActionComponent;
    }else if(normalizedName == "animation"){
        return ComponentType::AnimationComponent;
    }else if(normalizedName == "audio"){
        return ComponentType::SoundComponent;
    }else if(normalizedName == "body2d"){
        return ComponentType::Body2DComponent;
    }else if(normalizedName == "body3d"){
        return ComponentType::Body3DComponent;
    }else if(normalizedName == "bone"){
        return ComponentType::BoneComponent;
    }else if(normalizedName == "bundle"){
        return ComponentType::BundleComponent;
    }else if(normalizedName == "button"){
        return ComponentType::ButtonComponent;
    }else if(normalizedName == "camera"){
        return ComponentType::CameraComponent;
    }else if(normalizedName == "coloraction"){
        return ComponentType::ColorActionComponent;
    }else if(normalizedName == "fog"){
        return ComponentType::FogComponent;
    }else if(normalizedName == "mirror"){
        return ComponentType::MirrorComponent;
    }else if(normalizedName == "reflectionprobe"){
        return ComponentType::ReflectionProbeComponent;
    }else if(normalizedName == "light2d"){
        return ComponentType::Light2DComponent;
    }else if(normalizedName == "occluder2d"){
        return ComponentType::Occluder2DComponent;
    }else if(normalizedName == "image"){
        return ComponentType::ImageComponent;
    }else if(normalizedName == "instancedmesh"){
        return ComponentType::InstancedMeshComponent;
    }else if(normalizedName == "joint2d"){
        return ComponentType::Joint2DComponent;
    }else if(normalizedName == "joint3d"){
        return ComponentType::Joint3DComponent;
    }else if(normalizedName == "keyframetracks"){
        return ComponentType::KeyframeTracksComponent;
    }else if(normalizedName == "light"){
        return ComponentType::LightComponent;
    }else if(normalizedName == "lines"){
        return ComponentType::LinesComponent;
    }else if(normalizedName == "mesh"){
        return ComponentType::MeshComponent;
    }else if(normalizedName == "meshpolygon"){
        return ComponentType::MeshPolygonComponent;
    }else if(normalizedName == "model"){
        return ComponentType::ModelComponent;
    }else if(normalizedName == "morphtracks"){
        return ComponentType::MorphTracksComponent;
    }else if(normalizedName == "panel"){
        return ComponentType::PanelComponent;
    }else if(normalizedName == "particles"){
        return ComponentType::ParticlesComponent;
    }else if(normalizedName == "points"){
        return ComponentType::PointsComponent;
    }else if(normalizedName == "polygon"){
        return ComponentType::PolygonComponent;
    }else if(normalizedName == "positionaction"){
        return ComponentType::PositionActionComponent;
    }else if(normalizedName == "progressbar"){
        return ComponentType::ProgressbarComponent;
    }else if(normalizedName == "rotatetracks"){
        return ComponentType::RotateTracksComponent;
    }else if(normalizedName == "rotationaction"){
        return ComponentType::RotationActionComponent;
    }else if(normalizedName == "scaleaction"){
        return ComponentType::ScaleActionComponent;
    }else if(normalizedName == "scaletracks"){
        return ComponentType::ScaleTracksComponent;
    }else if(normalizedName == "script"){
        return ComponentType::ScriptComponent;
    }else if(normalizedName == "scrollbar"){
        return ComponentType::ScrollbarComponent;
    }else if(normalizedName == "sky"){
        return ComponentType::SkyComponent;
    }else if(normalizedName == "spriteanimation"){
        return ComponentType::SpriteAnimationComponent;
    }else if(normalizedName == "sprite"){
        return ComponentType::SpriteComponent;
    }else if(normalizedName == "terrain"){
        return ComponentType::TerrainComponent;
    }else if(normalizedName == "text"){
        return ComponentType::TextComponent;
    }else if(normalizedName == "textedit"){
        return ComponentType::TextEditComponent;
    }else if(normalizedName == "tilemap"){
        return ComponentType::TilemapComponent;
    }else if(normalizedName == "timedaction"){
        return ComponentType::TimedActionComponent;
    }else if(normalizedName == "transform"){
        return ComponentType::Transform;
    }else if(normalizedName == "translatetracks"){
        return ComponentType::TranslateTracksComponent;
    }else if(normalizedName == "ui"){
        return ComponentType::UIComponent;
    }else if(normalizedName == "uicontainer"){
        return ComponentType::UIContainerComponent;
    }else if(normalizedName == "uilayout"){
        return ComponentType::UILayoutComponent;
    }

    throw std::invalid_argument("Unknown component type: " + componentName);
}

Signature editor::Catalog::componentTypeToSignature(const EntityRegistry* registry, ComponentType compType) {
    Signature signature;
    ComponentId cid = getComponentId(registry, compType);
    signature.set(cid, true);
    return signature;
}

Signature editor::Catalog::componentMaskToSignature(const EntityRegistry* registry, uint64_t mask) {
    Signature signature;
    for (int i = 0; i < 64; ++i) {
        if ((mask >> i) & 1) {
            auto compType = static_cast<ComponentType>(i);
            ComponentId cid = getComponentId(registry, compType);
            signature.set(cid, true);
        }
    }
    return signature;
}

editor::PropertyType editor::Catalog::scriptPropertyTypeToPropertyType(ScriptPropertyType scriptType) {
    switch (scriptType) {
        case doriax::ScriptPropertyType::Bool: return editor::PropertyType::Bool;
        case doriax::ScriptPropertyType::Int: return editor::PropertyType::Int;
        case doriax::ScriptPropertyType::Float: return editor::PropertyType::Float;
        case doriax::ScriptPropertyType::String: return editor::PropertyType::String;
        case doriax::ScriptPropertyType::Vector2: return editor::PropertyType::Vector2;
        case doriax::ScriptPropertyType::Vector3: return editor::PropertyType::Vector3;
        case doriax::ScriptPropertyType::Vector4: return editor::PropertyType::Vector4;
        case doriax::ScriptPropertyType::Color3: return editor::PropertyType::Vector3;
        case doriax::ScriptPropertyType::Color4: return editor::PropertyType::Vector4;
        case doriax::ScriptPropertyType::EntityReference: return PropertyType::EntityReference;
        default: return editor::PropertyType::Custom;
    }
}

std::map<std::string, editor::PropertyData> editor::Catalog::getProperties(ComponentType component, void* compRef){
    std::map<std::string, editor::PropertyData> ps;
    tryEnumerateProperties(component, compRef, ps);
    return ps;
}

std::vector<editor::ComponentType> editor::Catalog::findComponents(EntityRegistry* registry, Entity entity){
    std::vector<editor::ComponentType> ret;

    if (registry->findComponent<ActionComponent>(entity)){
        ret.push_back(ComponentType::ActionComponent);
    }
    if (registry->findComponent<AlphaActionComponent>(entity)){
        ret.push_back(ComponentType::AlphaActionComponent);
    }
    if (registry->findComponent<AnimationComponent>(entity)){
        ret.push_back(ComponentType::AnimationComponent);
    }
    if (registry->findComponent<SoundComponent>(entity)){
        ret.push_back(ComponentType::SoundComponent);
    }
    if (registry->findComponent<Body2DComponent>(entity)){
        ret.push_back(ComponentType::Body2DComponent);
    }
    if (registry->findComponent<Body3DComponent>(entity)){
        ret.push_back(ComponentType::Body3DComponent);
    }
    if (registry->findComponent<BoneComponent>(entity)){
        ret.push_back(ComponentType::BoneComponent);
    }
    if (registry->findComponent<BundleComponent>(entity)){
        ret.push_back(ComponentType::BundleComponent);
    }
    if (registry->findComponent<ButtonComponent>(entity)){
        ret.push_back(ComponentType::ButtonComponent);
    }
    if (registry->findComponent<CameraComponent>(entity)){
        ret.push_back(ComponentType::CameraComponent);
    }
    if (registry->findComponent<ColorActionComponent>(entity)){
        ret.push_back(ComponentType::ColorActionComponent);
    }
    if (registry->findComponent<FogComponent>(entity)){
        ret.push_back(ComponentType::FogComponent);
    }
    if (registry->findComponent<MirrorComponent>(entity)){
        ret.push_back(ComponentType::MirrorComponent);
    }
    if (registry->findComponent<ReflectionProbeComponent>(entity)){
        ret.push_back(ComponentType::ReflectionProbeComponent);
    }
    if (registry->findComponent<Light2DComponent>(entity)){
        ret.push_back(ComponentType::Light2DComponent);
    }
    if (registry->findComponent<Occluder2DComponent>(entity)){
        ret.push_back(ComponentType::Occluder2DComponent);
    }
    if (registry->findComponent<ImageComponent>(entity)){
        ret.push_back(ComponentType::ImageComponent);
    }
    if (registry->findComponent<InstancedMeshComponent>(entity)){
        ret.push_back(ComponentType::InstancedMeshComponent);
    }
    if (registry->findComponent<Joint2DComponent>(entity)){
        ret.push_back(ComponentType::Joint2DComponent);
    }
    if (registry->findComponent<Joint3DComponent>(entity)){
        ret.push_back(ComponentType::Joint3DComponent);
    }
    if (registry->findComponent<KeyframeTracksComponent>(entity)){
        ret.push_back(ComponentType::KeyframeTracksComponent);
    }
    if (registry->findComponent<LightComponent>(entity)){
        ret.push_back(ComponentType::LightComponent);
    }
    if (registry->findComponent<LinesComponent>(entity)){
        ret.push_back(ComponentType::LinesComponent);
    }
    if (registry->findComponent<MeshComponent>(entity)){
        ret.push_back(ComponentType::MeshComponent);
    }
    if (registry->findComponent<MeshPolygonComponent>(entity)){
        ret.push_back(ComponentType::MeshPolygonComponent);
    }
    if (registry->findComponent<ModelComponent>(entity)){
        ret.push_back(ComponentType::ModelComponent);
    }
    if (registry->findComponent<MorphTracksComponent>(entity)){
        ret.push_back(ComponentType::MorphTracksComponent);
    }
    if (registry->findComponent<PanelComponent>(entity)){
        ret.push_back(ComponentType::PanelComponent);
    }
    if (registry->findComponent<ParticlesComponent>(entity)){
        ret.push_back(ComponentType::ParticlesComponent);
    }
    if (registry->findComponent<PointsComponent>(entity)){
        ret.push_back(ComponentType::PointsComponent);
    }
    if (registry->findComponent<PolygonComponent>(entity)){
        ret.push_back(ComponentType::PolygonComponent);
    }
    if (registry->findComponent<PositionActionComponent>(entity)){
        ret.push_back(ComponentType::PositionActionComponent);
    }
    if (registry->findComponent<ProgressbarComponent>(entity)){
        ret.push_back(ComponentType::ProgressbarComponent);
    }
    if (registry->findComponent<RotateTracksComponent>(entity)){
        ret.push_back(ComponentType::RotateTracksComponent);
    }
    if (registry->findComponent<RotationActionComponent>(entity)){
        ret.push_back(ComponentType::RotationActionComponent);
    }
    if (registry->findComponent<ScaleActionComponent>(entity)){
        ret.push_back(ComponentType::ScaleActionComponent);
    }
    if (registry->findComponent<ScaleTracksComponent>(entity)){
        ret.push_back(ComponentType::ScaleTracksComponent);
    }
    if (registry->findComponent<ScriptComponent>(entity)){
        ret.push_back(ComponentType::ScriptComponent);
    }
    if (registry->findComponent<ScrollbarComponent>(entity)){
        ret.push_back(ComponentType::ScrollbarComponent);
    }
    if (registry->findComponent<SkyComponent>(entity)){
        ret.push_back(ComponentType::SkyComponent);
    }
    if (registry->findComponent<SpriteAnimationComponent>(entity)){
        ret.push_back(ComponentType::SpriteAnimationComponent);
    }
    if (registry->findComponent<SpriteComponent>(entity)){
        ret.push_back(ComponentType::SpriteComponent);
    }
    if (registry->findComponent<TerrainComponent>(entity)){
        ret.push_back(ComponentType::TerrainComponent);
    }
    if (registry->findComponent<TextComponent>(entity)){
        ret.push_back(ComponentType::TextComponent);
    }
    if (registry->findComponent<TextEditComponent>(entity)){
        ret.push_back(ComponentType::TextEditComponent);
    }
    if (registry->findComponent<TilemapComponent>(entity)){
        ret.push_back(ComponentType::TilemapComponent);
    }
    if (registry->findComponent<TimedActionComponent>(entity)){
        ret.push_back(ComponentType::TimedActionComponent);
    }
    if (registry->findComponent<Transform>(entity)){
        ret.push_back(ComponentType::Transform);
    }
    if (registry->findComponent<TranslateTracksComponent>(entity)){
        ret.push_back(ComponentType::TranslateTracksComponent);
    }
    if (registry->findComponent<UIComponent>(entity)){
        ret.push_back(ComponentType::UIComponent);
    }
    if (registry->findComponent<UIContainerComponent>(entity)){
        ret.push_back(ComponentType::UIContainerComponent);
    }
    if (registry->findComponent<UILayoutComponent>(entity)){
        ret.push_back(ComponentType::UILayoutComponent);
    }

    return ret;
}

std::map<std::string, editor::PropertyData> editor::Catalog::findEntityProperties(EntityRegistry* registry, Entity entity, ComponentType component){
    std::map<std::string, PropertyData> ps;
    tryEnumerateEntityProperties(registry, entity, component, ps);
    return ps;
}

uint64_t editor::Catalog::getChangedUpdateFlags(ComponentType compType, void* oldComp, void* newComp) {
    if (!oldComp || !newComp) return 0;

    uint64_t flags = 0;

    auto oldProps = Catalog::getProperties(compType, oldComp);
    auto newProps = Catalog::getProperties(compType, newComp);

    for (auto& [name, newProp] : newProps) {
        auto it = oldProps.find(name);
        if (it == oldProps.end()) continue;

        PropertyData& oldProp = it->second;
        if (!oldProp.ref || !newProp.ref) continue;

        // A script property can change type between the two, the casts below would read
        // the old value as the new type
        if (oldProp.type != newProp.type) {
            flags |= newProp.updateFlags;
            continue;
        }

        bool changed = false;
        switch (newProp.type) {
            case PropertyType::Bool:
                changed = *static_cast<bool*>(oldProp.ref) != *static_cast<bool*>(newProp.ref);
                break;
            case PropertyType::Float:
                changed = *static_cast<float*>(oldProp.ref) != *static_cast<float*>(newProp.ref);
                break;
            case PropertyType::Double:
                changed = *static_cast<double*>(oldProp.ref) != *static_cast<double*>(newProp.ref);
                break;
            case PropertyType::Int:
                changed = *static_cast<int*>(oldProp.ref) != *static_cast<int*>(newProp.ref);
                break;
            case PropertyType::UInt:
                changed = *static_cast<unsigned int*>(oldProp.ref) != *static_cast<unsigned int*>(newProp.ref);
                break;
            case PropertyType::String:
                changed = *static_cast<std::string*>(oldProp.ref) != *static_cast<std::string*>(newProp.ref);
                break;
            case PropertyType::Font:
                changed = *static_cast<FontArray*>(oldProp.ref) != *static_cast<FontArray*>(newProp.ref);
                break;
            case PropertyType::Vector2:
                changed = *static_cast<Vector2*>(oldProp.ref) != *static_cast<Vector2*>(newProp.ref);
                break;
            case PropertyType::Vector3:
                changed = *static_cast<Vector3*>(oldProp.ref) != *static_cast<Vector3*>(newProp.ref);
                break;
            case PropertyType::Vector4:
                changed = *static_cast<Vector4*>(oldProp.ref) != *static_cast<Vector4*>(newProp.ref);
                break;
            case PropertyType::Quat:
                changed = *static_cast<Quaternion*>(oldProp.ref) != *static_cast<Quaternion*>(newProp.ref);
                break;
            case PropertyType::Enum:
                changed = *static_cast<int*>(oldProp.ref) != *static_cast<int*>(newProp.ref);
                break;
            case PropertyType::Ease:
                changed = *static_cast<Ease*>(oldProp.ref) != *static_cast<Ease*>(newProp.ref);
                break;
            case PropertyType::Texture:
                changed = *static_cast<Texture*>(oldProp.ref) != *static_cast<Texture*>(newProp.ref);
                break;
            case PropertyType::Material:
                changed = *static_cast<Material*>(oldProp.ref) != *static_cast<Material*>(newProp.ref);
                break;
            case PropertyType::Entity:
                changed = *static_cast<Entity*>(oldProp.ref) != *static_cast<Entity*>(newProp.ref);
                break;
            case PropertyType::EntityReference:
                changed = static_cast<EntityReference*>(oldProp.ref)->entity != static_cast<EntityReference*>(newProp.ref)->entity ||
                          static_cast<EntityReference*>(oldProp.ref)->sceneId != static_cast<EntityReference*>(newProp.ref)->sceneId;
                break;
            case PropertyType::Custom:
                // Conservative: assume changed for complex types
                changed = true;
                break;
        }

        if (changed) {
            flags |= newProp.updateFlags;
        }
    }

    return flags;
}

uint64_t editor::Catalog::getComponentStructuralUpdateFlags(ComponentType compType) {
    switch (compType) {
        case ComponentType::LightComponent:
        // adding/removing a 2D light toggles the USE_LIGHT2D shader variant
        case ComponentType::Light2DComponent:
        case ComponentType::FogComponent:
        // adding/removing a mirror toggles the inverted-culling pipeline baked
        // into meshes, so they must reload
        case ComponentType::MirrorComponent:
        case ComponentType::ReflectionProbeComponent:
            return UpdateFlags_Scene_Mesh_Reload;
        default:
            return UpdateFlags_None;
    }
}

uint32_t* editor::Catalog::getSubmeshOverrideMask(EntityRegistry* registry, Entity entity, ComponentType component, const std::string& propertyName, uint32_t& propertyFields) {
    if (component != ComponentType::MeshComponent) {
        return nullptr;
    }

    size_t submeshIndex = 0;
    if (!parseSubmeshOverrideProperty(propertyName, submeshIndex, propertyFields)) {
        return nullptr;
    }

    MeshComponent* mesh = registry->findComponent<MeshComponent>(entity);
    if (!mesh || submeshIndex >= mesh->numSubmeshes) {
        return nullptr;
    }

    return &mesh->submeshes[submeshIndex].overrideFields;
}

void editor::Catalog::updateEntity(EntityRegistry* registry, Entity entity, uint64_t updateFlags){
    if (updateFlags & UpdateFlags_Transform){
        if (Transform* transform = registry->findComponent<Transform>(entity)){
            transform->needUpdate = true;
        }
    }
    if (updateFlags & UpdateFlags_Camera){
        if (CameraComponent* camera = registry->findComponent<CameraComponent>(entity)){
            camera->needUpdate = true;
        }
    }
    if (updateFlags & UpdateFlags_Scene_Mesh_Reload){
        auto meshes = registry->getComponentArray<MeshComponent>();
        for (int i = 0; i < meshes->size(); i++) {
            MeshComponent& mesh = meshes->getComponentFromIndex(i);
            mesh.needReload = true;
        }
    }
    if (updateFlags & (UpdateFlags_Mesh_Reload | UpdateFlags_Mesh_Texture)){
        if (MeshComponent* mesh = registry->findComponent<MeshComponent>(entity)){
            if (updateFlags & UpdateFlags_Mesh_Reload)
                mesh->needReload = true;
            if (updateFlags & UpdateFlags_Mesh_Texture){
                for (unsigned int i = 0; i < mesh->numSubmeshes; i++)
                    mesh->submeshes[i].needUpdateTexture = true;
            }
        }
        if (updateFlags & UpdateFlags_Mesh_Reload){
            if (MeshPolygonComponent* meshPolygon = registry->findComponent<MeshPolygonComponent>(entity)){
                meshPolygon->needUpdatePolygon = true;
            }
        }
    }
    if (updateFlags & (UpdateFlags_LightShadowMap | UpdateFlags_LightShadowCamera)){
        if (LightComponent* light = registry->findComponent<LightComponent>(entity)){
            if (updateFlags & UpdateFlags_LightShadowMap)
                light->needUpdateShadowMap = true;
            if (updateFlags & UpdateFlags_LightShadowCamera)
                light->needUpdateShadowCamera = true;
        }
    }
    if (updateFlags & (UpdateFlags_UI_Reload | UpdateFlags_UI_Texture)){
        if (UIComponent* ui = registry->findComponent<UIComponent>(entity)){
            if (updateFlags & UpdateFlags_UI_Reload)
                ui->needReload = true;
            if (updateFlags & UpdateFlags_UI_Texture)
                ui->needUpdateTexture = true;
        }
        if (PolygonComponent* polygon = registry->findComponent<PolygonComponent>(entity)){
            if (updateFlags & UpdateFlags_UI_Reload)
                polygon->needUpdatePolygon = true;
        }
    }
    if (updateFlags & UpdateFlags_Image_Patches){
        if (ImageComponent* image = registry->findComponent<ImageComponent>(entity)){
            image->needUpdatePatches = true;
        }
    }
    if (updateFlags & (UpdateFlags_Layout_Sizes | UpdateFlags_Layout_Anchors)){
        // May be requested even if not have UILayoutComponent
        if (UILayoutComponent* layout = registry->findComponent<UILayoutComponent>(entity)){
            if (updateFlags & UpdateFlags_Layout_Sizes)
                layout->needUpdateSizes = true;
            if ((updateFlags & UpdateFlags_Layout_Anchors) && layout->usingAnchors)
                layout->needUpdateAnchorOffsets = true;
        }
    }
    if (updateFlags & (UpdateFlags_Sky | UpdateFlags_Sky_Texture)){
        if (SkyComponent* sky = registry->findComponent<SkyComponent>(entity)){
            if (updateFlags & UpdateFlags_Sky_Texture)
                sky->needUpdateTexture = true;
            if (updateFlags & UpdateFlags_Sky)
                sky->needUpdateSky = true;
        }
    }
    if (updateFlags & UpdateFlags_Reflection_Probe_Capture){
        if (ReflectionProbeComponent* probe = registry->findComponent<ReflectionProbeComponent>(entity)){
            probe->needUpdate = true;
            probe->captureRevision++;
        }
    }
    if (updateFlags & UpdateFlags_Reflection_Probe_Recapture_All){
        auto probes = registry->getComponentArray<ReflectionProbeComponent>();
        for (int i = 0; i < probes->size(); i++) {
            ReflectionProbeComponent& probe = probes->getComponentFromIndex(i);
            probe.needUpdate = true;
            probe.captureRevision++;
        }
    }
    if (updateFlags & UpdateFlags_Shader_Reload){
        // Forked-shader assignment/reset: reload whichever renderable the entity has
        // so it re-fetches its (possibly custom) shader from the pool.
        if (MeshComponent* m = registry->findComponent<MeshComponent>(entity)) m->needReload = true;
        if (UIComponent* u = registry->findComponent<UIComponent>(entity)) u->needReload = true;
        if (PointsComponent* p = registry->findComponent<PointsComponent>(entity)) p->needReload = true;
        if (LinesComponent* l = registry->findComponent<LinesComponent>(entity)) l->needReload = true;
        if (SkyComponent* s = registry->findComponent<SkyComponent>(entity)) s->needReload = true;
    }
    if (updateFlags & UpdateFlags_Sprite){
        if (SpriteComponent* sprite = registry->findComponent<SpriteComponent>(entity)){
            sprite->needUpdateSprite = true;
        }
        if (ActionComponent* action = registry->findComponent<ActionComponent>(entity)){
            if (action->target != NULL_ENTITY){
                if (SpriteComponent* targetSprite = registry->findComponent<SpriteComponent>(action->target)){
                    targetSprite->needUpdateSprite = true;
                }
            }
        }
    }
    if (updateFlags & (UpdateFlags_Text | UpdateFlags_Text_Atlas)){
        if (TextComponent* text = registry->findComponent<TextComponent>(entity)){
            if (updateFlags & UpdateFlags_Text)
                text->needUpdateText = true;
            if (updateFlags & UpdateFlags_Text_Atlas)
                text->needReloadAtlas = true;
        }
    }
    if (updateFlags & UpdateFlags_Body2D){
        if (Body2DComponent* body = registry->findComponent<Body2DComponent>(entity)){
            body->needReloadBody = true;
            body->needUpdateShapes = true;
        }
    }
    if (updateFlags & UpdateFlags_Body3D){
        if (Body3DComponent* body = registry->findComponent<Body3DComponent>(entity)){
            body->needReloadBody = true;
            body->needUpdateShapes = true;
        }
    }
    if (updateFlags & UpdateFlags_Joint2D){
        if (Joint2DComponent* joint = registry->findComponent<Joint2DComponent>(entity)){
            joint->needUpdateJoint = true;
        }
    }
    if (updateFlags & UpdateFlags_Joint3D){
        if (Joint3DComponent* joint = registry->findComponent<Joint3DComponent>(entity)){
            joint->needUpdateJoint = true;
        }
    }
    if (updateFlags & UpdateFlags_Model){
        if (ModelComponent* model = registry->findComponent<ModelComponent>(entity)){
            model->needUpdateModel = true;
        }
    }
    if (updateFlags & UpdateFlags_Tilemap){
        if (TilemapComponent* tilemap = registry->findComponent<TilemapComponent>(entity)){
            tilemap->needUpdateTilemap = true;
        }
    }
    if (updateFlags & (UpdateFlags_Terrain | UpdateFlags_Terrain_Texture | UpdateFlags_Terrain_Foliage)){
        if (TerrainComponent* terrain = registry->findComponent<TerrainComponent>(entity)){
            if (updateFlags & UpdateFlags_Terrain)
                terrain->needUpdateTerrain = true;
            if (updateFlags & UpdateFlags_Terrain_Texture)
                terrain->needUpdateTexture = true;
            if (updateFlags & UpdateFlags_Terrain_Foliage)
                terrain->needUpdateFoliage = true;
        }
    }
    if (updateFlags & UpdateFlags_Instanced_Mesh){
        if (InstancedMeshComponent* instmesh = registry->findComponent<InstancedMeshComponent>(entity)){
            instmesh->needUpdateInstances = true;
        }
    }
    if (updateFlags & (UpdateFlags_Points | UpdateFlags_Points_Texture)){
        if (PointsComponent* pts = registry->findComponent<PointsComponent>(entity)){
            if (updateFlags & UpdateFlags_Points)
                pts->needUpdate = true;
            if (updateFlags & UpdateFlags_Points_Texture)
                pts->needUpdateTexture = true;
        }
    }
    if (updateFlags & (UpdateFlags_Lines | UpdateFlags_Lines_Reload)){
        if (LinesComponent* lines = registry->findComponent<LinesComponent>(entity)){
            bool needsLargerBuffer = lines->lines.size() > lines->maxLines;
            if (needsLargerBuffer){
                lines->maxLines = static_cast<unsigned int>(lines->lines.size());
            }

            if ((updateFlags & UpdateFlags_Lines_Reload) || needsLargerBuffer){
                lines->needReload = true;
            }else{
                lines->needUpdateBuffer = true;
            }
        }
    }
    if (updateFlags & UpdateFlags_Sound){
        if (SoundComponent* audio = registry->findComponent<SoundComponent>(entity)){
            audio->needUpdate = true;
        }
    }
}

void editor::Catalog::copyComponent(EntityRegistry* sourceRegistry, Entity sourceEntity,
                                   EntityRegistry* targetRegistry, Entity targetEntity,
                                   ComponentType compType) {

    switch (compType) {
        case ComponentType::Transform: {
            Entity parent = targetRegistry->getComponent<Transform>(targetEntity).parent;
            YAML::Node encoded = Stream::encodeTransform(sourceRegistry->getComponent<Transform>(sourceEntity));
            targetRegistry->getComponent<Transform>(targetEntity) = Stream::decodeTransform(encoded);
            targetRegistry->getComponent<Transform>(targetEntity).parent = parent; // not need to re-order because it is same parent
            break;
        }

        case ComponentType::MeshComponent: {
            YAML::Node encoded = Stream::encodeMeshComponent(sourceRegistry->getComponent<MeshComponent>(sourceEntity));
            targetRegistry->getComponent<MeshComponent>(targetEntity) = Stream::decodeMeshComponent(encoded);
            break;
        }

        case ComponentType::UIComponent: {
            YAML::Node encoded = Stream::encodeUIComponent(sourceRegistry->getComponent<UIComponent>(sourceEntity));
            targetRegistry->getComponent<UIComponent>(targetEntity) = Stream::decodeUIComponent(encoded);
            break;
        }

        case ComponentType::ButtonComponent: {
            YAML::Node encoded = Stream::encodeButtonComponent(sourceRegistry->getComponent<ButtonComponent>(sourceEntity));
            targetRegistry->getComponent<ButtonComponent>(targetEntity) = Stream::decodeButtonComponent(encoded);
            break;
        }

        case ComponentType::ScrollbarComponent: {
            YAML::Node encoded = Stream::encodeScrollbarComponent(sourceRegistry->getComponent<ScrollbarComponent>(sourceEntity));
            targetRegistry->getComponent<ScrollbarComponent>(targetEntity) = Stream::decodeScrollbarComponent(encoded);
            break;
        }

        case ComponentType::ProgressbarComponent: {
            YAML::Node encoded = Stream::encodeProgressbarComponent(sourceRegistry->getComponent<ProgressbarComponent>(sourceEntity));
            targetRegistry->getComponent<ProgressbarComponent>(targetEntity) = Stream::decodeProgressbarComponent(encoded);
            break;
        }

        case ComponentType::TextEditComponent: {
            YAML::Node encoded = Stream::encodeTextEditComponent(sourceRegistry->getComponent<TextEditComponent>(sourceEntity));
            targetRegistry->getComponent<TextEditComponent>(targetEntity) = Stream::decodeTextEditComponent(encoded);
            break;
        }

        case ComponentType::PanelComponent: {
            YAML::Node encoded = Stream::encodePanelComponent(sourceRegistry->getComponent<PanelComponent>(sourceEntity));
            targetRegistry->getComponent<PanelComponent>(targetEntity) = Stream::decodePanelComponent(encoded);
            break;
        }

        case ComponentType::BundleComponent: {
            YAML::Node encoded = Stream::encodeBundleComponent(sourceRegistry->getComponent<BundleComponent>(sourceEntity));
            targetRegistry->getComponent<BundleComponent>(targetEntity) = Stream::decodeBundleComponent(encoded);
            break;
        }

        case ComponentType::UILayoutComponent: {
            YAML::Node encoded = Stream::encodeUILayoutComponent(sourceRegistry->getComponent<UILayoutComponent>(sourceEntity));
            targetRegistry->getComponent<UILayoutComponent>(targetEntity) = Stream::decodeUILayoutComponent(encoded);
            break;
        }

        case ComponentType::UIContainerComponent: {
            YAML::Node encoded = Stream::encodeUIContainerComponent(sourceRegistry->getComponent<UIContainerComponent>(sourceEntity));
            targetRegistry->getComponent<UIContainerComponent>(targetEntity) = Stream::decodeUIContainerComponent(encoded);
            break;
        }

        case ComponentType::ImageComponent: {
            YAML::Node encoded = Stream::encodeImageComponent(sourceRegistry->getComponent<ImageComponent>(sourceEntity));
            targetRegistry->getComponent<ImageComponent>(targetEntity) = Stream::decodeImageComponent(encoded);
            break;
        }

        case ComponentType::TextComponent: {
            YAML::Node encoded = Stream::encodeTextComponent(sourceRegistry->getComponent<TextComponent>(sourceEntity));
            targetRegistry->getComponent<TextComponent>(targetEntity) = Stream::decodeTextComponent(encoded);
            break;
        }

        case ComponentType::SpriteComponent: {
            YAML::Node encoded = Stream::encodeSpriteComponent(sourceRegistry->getComponent<SpriteComponent>(sourceEntity));
            targetRegistry->getComponent<SpriteComponent>(targetEntity) = Stream::decodeSpriteComponent(encoded);
            break;
        }

        case ComponentType::TilemapComponent: {
            YAML::Node encoded = Stream::encodeTilemapComponent(sourceRegistry->getComponent<TilemapComponent>(sourceEntity));
            targetRegistry->getComponent<TilemapComponent>(targetEntity) = Stream::decodeTilemapComponent(encoded);
            break;
        }

        case ComponentType::TerrainComponent: {
            YAML::Node encoded = Stream::encodeTerrainComponent(sourceRegistry->getComponent<TerrainComponent>(sourceEntity));
            targetRegistry->getComponent<TerrainComponent>(targetEntity) = Stream::decodeTerrainComponent(encoded);
            break;
        }

        case ComponentType::LightComponent: {
            YAML::Node encoded = Stream::encodeLightComponent(sourceRegistry->getComponent<LightComponent>(sourceEntity));
            targetRegistry->getComponent<LightComponent>(targetEntity) = Stream::decodeLightComponent(encoded);
            break;
        }

        case ComponentType::LinesComponent: {
            YAML::Node encoded = Stream::encodeLinesComponent(sourceRegistry->getComponent<LinesComponent>(sourceEntity));
            targetRegistry->getComponent<LinesComponent>(targetEntity) = Stream::decodeLinesComponent(encoded);
            break;
        }

        case ComponentType::PolygonComponent: {
            YAML::Node encoded = Stream::encodePolygonComponent(sourceRegistry->getComponent<PolygonComponent>(sourceEntity));
            targetRegistry->getComponent<PolygonComponent>(targetEntity) = Stream::decodePolygonComponent(encoded);
            break;
        }

        case ComponentType::MeshPolygonComponent: {
            YAML::Node encoded = Stream::encodeMeshPolygonComponent(sourceRegistry->getComponent<MeshPolygonComponent>(sourceEntity));
            targetRegistry->getComponent<MeshPolygonComponent>(targetEntity) = Stream::decodeMeshPolygonComponent(encoded);
            break;
        }

        case ComponentType::FogComponent: {
            YAML::Node encoded = Stream::encodeFogComponent(sourceRegistry->getComponent<FogComponent>(sourceEntity));
            targetRegistry->getComponent<FogComponent>(targetEntity) = Stream::decodeFogComponent(encoded);
            break;
        }

        case ComponentType::MirrorComponent: {
            YAML::Node encoded = Stream::encodeMirrorComponent(sourceRegistry->getComponent<MirrorComponent>(sourceEntity));
            targetRegistry->getComponent<MirrorComponent>(targetEntity) = Stream::decodeMirrorComponent(encoded);
            break;
        }

        case ComponentType::ReflectionProbeComponent: {
            YAML::Node encoded = Stream::encodeReflectionProbeComponent(sourceRegistry->getComponent<ReflectionProbeComponent>(sourceEntity));
            targetRegistry->getComponent<ReflectionProbeComponent>(targetEntity) = Stream::decodeReflectionProbeComponent(encoded);
            break;
        }

        case ComponentType::Light2DComponent: {
            YAML::Node encoded = Stream::encodeLight2DComponent(sourceRegistry->getComponent<Light2DComponent>(sourceEntity));
            targetRegistry->getComponent<Light2DComponent>(targetEntity) = Stream::decodeLight2DComponent(encoded);
            break;
        }

        case ComponentType::Occluder2DComponent: {
            YAML::Node encoded = Stream::encodeOccluder2DComponent(sourceRegistry->getComponent<Occluder2DComponent>(sourceEntity));
            targetRegistry->getComponent<Occluder2DComponent>(targetEntity) = Stream::decodeOccluder2DComponent(encoded);
            break;
        }

        case ComponentType::CameraComponent: {
            YAML::Node encoded = Stream::encodeCameraComponent(sourceRegistry->getComponent<CameraComponent>(sourceEntity));
            targetRegistry->getComponent<CameraComponent>(targetEntity) = Stream::decodeCameraComponent(encoded);
            break;
        }

        case ComponentType::SoundComponent: {
            YAML::Node encoded = Stream::encodeSoundComponent(sourceRegistry->getComponent<SoundComponent>(sourceEntity));
            targetRegistry->getComponent<SoundComponent>(targetEntity) = Stream::decodeSoundComponent(encoded);
            break;
        }

        case ComponentType::SkyComponent: {
            YAML::Node encoded = Stream::encodeSkyComponent(sourceRegistry->getComponent<SkyComponent>(sourceEntity));
            targetRegistry->getComponent<SkyComponent>(targetEntity) = Stream::decodeSkyComponent(encoded);
            break;
        }

        case ComponentType::ScriptComponent: {
            YAML::Node encoded = Stream::encodeScriptComponent(sourceRegistry->getComponent<ScriptComponent>(sourceEntity));
            targetRegistry->getComponent<ScriptComponent>(targetEntity) = Stream::decodeScriptComponent(encoded);
            break;
        }

        case ComponentType::Joint2DComponent: {
            YAML::Node encoded = Stream::encodeJoint2DComponent(sourceRegistry->getComponent<Joint2DComponent>(sourceEntity));
            targetRegistry->getComponent<Joint2DComponent>(targetEntity) = Stream::decodeJoint2DComponent(encoded);
            break;
        }

        case ComponentType::Joint3DComponent: {
            YAML::Node encoded = Stream::encodeJoint3DComponent(sourceRegistry->getComponent<Joint3DComponent>(sourceEntity));
            targetRegistry->getComponent<Joint3DComponent>(targetEntity) = Stream::decodeJoint3DComponent(encoded);
            break;
        }

        case ComponentType::BoneComponent: {
            YAML::Node encoded = Stream::encodeBoneComponent(sourceRegistry->getComponent<BoneComponent>(sourceEntity));
            targetRegistry->getComponent<BoneComponent>(targetEntity) = Stream::decodeBoneComponent(encoded);
            break;
        }

        case ComponentType::KeyframeTracksComponent: {
            YAML::Node encoded = Stream::encodeKeyframeTracksComponent(sourceRegistry->getComponent<KeyframeTracksComponent>(sourceEntity));
            targetRegistry->getComponent<KeyframeTracksComponent>(targetEntity) = Stream::decodeKeyframeTracksComponent(encoded);
            break;
        }

        case ComponentType::TranslateTracksComponent: {
            YAML::Node encoded = Stream::encodeTranslateTracksComponent(sourceRegistry->getComponent<TranslateTracksComponent>(sourceEntity));
            targetRegistry->getComponent<TranslateTracksComponent>(targetEntity) = Stream::decodeTranslateTracksComponent(encoded);
            break;
        }

        case ComponentType::RotateTracksComponent: {
            YAML::Node encoded = Stream::encodeRotateTracksComponent(sourceRegistry->getComponent<RotateTracksComponent>(sourceEntity));
            targetRegistry->getComponent<RotateTracksComponent>(targetEntity) = Stream::decodeRotateTracksComponent(encoded);
            break;
        }

        case ComponentType::ScaleTracksComponent: {
            YAML::Node encoded = Stream::encodeScaleTracksComponent(sourceRegistry->getComponent<ScaleTracksComponent>(sourceEntity));
            targetRegistry->getComponent<ScaleTracksComponent>(targetEntity) = Stream::decodeScaleTracksComponent(encoded);
            break;
        }

        case ComponentType::MorphTracksComponent: {
            YAML::Node encoded = Stream::encodeMorphTracksComponent(sourceRegistry->getComponent<MorphTracksComponent>(sourceEntity));
            targetRegistry->getComponent<MorphTracksComponent>(targetEntity) = Stream::decodeMorphTracksComponent(encoded);
            break;
        }

        case ComponentType::TimedActionComponent: {
            YAML::Node encoded = Stream::encodeTimedActionComponent(sourceRegistry->getComponent<TimedActionComponent>(sourceEntity));
            targetRegistry->getComponent<TimedActionComponent>(targetEntity) = Stream::decodeTimedActionComponent(encoded);
            break;
        }

        case ComponentType::PositionActionComponent: {
            YAML::Node encoded = Stream::encodePositionActionComponent(sourceRegistry->getComponent<PositionActionComponent>(sourceEntity));
            targetRegistry->getComponent<PositionActionComponent>(targetEntity) = Stream::decodePositionActionComponent(encoded);
            break;
        }

        case ComponentType::RotationActionComponent: {
            YAML::Node encoded = Stream::encodeRotationActionComponent(sourceRegistry->getComponent<RotationActionComponent>(sourceEntity));
            targetRegistry->getComponent<RotationActionComponent>(targetEntity) = Stream::decodeRotationActionComponent(encoded);
            break;
        }

        case ComponentType::ScaleActionComponent: {
            YAML::Node encoded = Stream::encodeScaleActionComponent(sourceRegistry->getComponent<ScaleActionComponent>(sourceEntity));
            targetRegistry->getComponent<ScaleActionComponent>(targetEntity) = Stream::decodeScaleActionComponent(encoded);
            break;
        }

        case ComponentType::ColorActionComponent: {
            YAML::Node encoded = Stream::encodeColorActionComponent(sourceRegistry->getComponent<ColorActionComponent>(sourceEntity));
            targetRegistry->getComponent<ColorActionComponent>(targetEntity) = Stream::decodeColorActionComponent(encoded);
            break;
        }

        case ComponentType::AlphaActionComponent: {
            YAML::Node encoded = Stream::encodeAlphaActionComponent(sourceRegistry->getComponent<AlphaActionComponent>(sourceEntity));
            targetRegistry->getComponent<AlphaActionComponent>(targetEntity) = Stream::decodeAlphaActionComponent(encoded);
            break;
        }

        case ComponentType::ParticlesComponent: {
            YAML::Node encoded = Stream::encodeParticlesComponent(sourceRegistry->getComponent<ParticlesComponent>(sourceEntity));
            targetRegistry->getComponent<ParticlesComponent>(targetEntity) = Stream::decodeParticlesComponent(encoded);
            break;
        }

        default:
            printf("WARNING: Unsupported component type for copying: %s\n", getComponentName(compType).c_str());
            break;
    }
}

void editor::Catalog::copyPropertyValue(EntityRegistry* sourceRegistry, Entity sourceEntity, 
                                       EntityRegistry* targetRegistry, Entity targetEntity, 
                                       ComponentType compType, const std::string& property) {

    // Get the property data to determine the type
    auto sourceProperties = Catalog::findEntityProperties(sourceRegistry, sourceEntity, compType);
    auto propIt = sourceProperties.find(property);
    if (propIt == sourceProperties.end()) {
        return; // Property not found
    }

    PropertyType propType = propIt->second.type;

    // Copy based on property type
    switch (propType) {
        case PropertyType::Bool: {
            bool* source = Catalog::getPropertyRef<bool>(sourceRegistry, sourceEntity, compType, property);
            bool* target = Catalog::getPropertyRef<bool>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Float: {
            float* source = Catalog::getPropertyRef<float>(sourceRegistry, sourceEntity, compType, property);
            float* target = Catalog::getPropertyRef<float>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Double: {
            double* source = Catalog::getPropertyRef<double>(sourceRegistry, sourceEntity, compType, property);
            double* target = Catalog::getPropertyRef<double>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Int: {
            int* source = Catalog::getPropertyRef<int>(sourceRegistry, sourceEntity, compType, property);
            int* target = Catalog::getPropertyRef<int>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::UInt: {
            unsigned int* source = Catalog::getPropertyRef<unsigned int>(sourceRegistry, sourceEntity, compType, property);
            unsigned int* target = Catalog::getPropertyRef<unsigned int>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Vector2: {
            Vector2* source = Catalog::getPropertyRef<Vector2>(sourceRegistry, sourceEntity, compType, property);
            Vector2* target = Catalog::getPropertyRef<Vector2>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Vector3: {
            Vector3* source = Catalog::getPropertyRef<Vector3>(sourceRegistry, sourceEntity, compType, property);
            Vector3* target = Catalog::getPropertyRef<Vector3>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Vector4: {
            Vector4* source = Catalog::getPropertyRef<Vector4>(sourceRegistry, sourceEntity, compType, property);
            Vector4* target = Catalog::getPropertyRef<Vector4>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Quat: {
            Quaternion* source = Catalog::getPropertyRef<Quaternion>(sourceRegistry, sourceEntity, compType, property);
            Quaternion* target = Catalog::getPropertyRef<Quaternion>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::String: {
            std::string* source = Catalog::getPropertyRef<std::string>(sourceRegistry, sourceEntity, compType, property);
            std::string* target = Catalog::getPropertyRef<std::string>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Font: {
            FontArray* source = Catalog::getPropertyRef<FontArray>(sourceRegistry, sourceEntity, compType, property);
            FontArray* target = Catalog::getPropertyRef<FontArray>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Material: {
            Material* source = Catalog::getPropertyRef<Material>(sourceRegistry, sourceEntity, compType, property);
            Material* target = Catalog::getPropertyRef<Material>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Texture: {
            Texture* source = Catalog::getPropertyRef<Texture>(sourceRegistry, sourceEntity, compType, property);
            Texture* target = Catalog::getPropertyRef<Texture>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Enum: {
            int* source = Catalog::getPropertyRef<int>(sourceRegistry, sourceEntity, compType, property);
            int* target = Catalog::getPropertyRef<int>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Ease: {
            Ease* source = Catalog::getPropertyRef<Ease>(sourceRegistry, sourceEntity, compType, property);
            Ease* target = Catalog::getPropertyRef<Ease>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Entity: {
            Entity* source = Catalog::getPropertyRef<Entity>(sourceRegistry, sourceEntity, compType, property);
            Entity* target = Catalog::getPropertyRef<Entity>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::EntityReference: {
            EntityReference* source = Catalog::getPropertyRef<EntityReference>(sourceRegistry, sourceEntity, compType, property);
            EntityReference* target = Catalog::getPropertyRef<EntityReference>(targetRegistry, targetEntity, compType, property);
            if (source && target) *target = *source;
            break;
        }
        case PropertyType::Custom:
            if (compType == ComponentType::ScriptComponent) {
                copyComponent(sourceRegistry, sourceEntity, targetRegistry, targetEntity, compType);
            } else if (compType == ComponentType::TerrainComponent && property == "foliageLayers") {
                auto* source = Catalog::getPropertyRef<std::vector<TerrainFoliageLayer>>(sourceRegistry, sourceEntity, compType, property);
                auto* target = Catalog::getPropertyRef<std::vector<TerrainFoliageLayer>>(targetRegistry, targetEntity, compType, property);
                if (source && target) *target = *source;
            }
            break;
        default:
            // For any unknown/unsupported type, do nothing
            break;
    }

    // The override bit travels with the value, or the target keeps it only until its model loads.
    uint32_t propertyFields = 0;
    if (uint32_t* source = getSubmeshOverrideMask(sourceRegistry, sourceEntity, compType, property, propertyFields)) {
        if (uint32_t* target = getSubmeshOverrideMask(targetRegistry, targetEntity, compType, property, propertyFields)) {
            *target = (*target & ~propertyFields) | (*source & propertyFields);
        }
    }

    // Apply any update flags that are associated with this property
    updateEntity(targetRegistry, targetEntity, propIt->second.updateFlags);
}

editor::PropertyData editor::Catalog::findProperty(EntityRegistry* registry, Entity entity, ComponentType component, std::string propertyName){
    return tryGetFastProperty(registry, entity, component, propertyName);
}

editor::PropertyData editor::Catalog::getProperty(EntityRegistry* registry, Entity entity, ComponentType component, std::string propertyName){
    PropertyData fastProperty = findProperty(registry, entity, component, propertyName);
    if (fastProperty.ref || fastProperty.def){
        return fastProperty;
    }

    printf("ERROR: Cannot find property %s\n", propertyName.c_str());
    return PropertyData();
}
