#pragma once

#include "yaml-cpp/yaml.h"
#include "Scene.h"
#include "ecs/Entity.h"
#include "Project.h"
#include "math/Vector3.h"
#include "math/Quaternion.h"
#include "math/Matrix4.h"

#include <unordered_map>
#include <unordered_set>

namespace doriax::editor {
    class Stream {
    private:

        static std::string sceneTypeToString(SceneType type);
        static SceneType stringToSceneType(const std::string& str);

        static std::string primitiveTypeToString(PrimitiveType type);
        static PrimitiveType stringToPrimitiveType(const std::string& str);

        static std::string bufferTypeToString(BufferType type);
        static BufferType stringToBufferType(const std::string& str);

        static std::string bufferUsageToString(BufferUsage usage);
        static BufferUsage stringToBufferUsage(const std::string& str);

        static std::string attributeTypeToString(AttributeType type);
        static AttributeType stringToAttributeType(const std::string& str);

        static std::string attributeDataTypeToString(AttributeDataType type);
        static AttributeDataType stringToAttributeDataType(const std::string& str);

        static std::string cullingModeToString(CullingMode mode);
        static CullingMode stringToCullingMode(const std::string& str);

        static std::string windingOrderToString(WindingOrder order);
        static WindingOrder stringToWindingOrder(const std::string& str);

        static std::string textureFilterToString(TextureFilter filter);
        static TextureFilter stringToTextureFilter(const std::string& str);

        static std::string textureWrapToString(TextureWrap wrap);
        static TextureWrap stringToTextureWrap(const std::string& str);

        static std::string lightTypeToString(LightType type);
        static LightType stringToLightType(const std::string& str);

        static std::string lightStateToString(LightState state);
        static LightState stringToLightState(const std::string& str);

        static std::string uiEventStateToString(UIEventState state);
        static UIEventState stringToUIEventState(const std::string& str);

        static std::string cameraTypeToString(CameraType type);
        static CameraType stringToCameraType(const std::string& str);

        static std::string pivotPresetToString(PivotPreset preset);
        static PivotPreset stringToPivotPreset(const std::string& str);

        static std::string scriptTypeToString(ScriptType type);
        static ScriptType stringToScriptType(const std::string& str);

        static std::string containerTypeToString(ContainerType type);
        static ContainerType stringToContainerType(const std::string& str);

        static std::string scalingModeToString(Scaling mode);
        static Scaling stringToScalingMode(const std::string& str);

        static std::string textureStrategyToString(TextureStrategy strategy);
        static TextureStrategy stringToTextureStrategy(const std::string& str);

        // ==============================

        static YAML::Node encodeVector2(const Vector2& vec);
        static Vector2 decodeVector2(const YAML::Node& node);

        static YAML::Node encodeVector3(const Vector3& vec);
        static Vector3 decodeVector3(const YAML::Node& node);

        static YAML::Node encodeVector4(const Vector4& vec);
        static Vector4 decodeVector4(const YAML::Node& node);

        static YAML::Node encodeQuaternion(const Quaternion& quat);
        static Quaternion decodeQuaternion(const YAML::Node& node);

        static YAML::Node encodeRect(const Rect& rect);
        static Rect decodeRect(const YAML::Node& node);

        static YAML::Node encodeMatrix4(const Matrix4& mat);
        static Matrix4 decodeMatrix4(const YAML::Node& node);

        static YAML::Node encodeTexture(const Texture& texture);
        static Texture decodeTexture(const YAML::Node& node);

        static YAML::Node encodeBuffer(const Buffer& buffer);
        static void decodeBuffer(Buffer& buffer, const YAML::Node& node);

        static YAML::Node encodeInterleavedBuffer(const InterleavedBuffer& buffer);
        static void decodeInterleavedBuffer(InterleavedBuffer& buffer, const YAML::Node& node);

        static YAML::Node encodeIndexBuffer(const IndexBuffer& buffer);
        static void decodeIndexBuffer(IndexBuffer& buffer, const YAML::Node& node);

