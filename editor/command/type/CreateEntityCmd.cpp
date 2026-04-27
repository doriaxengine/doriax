#include "CreateEntityCmd.h"

#include "editor/Out.h"
#include "util/ShapeParameters.h"
#include "Stream.h"
#include "command/type/DeleteEntityCmd.h"

using namespace doriax;

editor::CreateEntityCmd::CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, bool addToBundle){
    this->project = project;
    this->sceneId = sceneId;
    this->entityName = entityName;
    this->entity = NULL_ENTITY;
    this->childEntity = NULL_ENTITY;
    this->parent = NULL_ENTITY;
    this->type = EntityCreationType::EMPTY;
    this->addToBundle = addToBundle;
    this->wasModified = project->getScene(sceneId)->isModified;
    this->wasMainCamera = false;
    this->updateFlags = 0;
}

editor::CreateEntityCmd::CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, EntityCreationType type, bool addToBundle){
    this->project = project;
    this->sceneId = sceneId;
    this->entityName = entityName;
    this->entity = NULL_ENTITY;
    this->childEntity = NULL_ENTITY;
    this->parent = NULL_ENTITY;
    this->type = type;
    this->addToBundle = addToBundle;
    this->wasModified = project->getScene(sceneId)->isModified;
    this->wasMainCamera = false;
    this->updateFlags = 0;
}

editor::CreateEntityCmd::CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, EntityCreationType type, Entity parent, bool addToBundle){
    this->project = project;
    this->sceneId = sceneId;
    this->entityName = entityName;
    this->entity = NULL_ENTITY;
    this->childEntity = NULL_ENTITY;
    this->parent = parent;
    this->type = type;
    this->addToBundle = addToBundle;
    this->wasModified = project->getScene(sceneId)->isModified;
    this->wasMainCamera = false;
    this->updateFlags = 0;
}

