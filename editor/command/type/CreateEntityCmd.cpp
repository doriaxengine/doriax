// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CreateEntityCmd.h"

#include "editor/Out.h"
#include "util/ShapeParameters.h"
#include "util/ProjectUtils.h"
#include "Stream.h"
#include "command/type/DeleteEntityCmd.h"

using namespace doriax;

editor::CreateEntityCmd::CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, bool addToBundle){
    this->project = project;
    this->sceneId = sceneId;
    this->entityName = entityName;
    this->entity = NULL_ENTITY;
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
    childEntities.clear();

    auto registerChildEntity = [this](Entity childEntity, Entity childParent){
        childEntities.push_back({childEntity, childParent});
    };

    if (entity == NULL_ENTITY){
        entity = scene->createUserEntity();
    }else{
        if (!scene->recreateEntity(entity)){
            entity = scene->createUserEntity();
        }
    }

    entityName = ProjectUtils::makeUniqueEntityName(scene, sceneProject->entities, entityName);

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

    }else if (type == EntityCreationType::WALL){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createWall(mesh, shapeDefs.wallWidth, shapeDefs.wallHeight);

    }else if (type == EntityCreationType::MIRROR){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<MirrorComponent>(entity, {}); // default normal +Z matches the wall
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::MirrorComponent);

        ShapeParameters shapeDefs;
        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        scene->getSystem<MeshSystem>()->createWall(mesh, shapeDefs.wallWidth, shapeDefs.wallHeight);

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

        registerChildEntity(buttonComp.label, entity);

        scene->setEntityName(buttonComp.label, "Label");

        TextComponent& textComp = scene->getComponent<TextComponent>(buttonComp.label);
        textComp.text = "Button";
        textComp.needUpdateText = true;

    }else if (type == EntityCreationType::SCROLLBAR){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<ScrollbarComponent>(entity, {});
        scene->addComponent<ImageComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 16;
        layout.height = 200;

        ScrollbarComponent& scrollbarComp = scene->getComponent<ScrollbarComponent>(entity);
        scene->getSystem<UISystem>()->createScrollbarObjects(entity, scrollbarComp);

        registerChildEntity(scrollbarComp.bar, entity);

        scene->setEntityName(scrollbarComp.bar, "Bar");

        UIComponent& barUi = scene->getComponent<UIComponent>(scrollbarComp.bar);
        barUi.color = Vector4(0.45f, 0.45f, 0.5f, 1.0f);

    }else if (type == EntityCreationType::PROGRESSBAR){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<ProgressbarComponent>(entity, {});
        scene->addComponent<ImageComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 200;
        layout.height = 16;

        ProgressbarComponent& progressbarComp = scene->getComponent<ProgressbarComponent>(entity);
        scene->getSystem<UISystem>()->createProgressbarObjects(entity, progressbarComp);

        registerChildEntity(progressbarComp.fill, entity);

        scene->setEntityName(progressbarComp.fill, "Fill");

        UIComponent& fillUi = scene->getComponent<UIComponent>(progressbarComp.fill);
        fillUi.color = Vector4(0.3f, 0.7f, 0.4f, 1.0f);

        progressbarComp.value = 0.5f;
        progressbarComp.needUpdateProgressbar = true;

    }else if (type == EntityCreationType::TEXTEDIT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<TextEditComponent>(entity, {});
        scene->addComponent<ImageComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 200;
        layout.height = 32;

        TextEditComponent& textEditComp = scene->getComponent<TextEditComponent>(entity);
        scene->getSystem<UISystem>()->createTextEditObjects(entity, textEditComp);

        scene->setEntityName(textEditComp.text, "Text");
        scene->setEntityName(textEditComp.selection, "Selection");
        scene->setEntityName(textEditComp.cursor, "Cursor");

        registerChildEntity(textEditComp.text, entity);
        registerChildEntity(textEditComp.selection, entity);
        registerChildEntity(textEditComp.cursor, entity);

        TextComponent& textComp = scene->getComponent<TextComponent>(textEditComp.text);
        textComp.text = "";
        textComp.multiline = false;
        textComp.needUpdateText = true;

        textEditComp.placeholder = "Enter text...";
        textEditComp.needUpdateTextEdit = true;

    }else if (type == EntityCreationType::PANEL){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<PanelComponent>(entity, {});
        scene->addComponent<ImageComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 250;
        layout.height = 150;

        ImageComponent& imgComp = scene->getComponent<ImageComponent>(entity);
        imgComp.patchMarginTop = 32;

        PanelComponent& panelComp = scene->getComponent<PanelComponent>(entity);
        scene->getSystem<UISystem>()->createPanelObjects(entity, panelComp);

        scene->setEntityName(panelComp.headerimage, "HeaderImage");
        scene->setEntityName(panelComp.headercontainer, "HeaderContainer");
        scene->setEntityName(panelComp.headertext, "HeaderText");

        registerChildEntity(panelComp.headerimage, entity);
        registerChildEntity(panelComp.headercontainer, panelComp.headerimage);
        registerChildEntity(panelComp.headertext, panelComp.headercontainer);

        TextComponent& titleComp = scene->getComponent<TextComponent>(panelComp.headertext);
        titleComp.text = "Panel";
        titleComp.needUpdateText = true;

        panelComp.needUpdatePanel = true;

    }else if (type == EntityCreationType::POLYGON){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
        scene->addComponent<UIComponent>(entity, {});
        scene->addComponent<PolygonComponent>(entity, {});

        UILayoutComponent& layout = scene->getComponent<UILayoutComponent>(entity);
        layout.width = 100;
        layout.height = 100;

        PolygonComponent& polygonComp = scene->getComponent<PolygonComponent>(entity);
        polygonComp.points.push_back({Vector3(0, 0, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.points.push_back({Vector3(100, 0, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.points.push_back({Vector3(0, 100, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.points.push_back({Vector3(100, 100, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.needUpdatePolygon = true;

    }else if (type == EntityCreationType::CONTAINER){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<UILayoutComponent>(entity, {});
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
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::LightComponent);

        LightComponent& light = scene->getComponent<LightComponent>(entity);
        light.type = LightType::DIRECTIONAL;
        light.direction = Vector3(0.0f, -1.0f, 0.0f);
        light.intensity = 4.0f;

    }else if (type == EntityCreationType::POINT_LIGHT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<LightComponent>(entity, {});
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::LightComponent);

        LightComponent& light = scene->getComponent<LightComponent>(entity);
        light.type = LightType::POINT;
        light.range = 10.0f;
        light.intensity = 30.0f;

    }else if (type == EntityCreationType::SPOT_LIGHT){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<LightComponent>(entity, {});
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::LightComponent);

        LightComponent& light = scene->getComponent<LightComponent>(entity);
        light.type = LightType::SPOT;
        light.direction = Vector3(0.0f, -1.0f, 0.0f);
        light.range = 10.0f;
        light.intensity = 30.0f;

    }else if (type == EntityCreationType::POINT_LIGHT_2D){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<Light2DComponent>(entity, {});
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::Light2DComponent);

        Light2DComponent& light = scene->getComponent<Light2DComponent>(entity);
        light.range = 200.0f;
        light.intensity = 1.0f;

    }else if (type == EntityCreationType::OCCLUDER_2D){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<Occluder2DComponent>(entity, {});
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::Occluder2DComponent);

        // a standalone occluder entity has no sibling mesh for AUTO_QUAD to derive
        // an outline from, so start as an editable polygon with a default square
        Occluder2DComponent& occluder = scene->getComponent<Occluder2DComponent>(entity);
        occluder.shape = Occluder2DShape::POLYGON;
        occluder.points.push_back(Vector2(-50.0f, -50.0f));
        occluder.points.push_back(Vector2(50.0f, -50.0f));
        occluder.points.push_back(Vector2(50.0f, 50.0f));
        occluder.points.push_back(Vector2(-50.0f, 50.0f));

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

    }else if (type == EntityCreationType::FOG){

        scene->addComponent<FogComponent>(entity, {});
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::FogComponent);

    }else if (type == EntityCreationType::REFLECTION_PROBE){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<ReflectionProbeComponent>(entity, {});
        updateFlags |= Catalog::getComponentStructuralUpdateFlags(ComponentType::ReflectionProbeComponent);

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

    }else if (type == EntityCreationType::TRANSLATE_TRACKS){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<KeyframeTracksComponent>(entity, {});
        scene->addComponent<TranslateTracksComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::ROTATE_TRACKS){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<KeyframeTracksComponent>(entity, {});
        scene->addComponent<RotateTracksComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::SCALE_TRACKS){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<KeyframeTracksComponent>(entity, {});
        scene->addComponent<ScaleTracksComponent>(entity, {});
        if (parent != NULL_ENTITY){
            scene->getComponent<ActionComponent>(entity).target = parent;
        }

    }else if (type == EntityCreationType::MORPH_TRACKS){

        scene->addComponent<ActionComponent>(entity, {});
        scene->addComponent<KeyframeTracksComponent>(entity, {});
        scene->addComponent<MorphTracksComponent>(entity, {});
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
            camera.depthTest = false;

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

        scene->addComponent<SoundComponent>(entity, {});

    }else if (type == EntityCreationType::SOUND_3D){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<SoundComponent>(entity, {});

    }else if (type == EntityCreationType::MODEL){

        // the load adds a mesh when the geometry is not on node children
        scene->addComponent<Transform>(entity, {});
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

    }else if (type == EntityCreationType::LINES){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<LinesComponent>(entity, {});

    }else if (type == EntityCreationType::MESH_POLYGON){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<MeshPolygonComponent>(entity, {});

        MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
        mesh.submeshes[0].primitiveType = PrimitiveType::TRIANGLE_STRIP;

        MeshPolygonComponent& polygonComp = scene->getComponent<MeshPolygonComponent>(entity);
        polygonComp.points.push_back({Vector3(0, 0, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.points.push_back({Vector3(100, 0, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.points.push_back({Vector3(0, 100, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.points.push_back({Vector3(100, 100, 0), Vector4(1.0f, 1.0f, 1.0f, 1.0f)});
        polygonComp.needUpdatePolygon = true;

    }else if (type == EntityCreationType::TERRAIN){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<MeshComponent>(entity, {});
        scene->addComponent<TerrainComponent>(entity, {});

    }else if (type == EntityCreationType::WATER){

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<WaterComponent>(entity, {});

    }else if (type == EntityCreationType::WATER_EXCLUSION){

        // a hull needs meshes, which a new entity has none of
        WaterExclusionComponent exclusion;
        exclusion.shape = WaterExclusionShape::BOX;

        scene->addComponent<Transform>(entity, {});
        scene->addComponent<WaterExclusionComponent>(entity, exclusion);

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
    for (const auto& child : childEntities){
        Catalog::updateEntity(scene, child.entity, updateFlags);
        sceneProject->entities.push_back(child.entity);
    }
    ProjectUtils::sortEntitiesByTransformOrder(scene, sceneProject->entities);

    if (!quiet){
        lastSelected = project->getSelectedEntities(sceneId);
        project->setSelectedEntity(sceneId, entity);
    }

    sceneProject->isModified = true;

    if (addToBundle){
        project->addEntityToBundle(sceneId, entity, parent, false);
        for (const auto& child : childEntities){
            project->addEntityToBundle(sceneId, child.entity, child.parent, false);
        }
    }

    if (!quiet){
        if (ImGui::GetCurrentContext()){
            ImGui::SetWindowFocus(("###Scene" + std::to_string(sceneId)).c_str());
        }

        editor::Out::info("Created entity '%s' at scene '%s'", entityName.c_str(), sceneProject->name.c_str());
    }

    return true;
}

void editor::CreateEntityCmd::undo(){
    SceneProject* sceneProject = project->getScene(sceneId);

    if (sceneProject){
        if (addToBundle){
            for (const auto& child : childEntities){
                project->removeEntityFromBundle(sceneId, child.entity, false);
            }
            project->removeEntityFromBundle(sceneId, entity, false);
        }

        if (wasMainCamera && sceneProject->mainCamera == entity){
            sceneProject->mainCamera = NULL_ENTITY;
        }

        std::vector<Entity> entitiesToDestroy;
        entitiesToDestroy.reserve(childEntities.size() + 1);
        for (auto it = childEntities.rbegin(); it != childEntities.rend(); ++it) {
            entitiesToDestroy.push_back(it->entity);
        }
        entitiesToDestroy.push_back(entity);
        DeleteEntityCmd::destroyEntities(sceneProject->scene, entitiesToDestroy,
            sceneProject->entities, project, sceneId);

        sceneProject->isModified = wasModified;
    }
}

bool editor::CreateEntityCmd::mergeWith(editor::Command* otherCommand){
    return false;
}

Entity editor::CreateEntityCmd::getEntity(){
    return entity;
}

void editor::CreateEntityCmd::setQuiet(bool quiet){
    this->quiet = quiet;
}