        static YAML::Node encodeExternalBuffer(const ExternalBuffer& buffer);
        static void decodeExternalBuffer(ExternalBuffer& buffer, const YAML::Node& node);

        static YAML::Node encodeSubmesh(const Submesh& submesh);
        static Submesh decodeSubmesh(const YAML::Node& node, const Submesh* oldSubmesh = nullptr);

        static YAML::Node encodeAABB(const AABB& aabb);
        static AABB decodeAABB(const YAML::Node& node);

        static YAML::Node encodeSpriteFrameData(const SpriteFrameData& frameData);
        static SpriteFrameData decodeSpriteFrameData(const YAML::Node& node);

        static YAML::Node encodeTileRectData(const TileRectData& tileRect);
        static TileRectData decodeTileRectData(const YAML::Node& node);

        static YAML::Node encodeTileData(const TileData& tile);
        static TileData decodeTileData(const YAML::Node& node);

        static YAML::Node encodeEntityAux(const Entity entity, const EntityRegistry* registry, const Project* project = nullptr, const SceneProject* sceneProject = nullptr);

        static YAML::Node encodeScriptProperty(const ScriptProperty& prop);
        static ScriptProperty decodeScriptProperty(const YAML::Node& node);

    public:
        static YAML::Node encodeProject(Project* project);
        static void decodeProject(Project* project, const YAML::Node& node);

        static YAML::Node encodeSceneProject(const Project* project, const SceneProject* sceneProject);
        static void decodeSceneProject(SceneProject* sceneProject, const YAML::Node& node, bool loadScene);
        static void decodeSceneProjectEntities(Project* project, SceneProject* sceneProject, const YAML::Node& node);

        static YAML::Node encodeEditorCamera(Camera* camera, float zoom = 0.0f);
        static void decodeEditorCamera(Camera* camera, const YAML::Node& node, float& zoom);

        static YAML::Node encodeScene(Scene* scene);
        static Scene* decodeScene(Scene* scene, const YAML::Node& node);

        static YAML::Node encodeEntitySelection(const std::vector<Entity>& entities, const EntityRegistry* registry, const Project* project = nullptr, const SceneProject* sceneProject = nullptr);
        static std::vector<Entity> decodeEntitySelection(const YAML::Node& entityNode, EntityRegistry* registry, std::vector<Entity>* entities = nullptr, Project* project = nullptr, SceneProject* sceneProject = nullptr, Entity parent = NULL_ENTITY, bool createNewIfExists = true);
        static YAML::Node encodeEntity(const Entity entity, const EntityRegistry* registry, const Project* project = nullptr, const SceneProject* sceneProject = nullptr);
        static std::vector<Entity> decodeEntity(const YAML::Node& entityNode, EntityRegistry* registry, std::vector<Entity>* entities = nullptr, Project* project = nullptr, SceneProject* sceneProject = nullptr, Entity parent = NULL_ENTITY, bool createNewIfExists = true);

        static YAML::Node encodeMaterial(const Material& material);
        static Material decodeMaterial(const YAML::Node& node);

        static YAML::Node encodeComponents(const Entity entity, const EntityRegistry* registry, Signature signature);
        static void decodeComponents(Entity entity, Entity parent, EntityRegistry* registry, const YAML::Node& compNode);

        static YAML::Node encodeTransform(const Transform& transform);
        static Transform decodeTransform(const YAML::Node& node, const Transform* oldTransform = nullptr);

        static YAML::Node encodeMeshComponent(const MeshComponent& mesh, bool encodeBuffers = true);
        static MeshComponent decodeMeshComponent(const YAML::Node& node, const MeshComponent* oldMesh = nullptr);

        static YAML::Node encodeUIComponent(const UIComponent& ui);
        static UIComponent decodeUIComponent(const YAML::Node& node, const UIComponent* oldUI = nullptr);

        static YAML::Node encodeButtonComponent(const ButtonComponent& button);
        static ButtonComponent decodeButtonComponent(const YAML::Node& node, const ButtonComponent* oldButton = nullptr);

