#include "Catalog.h"

#include "Scene.h"
#include "yaml-cpp/yaml.h"
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
        int updateFlags;
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
    constexpr FastPropertyDescriptor makeFastProperty(const char* name, PropertyType type, int updateFlags) {
        return {name, type, updateFlags, &getMemberDef<Component, Member, MemberPtr>, &getMemberRef<Component, Member, MemberPtr>};
    }

    template<typename Component, typename Member, Member Component::*MemberPtr>
    constexpr FastPropertyDescriptor makeFastPropertyNoDefault(const char* name, PropertyType type, int updateFlags) {
        return {name, type, updateFlags, &nullPropertyPtr, &getMemberRef<Component, Member, MemberPtr>};
    }

    constexpr FastPropertyDescriptor makeCustomProperty(const char* name, PropertyType type, int updateFlags, FastDefGetter getDef, FastRefGetter getRef) {
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

    void* getLightShadowNearRef(void* comp) {
        return &static_cast<LightComponent*>(comp)->shadowCameraNearFar.x;
    }

    void* getLightShadowFarRef(void* comp) {
        return &static_cast<LightComponent*>(comp)->shadowCameraNearFar.y;
    }

    static const FastPropertyDescriptor kTransformProperties[] = {
        makeFastProperty<Transform, Vector3, &Transform::position>("position", PropertyType::Vector3, UpdateFlags_Transform | UpdateFlags_Layout_Anchors),
        makeFastProperty<Transform, Quaternion, &Transform::rotation>("rotation", PropertyType::Quat, UpdateFlags_Transform),
        makeFastProperty<Transform, Vector3, &Transform::scale>("scale", PropertyType::Vector3, UpdateFlags_Transform),
        makeFastProperty<Transform, bool, &Transform::visible>("visible", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<Transform, bool, &Transform::billboard>("billboard", PropertyType::Bool, UpdateFlags_Transform),
        makeFastProperty<Transform, bool, &Transform::fakeBillboard>("fakeBillboard", PropertyType::Bool, UpdateFlags_Transform),
        makeFastProperty<Transform, bool, &Transform::cylindricalBillboard>("cylindricalBillboard", PropertyType::Bool, UpdateFlags_Transform),
        makeFastProperty<Transform, Quaternion, &Transform::billboardRotation>("billboardRotation", PropertyType::Quat, UpdateFlags_Transform),
    };

    static const FastPropertyDescriptor kUIProperties[] = {
        makeFastProperty<UIComponent, Vector4, &UIComponent::color>("color", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<UIComponent, Texture, &UIComponent::texture>("texture", PropertyType::Texture, UpdateFlags_UI_Texture),
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
        makeFastProperty<ButtonComponent, Texture, &ButtonComponent::texturePressed>("texturePressed", PropertyType::Texture, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Texture, &ButtonComponent::textureDisabled>("textureDisabled", PropertyType::Texture, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorNormal>("colorNormal", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorPressed>("colorPressed", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, Vector4, &ButtonComponent::colorDisabled>("colorDisabled", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<ButtonComponent, bool, &ButtonComponent::disabled>("disabled", PropertyType::Bool, UpdateFlags_None),
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

    static const FastPropertyDescriptor kActionProperties[] = {
        makeFastPropertyNoDefault<ActionComponent, ActionState, &ActionComponent::state>("state", PropertyType::Enum, UpdateFlags_None),
        makeFastProperty<ActionComponent, float, &ActionComponent::speed>("speed", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<ActionComponent, Entity, &ActionComponent::target>("target", PropertyType::Entity, UpdateFlags_None),
        makeFastProperty<ActionComponent, bool, &ActionComponent::ownedTarget>("ownedTarget", PropertyType::Bool, UpdateFlags_None),
    };

    static const FastPropertyDescriptor kTimedActionProperties[] = {
        makeFastProperty<TimedActionComponent, float, &TimedActionComponent::duration>("duration", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<TimedActionComponent, bool, &TimedActionComponent::loop>("loop", PropertyType::Bool, UpdateFlags_None),
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
        makeFastProperty<SpriteAnimationComponent, std::string, &SpriteAnimationComponent::name>("name", PropertyType::String, UpdateFlags_None),
        makeFastProperty<SpriteAnimationComponent, bool, &SpriteAnimationComponent::loop>("loop", PropertyType::Bool, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, unsigned int, &SpriteAnimationComponent::framesSize>("framesSize", PropertyType::UInt, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, unsigned int, &SpriteAnimationComponent::framesTimeSize>("framesTimeSize", PropertyType::UInt, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, int, &SpriteAnimationComponent::frameIndex>("frameIndex", PropertyType::Int, UpdateFlags_Sprite),
        makeFastPropertyNoDefault<SpriteAnimationComponent, int, &SpriteAnimationComponent::frameTimeIndex>("frameTimeIndex", PropertyType::Int, UpdateFlags_None),
        makeFastPropertyNoDefault<SpriteAnimationComponent, unsigned int, &SpriteAnimationComponent::spriteFrameCount>("spriteFrameCount", PropertyType::UInt, UpdateFlags_Sprite),
    };

    static const FastPropertyDescriptor kAnimationProperties[] = {
        makeFastProperty<AnimationComponent, std::string, &AnimationComponent::name>("name", PropertyType::String, UpdateFlags_None),
        makeFastProperty<AnimationComponent, bool, &AnimationComponent::loop>("loop", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<AnimationComponent, float, &AnimationComponent::duration>("duration", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<AnimationComponent, bool, &AnimationComponent::ownedActions>("ownedActions", PropertyType::Bool, UpdateFlags_None),
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
        makeFastProperty<LightComponent, float, &LightComponent::shadowBias>("shadowBias", PropertyType::Float, UpdateFlags_None),
        makeFastProperty<LightComponent, unsigned int, &LightComponent::mapResolution>("mapResolution", PropertyType::UInt, UpdateFlags_LightShadowMap | UpdateFlags_Scene_Mesh_Reload),
        makeFastProperty<LightComponent, bool, &LightComponent::automaticShadowCamera>("automaticShadowCamera", PropertyType::Bool, UpdateFlags_LightShadowCamera),
        makeCustomProperty("shadowCameraNear", PropertyType::Float, UpdateFlags_LightShadowCamera, &nullPropertyPtr, &getLightShadowNearRef),
        makeCustomProperty("shadowCameraFar", PropertyType::Float, UpdateFlags_LightShadowCamera, &nullPropertyPtr, &getLightShadowFarRef),
        makeFastProperty<LightComponent, unsigned int, &LightComponent::numShadowCascades>("numShadowCascades", PropertyType::UInt, UpdateFlags_LightShadowCamera | UpdateFlags_Scene_Mesh_Reload),
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
        makeFastProperty<CameraComponent, bool, &CameraComponent::renderToTexture>("renderToTexture", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<CameraComponent, bool, &CameraComponent::transparentSort>("transparentSort", PropertyType::Bool, UpdateFlags_None),
        makeFastProperty<CameraComponent, bool, &CameraComponent::useTarget>("useTarget", PropertyType::Bool, UpdateFlags_Camera),
        makeFastProperty<CameraComponent, bool, &CameraComponent::autoResize>("autoResize", PropertyType::Bool, UpdateFlags_Camera),
    };

    static const FastPropertyDescriptor kSkyProperties[] = {
        makeFastProperty<SkyComponent, Texture, &SkyComponent::texture>("texture", PropertyType::Texture, UpdateFlags_Sky_Texture),
        makeFastProperty<SkyComponent, Vector4, &SkyComponent::color>("color", PropertyType::Vector4, UpdateFlags_None),
        makeFastProperty<SkyComponent, float, &SkyComponent::rotation>("rotation", PropertyType::Float, UpdateFlags_Sky),
    };

    static const FastPropertyDescriptor kTextProperties[] = {
        makeFastProperty<TextComponent, std::string, &TextComponent::text>("text", PropertyType::String, UpdateFlags_Text),
        makeFastProperty<TextComponent, std::string, &TextComponent::font>("font", PropertyType::String, UpdateFlags_Text_Atlas),
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
        makeFastProperty<MeshComponent, bool, &MeshComponent::castShadows>("castShadows", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::receiveShadows>("receiveShadows", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::transparent>("transparent", PropertyType::Bool, UpdateFlags_Mesh_Reload),
        makeFastProperty<MeshComponent, bool, &MeshComponent::autoTransparency>("autoTransparency", PropertyType::Bool, UpdateFlags_Mesh_Reload),
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
                        result.ref = const_cast<bool*>(&std::get<bool>(prop.value));
                        result.def = const_cast<bool*>(&std::get<bool>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::Int:
                        result.ref = const_cast<int*>(&std::get<int>(prop.value));
                        result.def = const_cast<int*>(&std::get<int>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::Float:
                        result.ref = const_cast<float*>(&std::get<float>(prop.value));
                        result.def = const_cast<float*>(&std::get<float>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::String:
                        result.ref = const_cast<std::string*>(&std::get<std::string>(prop.value));
                        result.def = const_cast<std::string*>(&std::get<std::string>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::Vector2:
                        result.ref = const_cast<Vector2*>(&std::get<Vector2>(prop.value));
                        result.def = const_cast<Vector2*>(&std::get<Vector2>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::Vector3:
                    case ScriptPropertyType::Color3:
                        result.ref = const_cast<Vector3*>(&std::get<Vector3>(prop.value));
                        result.def = const_cast<Vector3*>(&std::get<Vector3>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::Vector4:
                    case ScriptPropertyType::Color4:
                        result.ref = const_cast<Vector4*>(&std::get<Vector4>(prop.value));
                        result.def = const_cast<Vector4*>(&std::get<Vector4>(prop.defaultValue));
                        break;
                    case ScriptPropertyType::EntityReference:
                        result.ref = const_cast<Entity*>(&std::get<EntityReference>(prop.value).entity);
                        result.def = const_cast<Entity*>(&std::get<EntityReference>(prop.defaultValue).entity);
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
                    return {PropertyType::UInt, UpdateFlags_None, (void*)&defSceneId, (void*)&std::get<EntityReference>(prop.value).sceneId};
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
            return {PropertyType::UInt, UpdateFlags_Sprite, (void*)&def.numFramesRect, (void*)&comp->numFramesRect};
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
            return {PropertyType::Custom, UpdateFlags_Sprite, (void*)&defFrame, (void*)&frame};
        }
        if (propertyName[pos] != '.') {
            return PropertyData();
        }

        const size_t fieldPos = pos + 1;

        if (propertyName.compare(fieldPos, 4, "name") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::String, UpdateFlags_Sprite, (void*)&defFrame.name, (void*)&frame.name};
        }
        if (propertyName.compare(fieldPos, 4, "rect") == 0 && fieldPos + 4 == propertyName.size()) {
            return {PropertyType::Vector4, UpdateFlags_Sprite, (void*)&defFrame.rect, (void*)&frame.rect};
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

    PropertyData resolveLightPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<LightComponent*>(comp), propertyName, kLightProperties);
    }

    PropertyData resolveCameraPropertyFast(void* comp, const std::string& propertyName) {
        return resolveDirectProperties(static_cast<CameraComponent*>(comp), propertyName, kCameraProperties);
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

        return PropertyData();
    }

    PropertyData resolveTranslateTracksPropertyFast(void* comp, const std::string& propertyName) {
        TranslateTracksComponent* trackComp = static_cast<TranslateTracksComponent*>(comp);
        if (!trackComp) return PropertyData();

        TranslateTracksComponent& def = getDefaultComponent<TranslateTracksComponent>();

        if (propertyName == "values") {
            return {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, (void*)&trackComp->values};
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
    }

    void enumerateTranslateTracksProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        TranslateTracksComponent* comp = static_cast<TranslateTracksComponent*>(compRef);
        TranslateTracksComponent& def = getDefaultComponent<TranslateTracksComponent>();

        ps["values"] = {PropertyType::Custom, UpdateFlags_None, (void*)&def.values, compRef ? (void*)&comp->values : nullptr};

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

    void enumerateSpriteProperties(void* compRef, std::map<std::string, PropertyData>& ps) {
        SpriteComponent* comp = static_cast<SpriteComponent*>(compRef);
        SpriteComponent& def = getDefaultComponent<SpriteComponent>();

        enumerateFromDescriptors(compRef, ps, kSpriteTopProperties);

        ps["numFramesRect"] = {PropertyType::UInt, UpdateFlags_Sprite, (void*)&def.numFramesRect, compRef ? (void*)&comp->numFramesRect : nullptr};

        for (unsigned int i = 0; i < (compRef ? comp->numFramesRect : 0); i++) {
            std::string idx = std::to_string(i);

            SpriteFrameData& frame = comp->framesRect[i];
            SpriteFrameData& defFrame = def.framesRect[0];

            ps["framesRect[" + idx + "].name"] = {PropertyType::String, UpdateFlags_Sprite, (void*)&defFrame.name, (void*)&frame.name};
            ps["framesRect[" + idx + "].rect"] = {PropertyType::Vector4, UpdateFlags_Sprite, (void*)&defFrame.rect, (void*)&frame.rect};
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

    void enumerateLightProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kLightProperties);
    }

    void enumerateCameraProperties(void* comp, std::map<std::string, PropertyData>& ps) {
        enumerateFromDescriptors(comp, ps, kCameraProperties);
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
                            propData.ref = &std::get<bool>(prop.value);
                            propData.def = &std::get<bool>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::Int:
                            propData.ref = &std::get<int>(prop.value);
                            propData.def = &std::get<int>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::Float:
                            propData.ref = &std::get<float>(prop.value);
                            propData.def = &std::get<float>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::String:
                            propData.ref = &std::get<std::string>(prop.value);
                            propData.def = &std::get<std::string>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::Vector2:
                            propData.ref = &std::get<Vector2>(prop.value);
                            propData.def = &std::get<Vector2>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::Vector3:
                        case ScriptPropertyType::Color3:
                            propData.ref = &std::get<Vector3>(prop.value);
                            propData.def = &std::get<Vector3>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::Vector4:
                        case ScriptPropertyType::Color4:
                            propData.ref = &std::get<Vector4>(prop.value);
                            propData.def = &std::get<Vector4>(prop.defaultValue);
                            break;
                        case ScriptPropertyType::EntityReference:
                            propData.ref = &std::get<EntityReference>(prop.value).entity;
                            propData.def = &std::get<EntityReference>(prop.defaultValue).entity;
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
                        ps[sceneIdKey] = {PropertyType::UInt, UpdateFlags_None, (void*)&defSceneId, (void*)&std::get<EntityReference>(prop.value).sceneId};
                    }
                }
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
        {ComponentType::SpriteComponent, &findComponentPtr<SpriteComponent>, &resolveSpritePropertyFast, &enumerateSpriteProperties},
        {ComponentType::TilemapComponent, &findComponentPtr<TilemapComponent>, &resolveTilemapPropertyFast, &enumerateTilemapProperties},
        {ComponentType::LightComponent, &findComponentPtr<LightComponent>, &resolveLightPropertyFast, &enumerateLightProperties},
        {ComponentType::CameraComponent, &findComponentPtr<CameraComponent>, &resolveCameraPropertyFast, &enumerateCameraProperties},
        {ComponentType::SkyComponent, &findComponentPtr<SkyComponent>, &resolveSkyPropertyFast, &enumerateSkyProperties},
        {ComponentType::TextComponent, &findComponentPtr<TextComponent>, &resolveTextPropertyFast, &enumerateTextProperties},
        {ComponentType::ScriptComponent, &findComponentPtr<ScriptComponent>, &resolveScriptPropertyFast, &enumerateScriptProperties},
        {ComponentType::Joint2DComponent, &findComponentPtr<Joint2DComponent>, &resolveJoint2DPropertyFast, &enumerateJoint2DProperties},
        {ComponentType::Joint3DComponent, &findComponentPtr<Joint3DComponent>, &resolveJoint3DPropertyFast, &enumerateJoint3DProperties},
        {ComponentType::Body2DComponent, &findComponentPtr<Body2DComponent>, &resolveBody2DPropertyFast, &enumerateBody2DProperties},
        {ComponentType::Body3DComponent, &findComponentPtr<Body3DComponent>, &resolveBody3DPropertyFast, &enumerateBody3DProperties},
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
    }else if(component == ComponentType::AudioComponent){
        name = "AudioComponent";
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
        case ComponentType::AudioComponent:
            return registry->getComponentId<AudioComponent>();
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
        return ComponentType::AudioComponent;
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
    if (registry->findComponent<AudioComponent>(entity)){
        ret.push_back(ComponentType::AudioComponent);
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

int editor::Catalog::getChangedUpdateFlags(ComponentType compType, void* oldComp, void* newComp) {
    if (!oldComp || !newComp) return 0;

    int flags = 0;

    auto oldProps = Catalog::getProperties(compType, oldComp);
    auto newProps = Catalog::getProperties(compType, newComp);

    for (auto& [name, newProp] : newProps) {
        auto it = oldProps.find(name);
        if (it == oldProps.end()) continue;

        PropertyData& oldProp = it->second;
        if (!oldProp.ref || !newProp.ref) continue;

        bool changed = false;
        switch (newProp.type) {
            case PropertyType::Bool:
                changed = *static_cast<bool*>(oldProp.ref) != *static_cast<bool*>(newProp.ref);
                break;
            case PropertyType::Float:
                changed = *static_cast<float*>(oldProp.ref) != *static_cast<float*>(newProp.ref);
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

void editor::Catalog::updateEntity(EntityRegistry* registry, Entity entity, int updateFlags){
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

        case ComponentType::LightComponent: {
            YAML::Node encoded = Stream::encodeLightComponent(sourceRegistry->getComponent<LightComponent>(sourceEntity));
            targetRegistry->getComponent<LightComponent>(targetEntity) = Stream::decodeLightComponent(encoded);
            break;
        }

        case ComponentType::CameraComponent: {
            YAML::Node encoded = Stream::encodeCameraComponent(sourceRegistry->getComponent<CameraComponent>(sourceEntity));
            targetRegistry->getComponent<CameraComponent>(targetEntity) = Stream::decodeCameraComponent(encoded);
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
            }
            break;
        default:
            // For any unknown/unsupported type, do nothing
            break;
    }

    // Apply any update flags that are associated with this property
    updateEntity(targetRegistry, targetEntity, propIt->second.updateFlags);
}

editor::PropertyData editor::Catalog::getProperty(EntityRegistry* registry, Entity entity, ComponentType component, std::string propertyName){
    PropertyData fastProperty = tryGetFastProperty(registry, entity, component, propertyName);
    if (fastProperty.ref || fastProperty.def){
        return fastProperty;
    }

    printf("ERROR: Cannot find property %s\n", propertyName.c_str());
    return PropertyData();
}