bool editor::CreateEntityCmd::execute(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (!sceneProject){
        return false;
    }

    Scene* scene = sceneProject->scene;

    if (entity == NULL_ENTITY){
        entity = scene->createUserEntity();
    }else{
        if (!scene->recreateEntity(entity)){
            entity = scene->createUserEntity();
        }
    }

    unsigned int nameCount = 2;
    std::string baseName = entityName;
    bool foundName = true;
    while (foundName){
        foundName = false;
        for (auto& entity : sceneProject->entities) {
            std::string usedName = scene->getEntityName(entity);
            if (usedName == entityName){
                entityName = baseName + " " + std::to_string(nameCount);
                nameCount++;
                foundName = true;
            }
        }
    }

    if (type == EntityCreationType::OBJECT){

        scene->addComponent<Transform>(entity, {});

    }else if (type == EntityCreationType::BOX){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createBox(mesh, shapeDefs.boxWidth, shapeDefs.boxHeight, shapeDefs.boxDepth);

    }else if (type == EntityCreationType::PLANE){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createPlane(mesh, shapeDefs.planeWidth, shapeDefs.planeDepth);

    }else if (type == EntityCreationType::SPHERE){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createSphere(mesh, shapeDefs.sphereRadius, shapeDefs.sphereSlices, shapeDefs.sphereStacks);

    }else if (type == EntityCreationType::CYLINDER){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createCylinder(mesh, shapeDefs.cylinderBaseRadius, shapeDefs.cylinderTopRadius, shapeDefs.cylinderHeight, shapeDefs.cylinderSlices, shapeDefs.cylinderStacks);

    }else if (type == EntityCreationType::CAPSULE){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createCapsule(mesh, shapeDefs.capsuleBaseRadius, shapeDefs.capsuleTopRadius, shapeDefs.capsuleHeight, shapeDefs.capsuleSlices, shapeDefs.capsuleStacks);

    }else if (type == EntityCreationType::TORUS){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createTorus(mesh, shapeDefs.torusRadius, shapeDefs.torusRingRadius, shapeDefs.torusSides, shapeDefs.torusRings);

    }else if (type == EntityCreationType::IMAGE){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<ImageComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 100;
        layout.height = 100;

    }else if (type == EntityCreationType::TEXT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<TextComponent>(entity, {});

        TextComponent& textComp = scene->getComponent<TextComponent>(entity);
        textComp.text = "Text";
        textComp.needUpdateText = true;

    }else if (type == EntityCreationType::BUTTON){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<ButtonComponent>(entity, {});
        scene->addComponent<ImageComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 150;
        layout.height = 50;

        ButtonComponent& buttonComp = scene->getComponent<ButtonComponent>(entity);
        scene->getSystem<UISystem>()->createButtonObjects(entity, buttonComp);

        childEntity = buttonComp.label;

        scene->setEntityName(childEntity, "Label");

        TextComponent& textComp = scene->getComponent<TextComponent>(childEntity);
        textComp.text = "Button";
        textComp.needUpdateText = true;

    }else if (type == EntityCreationType::CONTAINER){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<UIContainerComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 100;
        layout.height = 100;

    }else if (type == EntityCreationType::SPRITE){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<SpriteComponent>(entity, {});

        SpriteComponent& sprite = scene->getComponent<SpriteComponent>(entity);
        sprite.width = 100;
        sprite.height = 100;

    }else if (type == EntityCreationType::TILEMAP){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<TilemapComponent>(entity, {});

        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        mesh.numSubmeshes = 1;

        TilemapComponent& tilemap = scene->getComponent<TilemapComponent>(entity);
        tilemap.width = 100;
        tilemap.height = 100;

    }else if (type == EntityCreationType::DIRECTIONAL_LIGHT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<LightComponent>(entity, {});

        LightComponent& light = scene->getComponent<LightComponent>(entity);
        light.type = LightType::DIRECTIONAL;
        light.direction = Vector3(0.0f, -1.0f, 0.0f);
        light.intensity = 4.0f;

    }else if (type == EntityCreationType::POINT_LIGHT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<LightComponent>(entity, {});

        LightComponent& light = scene->getComponent<LightComponent>(entity);
        light.type = LightType::POINT;
        light.range = 10.0f;
        light.intensity = 30.0f;

    }else if (type == EntityCreationType::SPOT_LIGHT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<LightComponent>(entity, {});

        LightComponent& light = scene->getComponent<LightComponent>(entity);
        light.type = LightType::SPOT;
        light.direction = Vector3(0.0f, -1.0f, 0.0f);
        light.range = 10.0f;
        light.intensity = 30.0f;

    }else if (type == EntityCreationType::JOINT2D){

        scene->addComponent<Joint2DComponent>(entity, {});

    }else if (type == EntityCreationType::JOINT3D){

        scene->addComponent<Joint3DComponent>(entity, {});

    }else if (type == EntityCreationType::BODY2D){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<Body2DComponent>(entity, {});

    }else if (type == EntityCreationType::BODY3D){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<Body3DComponent>(entity, {});

    }else if (type == EntityCreationType::SKY){

        scene->addComponent<SkyComponent>(entity, {});

    }else if (type == EntityCreationType::ANIMATION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<AnimationComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::SPRITE_ANIMATION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<SpriteAnimationComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::POSITION_ACTION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<TimedActionComponent>(entity, {});
        scene->addComponent<PositionActionComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::ROTATION_ACTION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<TimedActionComponent>(entity, {});
        scene->addComponent<RotationActionComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::SCALE_ACTION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<TimedActionComponent>(entity, {});
        scene->addComponent<ScaleActionComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::COLOR_ACTION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<TimedActionComponent>(entity, {});
        scene->addComponent<ColorActionComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::ALPHA_ACTION){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<TimedActionComponent>(entity, {});
        scene->addComponent<AlphaActionComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::CAMERA){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<CameraComponent>(entity, {});

        CameraComponent& camera = scene->getComponent<CameraComponent>(entity);
        camera.useTarget = false;

        if (sceneProject->sceneType == SceneType::SCENE_2D || sceneProject->sceneType == SceneType::SCENE_UI){
            camera.type = CameraType::CAMERA_ORTHO;

            float width = static_cast<float>(Engine::getCanvasWidth());
            float height = static_cast<float>(Engine::getCanvasHeight());
            if (width <= 0) width = 800;
            if (height <= 0) height = 600;

            camera.leftClip = 0;
            camera.rightClip = width;
            camera.bottomClip = 0;
            camera.topClip = height;
            camera.nearClip = DEFAULT_ORTHO_NEAR;
            camera.farClip = DEFAULT_ORTHO_FAR;
            camera.transparentSort = false;

            Transform& cameratransform = scene->getComponent<Transform>(entity);
            cameratransform.position = Vector3(0.0f, 0.0f, 1.0f);
        }else{
            camera.type = CameraType::CAMERA_PERSPECTIVE;
            camera.yfov = 0.75f;

            if (Engine::getCanvasWidth() != 0 && Engine::getCanvasHeight() != 0) {
                camera.aspect = (float) Engine::getCanvasWidth() / (float) Engine::getCanvasHeight();
            }else{
                camera.aspect = 1.0;
            }

            camera.nearClip = DEFAULT_PERSPECTIVE_NEAR;
            camera.farClip = DEFAULT_PERSPECTIVE_FAR;
        }

        if (sceneProject->mainCamera == NULL_ENTITY){
            sceneProject->mainCamera = entity;
            wasMainCamera = true;
        }

    }else if (type == EntityCreationType::SOUND){

        scene->addComponent<AudioComponent>(entity, {});

    }else if (type == EntityCreationType::SOUND_3D){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<AudioComponent>(entity, {});
        scene->getComponent<AudioComponent>(entity).enable3D = true;

    }else if (type == EntityCreationType::MODEL){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<ModelComponent>(entity, {});

    }else if (type == EntityCreationType::PARTICLES){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<ParticlesComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::POINTS){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<PointsComponent>(entity, {});

    }else if (type == EntityCreationType::TERRAIN){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<TerrainComponent>(entity, {});

    }

    scene->setEntityName(entity, entityName);

    if (parent != NULL_ENTITY){
        scene->addEntityChild(parent, entity, false);
    }

    // Apply all property setters
    for (const auto& [componentType, properties] : propertySetters) {
        for (const auto& [propertyName, propertySetter] : properties) {
            propertySetter(entity);
        }
    }

    Catalog::updateEntity(scene, entity, updateFlags);

    sceneProject->entities.push_back(entity);
    if (childEntity != NULL_ENTITY){
        Catalog::updateEntity(scene, childEntity, updateFlags);
        sceneProject->entities.push_back(childEntity);
    }

    lastSelected = project->getSelectedEntities(sceneId);
    project->setSelectedEntity(sceneId, entity);

    sceneProject->isModified = true;

    if (addToBundle){
        project->addEntityToBundle(sceneId, entity, parent, false);
        if (childEntity != NULL_ENTITY){
            project->addEntityToBundle(sceneId, childEntity, entity, false);
        }
    }

    ImGui::SetWindowFocus(("###Scene" + std::to_string(sceneId)).c_str());

    editor::Out::info("Created entity '%s' at scene '%s'", entityName.c_str(), sceneProject->name.c_str());

    return true;
}

void editor::CreateEntityCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (sceneProject){
        if (addToBundle){
            if (childEntity != NULL_ENTITY){
                project->removeEntityFromBundle(sceneId, childEntity, false);
            }
            project->removeEntityFromBundle(sceneId, entity, false);
        }

        if (wasMainCamera && sceneProject->mainCamera == entity){
            sceneProject->mainCamera = NULL_ENTITY;
        }

        if (childEntity != NULL_ENTITY){
            DeleteEntityCmd::destroyEntity(sceneProject->scene, childEntity, sceneProject->entities, project, sceneId);
        }
        DeleteEntityCmd::destroyEntity(sceneProject->scene, entity, sceneProject->entities, project, sceneId);

        sceneProject->isModified = wasModified;
    }
}

bool editor::CreateEntityCmd::mergeWith(editor::Command* otherCommand){
    return false;
}

Entity editor::CreateEntityCmd::getEntity(){
    return entity;
}