        static YAML::Node encodeUILayoutComponent(const UILayoutComponent& layout);
        static UILayoutComponent decodeUILayoutComponent(const YAML::Node& node, const UILayoutComponent* oldLayout = nullptr);

        static YAML::Node encodeUIContainerComponent(const UIContainerComponent& container);
        static UIContainerComponent decodeUIContainerComponent(const YAML::Node& node, const UIContainerComponent* oldContainer = nullptr);

        static YAML::Node encodeTextComponent(const TextComponent& text);
        static TextComponent decodeTextComponent(const YAML::Node& node, const TextComponent* oldText = nullptr);

        static YAML::Node encodeImageComponent(const ImageComponent& image);
        static ImageComponent decodeImageComponent(const YAML::Node& node, const ImageComponent* oldImage = nullptr);

        static YAML::Node encodeSpriteComponent(const SpriteComponent& sprite);
        static SpriteComponent decodeSpriteComponent(const YAML::Node& node, const SpriteComponent* oldSprite = nullptr);

        static YAML::Node encodeTilemapComponent(const TilemapComponent& tilemap);
        static TilemapComponent decodeTilemapComponent(const YAML::Node& node, const TilemapComponent* oldTilemap = nullptr);

        static YAML::Node encodeLightComponent(const LightComponent& light);
        static LightComponent decodeLightComponent(const YAML::Node& node, const LightComponent* oldLight = nullptr);

        static YAML::Node encodeCameraComponent(const CameraComponent& camera);
        static CameraComponent decodeCameraComponent(const YAML::Node& node, const CameraComponent* oldCamera = nullptr);

        static YAML::Node encodeScriptComponent(const ScriptComponent& script);
        static ScriptComponent decodeScriptComponent(const YAML::Node& node, const ScriptComponent* oldScript = nullptr);

        static YAML::Node encodeSkyComponent(const SkyComponent& sky);
        static SkyComponent decodeSkyComponent(const YAML::Node& node, const SkyComponent* oldSky = nullptr);

        static YAML::Node encodeModelComponent(const ModelComponent& model);
        static ModelComponent decodeModelComponent(const YAML::Node& node, const ModelComponent* oldModel = nullptr);

        static YAML::Node encodeBundleComponent(const BundleComponent& bundle);
        static BundleComponent decodeBundleComponent(const YAML::Node& node, const BundleComponent* oldBundle = nullptr);

        static std::string bodyTypeToString(BodyType type);
        static BodyType stringToBodyType(const std::string& str);

        static std::string shape2DTypeToString(Shape2DType type);
        static Shape2DType stringToShape2DType(const std::string& str);

        static std::string shape3DTypeToString(Shape3DType type);
        static Shape3DType stringToShape3DType(const std::string& str);

        static std::string joint2DTypeToString(Joint2DType type);
        static Joint2DType stringToJoint2DType(const std::string& str);

        static std::string joint3DTypeToString(Joint3DType type);
        static Joint3DType stringToJoint3DType(const std::string& str);

        static YAML::Node encodeBody2DComponent(const Body2DComponent& body);
        static Body2DComponent decodeBody2DComponent(const YAML::Node& node, const Body2DComponent* oldBody = nullptr);

        static YAML::Node encodeBody3DComponent(const Body3DComponent& body);
        static Body3DComponent decodeBody3DComponent(const YAML::Node& node, const Body3DComponent* oldBody = nullptr);

        static YAML::Node encodeJoint2DComponent(const Joint2DComponent& joint);
        static Joint2DComponent decodeJoint2DComponent(const YAML::Node& node, const Joint2DComponent* oldJoint = nullptr);

        static YAML::Node encodeJoint3DComponent(const Joint3DComponent& joint);
        static Joint3DComponent decodeJoint3DComponent(const YAML::Node& node, const Joint3DComponent* oldJoint = nullptr);

        static std::string actionStateToString(ActionState state);
        static ActionState stringToActionState(const std::string& str);

        static YAML::Node encodeActionComponent(const ActionComponent& action);
        static ActionComponent decodeActionComponent(const YAML::Node& node, const ActionComponent* oldAction = nullptr);

        static YAML::Node encodeTimedActionComponent(const TimedActionComponent& timed);
        static TimedActionComponent decodeTimedActionComponent(const YAML::Node& node, const TimedActionComponent* oldTimed = nullptr);

        static YAML::Node encodePositionActionComponent(const PositionActionComponent& posAction);
        static PositionActionComponent decodePositionActionComponent(const YAML::Node& node, const PositionActionComponent* oldPosAction = nullptr);

        static YAML::Node encodeRotationActionComponent(const RotationActionComponent& rotAction);
        static RotationActionComponent decodeRotationActionComponent(const YAML::Node& node, const RotationActionComponent* oldRotAction = nullptr);

        static YAML::Node encodeScaleActionComponent(const ScaleActionComponent& scaleAction);
        static ScaleActionComponent decodeScaleActionComponent(const YAML::Node& node, const ScaleActionComponent* oldScaleAction = nullptr);

        static YAML::Node encodeColorActionComponent(const ColorActionComponent& colorAction);
        static ColorActionComponent decodeColorActionComponent(const YAML::Node& node, const ColorActionComponent* oldColorAction = nullptr);

        static YAML::Node encodeAlphaActionComponent(const AlphaActionComponent& alphaAction);
        static AlphaActionComponent decodeAlphaActionComponent(const YAML::Node& node, const AlphaActionComponent* oldAlphaAction = nullptr);

        static YAML::Node encodeSpriteAnimationComponent(const SpriteAnimationComponent& spriteanim);
        static SpriteAnimationComponent decodeSpriteAnimationComponent(const YAML::Node& node, const SpriteAnimationComponent* oldSpriteanim = nullptr);

        static YAML::Node encodeAnimationComponent(const AnimationComponent& animation);
        static AnimationComponent decodeAnimationComponent(const YAML::Node& node, const AnimationComponent* oldAnimation = nullptr);

        static YAML::Node encodeBoneComponent(const BoneComponent& bone);
        static BoneComponent decodeBoneComponent(const YAML::Node& node, const BoneComponent* oldBone = nullptr);

        static YAML::Node encodeKeyframeTracksComponent(const KeyframeTracksComponent& tracks);
        static KeyframeTracksComponent decodeKeyframeTracksComponent(const YAML::Node& node, const KeyframeTracksComponent* oldTracks = nullptr);

        static YAML::Node encodeTranslateTracksComponent(const TranslateTracksComponent& tracks);
        static TranslateTracksComponent decodeTranslateTracksComponent(const YAML::Node& node, const TranslateTracksComponent* oldTracks = nullptr);

        static YAML::Node encodeRotateTracksComponent(const RotateTracksComponent& tracks);
        static RotateTracksComponent decodeRotateTracksComponent(const YAML::Node& node, const RotateTracksComponent* oldTracks = nullptr);

        static YAML::Node encodeScaleTracksComponent(const ScaleTracksComponent& tracks);
        static ScaleTracksComponent decodeScaleTracksComponent(const YAML::Node& node, const ScaleTracksComponent* oldTracks = nullptr);

        static YAML::Node encodeMorphTracksComponent(const MorphTracksComponent& tracks);
        static MorphTracksComponent decodeMorphTracksComponent(const YAML::Node& node, const MorphTracksComponent* oldTracks = nullptr);

        static YAML::Node encodeParticlesComponent(const ParticlesComponent& particles);
        static ParticlesComponent decodeParticlesComponent(const YAML::Node& node, const ParticlesComponent* oldParticles = nullptr);

        static YAML::Node encodeInstancedMeshComponent(const InstancedMeshComponent& instmesh);
        static InstancedMeshComponent decodeInstancedMeshComponent(const YAML::Node& node, const InstancedMeshComponent* oldInstmesh = nullptr);
    };
}