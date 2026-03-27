#include "EntityRegistry.h"

using namespace Supernova;

EntityRegistry::EntityRegistry() {
    registerComponent<MeshComponent>();
    registerComponent<ModelComponent>();
    registerComponent<BoneComponent>();
    registerComponent<SkyComponent>();
    registerComponent<FogComponent>();
    registerComponent<UIContainerComponent>();
    registerComponent<UILayoutComponent>();
    registerComponent<SpriteComponent>();
    registerComponent<SpriteAnimationComponent>();
    registerComponent<Transform>();
    registerComponent<CameraComponent>();
    registerComponent<LightComponent>();
    registerComponent<ActionComponent>();
    registerComponent<TimedActionComponent>();
    registerComponent<PositionActionComponent>();
    registerComponent<RotationActionComponent>();
    registerComponent<ScaleActionComponent>();
    registerComponent<ColorActionComponent>();
    registerComponent<AlphaActionComponent>();
    registerComponent<ParticlesComponent>();
    registerComponent<PointsComponent>();
    registerComponent<LinesComponent>();
    registerComponent<TextComponent>();
    registerComponent<UIComponent>();
    registerComponent<ImageComponent>();
    registerComponent<ButtonComponent>();
    registerComponent<PanelComponent>();
    registerComponent<ScrollbarComponent>();
    registerComponent<TextEditComponent>();
    registerComponent<MeshPolygonComponent>();
    registerComponent<PolygonComponent>();
    registerComponent<AnimationComponent>();
    registerComponent<KeyframeTracksComponent>();
    registerComponent<MorphTracksComponent>();
    registerComponent<RotateTracksComponent>();
    registerComponent<TranslateTracksComponent>();
    registerComponent<ScaleTracksComponent>();
    registerComponent<ScriptComponent>();
    registerComponent<TerrainComponent>();
    registerComponent<AudioComponent>();
    registerComponent<TilemapComponent>();
    registerComponent<Body2DComponent>();
    registerComponent<Joint2DComponent>();
    registerComponent<Body3DComponent>();
    registerComponent<Joint3DComponent>();
    registerComponent<InstancedMeshComponent>();
    registerComponent<BundleComponent>();
}

EntityRegistry::EntityRegistry(EntityPool defaultPool) : EntityRegistry() {
    this->defaultPool = defaultPool;
}

EntityRegistry::~EntityRegistry(){
    std::vector<Entity> entityList = entityManager.getEntityList();
    while(entityList.size() > 0){
        destroyEntity(entityList.front());
        // some entities can destroy other entities (ex: models)
        entityList = entityManager.getEntityList();
    }
}

void EntityRegistry::sortComponentsByTransform(Signature entitySignature){
    // Mesh component
    if (entitySignature.test(getComponentId<MeshComponent>())){
        auto meshes = componentManager.getComponentArray<MeshComponent>();
        meshes->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // InstancedMesh component
    if (entitySignature.test(getComponentId<InstancedMeshComponent>())){
        auto instmeshes = componentManager.getComponentArray<InstancedMeshComponent>();
        instmeshes->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // Model component
    if (entitySignature.test(getComponentId<ModelComponent>())){
        auto models = componentManager.getComponentArray<ModelComponent>();
        models->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // Bone component
    if (entitySignature.test(getComponentId<BoneComponent>())){
        auto bones = componentManager.getComponentArray<BoneComponent>();
        bones->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // Polygon component
    if (entitySignature.test(getComponentId<PolygonComponent>())){
        auto polygons = componentManager.getComponentArray<PolygonComponent>();
        polygons->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // UI layout component
    if (entitySignature.test(getComponentId<UILayoutComponent>())){
        auto layout = componentManager.getComponentArray<UILayoutComponent>();
        layout->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // UI component
    if (entitySignature.test(getComponentId<UIComponent>())){
        auto ui = componentManager.getComponentArray<UIComponent>();
        ui->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // Points component
    if (entitySignature.test(getComponentId<PointsComponent>())){
        auto points = componentManager.getComponentArray<PointsComponent>();
        points->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // Lines component
    if (entitySignature.test(getComponentId<LinesComponent>())){
        auto lines = componentManager.getComponentArray<LinesComponent>();
        lines->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }

    // Audio component
    if (entitySignature.test(getComponentId<AudioComponent>())){
        auto audios = componentManager.getComponentArray<AudioComponent>();
        audios->sortByComponent<Transform>(componentManager.getComponentArray<Transform>());
    }
}

void EntityRegistry::moveChildAux(Entity entity, bool increase, bool stopIfFound){
    Signature signature = entityManager.getSignature(entity);

    if (signature.test(getComponentId<Transform>())){
        auto transforms = componentManager.getComponentArray<Transform>();

        size_t entityIndex = transforms->getIndex(entity);
        //auto teste = transforms->getComponent(entity);
        Entity entityParent = transforms->getComponent(entity).parent;

        size_t nextIndex = entityIndex;
        if (increase){
            for (int i = (entityIndex+1); i < transforms->size(); i++){
                Transform& next = transforms->getComponentFromIndex(i);
                if (next.parent == entityParent){
                    nextIndex = i;
                    if (stopIfFound)
                        break;
                }
            }
            nextIndex = findBranchLastIndex(transforms->getEntity(nextIndex)) + 1;
        }else{
            for (int i = (entityIndex-1); i >= 0; i--){
                Transform& next = transforms->getComponentFromIndex(i);
                if (next.parent == entityParent){
                    nextIndex = i;
                    if (stopIfFound)
                        break;
                }
            }
        }

        moveChildToIndex(entity, nextIndex);
    }
}

void EntityRegistry::changeTransformChildren(Entity entity){
    // Get the parent of the entity being removed before it's deleted
    Transform* removedTransform = findComponent<Transform>(entity);
    Entity newParent = NULL_ENTITY;
    if (removedTransform) {
        newParent = removedTransform->parent;
    }

    // Find all entities with Transform components that have this entity as parent
    auto transformArray = getComponentArray<Transform>();
    if (transformArray) {
        for (size_t i = 0; i < transformArray->size(); i++) {
            Transform* childTransform = transformArray->findComponentFromIndex(i);
            if (childTransform && childTransform->parent == entity) {
                // Set the parent to the grandparent (parent of removed entity)
                childTransform->parent = newParent;
                // Mark for update since hierarchy changed
                childTransform->needUpdate = true;
                childTransform->needUpdateChildVisibility = true;
            }
        }
    }
}

Entity EntityRegistry::createEntity() {
    return (defaultPool == EntityPool::System)
        ? entityManager.createSystemEntity()
        : entityManager.createUserEntity();
}

Entity EntityRegistry::createUserEntity() {
    return entityManager.createUserEntity();
}

Entity EntityRegistry::createSystemEntity() {
    return entityManager.createSystemEntity();
}

bool EntityRegistry::isEntityCreated(Entity entity) const {
    return entityManager.isCreated(entity);
}

bool EntityRegistry::recreateEntity(Entity entity) {
    return entityManager.recreateEntity(entity);
}

void EntityRegistry::destroyEntity(Entity entity) {
    onEntityDestroyed(entity, entityManager.getSignature(entity));

    componentManager.entityDestroyed(entity);
    entityManager.destroy(entity);
}

Signature EntityRegistry::getSignature(Entity entity) const {
    return entityManager.getSignature(entity);
}

void EntityRegistry::setEntityName(Entity entity, const std::string& name) {
    entityManager.setName(entity, name);
}

std::string EntityRegistry::getEntityName(Entity entity) const {
    return entityManager.getName(entity);
}

Entity EntityRegistry::findOldestParent(Entity entity){
    auto transforms = componentManager.getComponentArray<Transform>();

    size_t index = transforms->getIndex(entity);
    Entity lastParent = transforms->getComponentFromIndex(index).parent;

    for (int i = index - 1; i >= 0; i--){
        entity = transforms->getEntity(i);
        Transform& transform = transforms->getComponentFromIndex(i);
        if (entity == lastParent){
            lastParent = transform.parent;

            if (lastParent == NULL_ENTITY){
                return entity;
            }
        }
    }

    return entity;
}

bool EntityRegistry::isParentOf(Entity parent, Entity child){
    auto transforms = componentManager.getComponentArray<Transform>();

    if (!getSignature(parent).test(getComponentId<Transform>()) || !getSignature(child).test(getComponentId<Transform>())) {
        return false;
    }

    size_t index = transforms->getIndex(child);
    Entity lastParent = transforms->getComponentFromIndex(index).parent;

    if (lastParent == parent){
        return true;
    }
    if (lastParent == NULL_ENTITY){
        return false;
    }

    for (int i = index - 1; i >= 0; i--){
        Entity entity = transforms->getEntity(i);
        Transform& transform = transforms->getComponentFromIndex(i);
        if (entity == lastParent){
            lastParent = transform.parent;

            if (lastParent == parent){
                return true;
            }
            if (lastParent == NULL_ENTITY){
                return false;
            }
        }
    }

    return false;
}

size_t EntityRegistry::findBranchLastIndex(Entity entity){
    auto transforms = componentManager.getComponentArray<Transform>();

    // will throw error if entity has not Transform
    size_t index = transforms->getIndex(entity);

    size_t currentIndex = index + 1;
    std::vector<Entity> entityList;
    entityList.push_back(entity);

    bool found = true;
    while (found){
        if (currentIndex < transforms.get()->size()){
            Transform& transform = componentManager.getComponentFromIndex<Transform>(currentIndex);
            //if not in list
            if (std::find(entityList.begin(), entityList.end(),transform.parent)==entityList.end()) {
                found = false;
            }else{
                entityList.push_back(transforms->getEntity(currentIndex));
                currentIndex++;
            }
        } else {
            found = false;
        }
    }

    currentIndex--;

    return currentIndex;
}

Entity EntityRegistry::getLastEntity() const{
    return entityManager.getLastUserEntity();
}

std::vector<Entity> EntityRegistry::getEntityList() const{
    return entityManager.getEntityList();
}

void EntityRegistry::setDefaultEntityPool(EntityPool pool) { 
    defaultPool = pool;
}

EntityPool EntityRegistry::getDefaultEntityPool() const {
    return defaultPool;
}

void EntityRegistry::clear() {
    std::vector<Entity> entities = getEntityList();
    for (Entity entity : entities) {
        destroyEntity(entity);
    }
    // Reset user counter to just before the user range
    entityManager.setLastUserEntity(EntityManager::lastSystemEntity());
}

void EntityRegistry::addEntityChild(Entity parent, Entity child, bool changeTransform){
    //----------DEBUG
    //printf("Add child - BEFORE\n");
    //auto transforms = componentManager.getComponentArray<Transform>();
    //for (int i = 0; i < transforms->size(); i++){
    //	auto transform = transforms->getComponentFromIndex(i);
    //	printf("Transform %i - Entity: %i - Parent: %i: %s\n", i, transforms->getEntity(i), transform.parent, getEntityName(transforms->getEntity(i)).c_str());
    //}
    //----------DEBUG

    Signature childSignature = entityManager.getSignature(child);
    if (childSignature.test(getComponentId<Transform>())){
        Transform& transformChild = componentManager.getComponent<Transform>(child);

        if (transformChild.parent == parent){
            return;
        }

        transformChild.needUpdate = true;

        if (parent == NULL_ENTITY){
            size_t newIndex = findBranchLastIndex(findOldestParent(child)) + 1;
            moveChildToIndex(child, newIndex);

            Transform& transformChild = componentManager.getComponent<Transform>(child);

            transformChild.parent = NULL_ENTITY;
            if (changeTransform){
                // set local position to be the same of world position
                transformChild.modelMatrix.decompose(transformChild.position, transformChild.scale, transformChild.rotation);
            }
        }else{
            Signature parentSignature = entityManager.getSignature(parent);
            if (parentSignature.test(getComponentId<Transform>())){
                size_t newIndex = findBranchLastIndex(parent) + 1;
                moveChildToIndex(child, newIndex);

                Transform& transformParent = componentManager.getComponent<Transform>(parent);
                Transform& transformChild = componentManager.getComponent<Transform>(child);

                if (transformChild.parent != parent) {
                    transformChild.parent = parent;

                    if (changeTransform){
                        Matrix4 localMatrix = transformParent.modelMatrix.inverse() * transformChild.modelMatrix;
                        localMatrix.decompose(transformChild.position, transformChild.scale, transformChild.rotation);
                    }

                }
            }
        }
    }

    //----------DEBUG
    //printf("Add child - AFTER\n");
    //for (int i = 0; i < transforms->size(); i++){
    //	auto transform = transforms->getComponentFromIndex(i);
    //	printf("Transform %i - Entity: %i - Parent: %i: %s\n", i, transforms->getEntity(i), transform.parent, getEntityName(transforms->getEntity(i)).c_str());
    //}
    //printf("\n");
    //----------DEBUG
}

void EntityRegistry::moveChildToIndex(Entity entity, size_t index){
    Signature signature = entityManager.getSignature(entity);

    if (signature.test(getComponentId<Transform>())){
        auto transforms = componentManager.getComponentArray<Transform>();
        size_t entityIndex = transforms->getIndex(entity);

        if (index != entityIndex){
            size_t lastChildIndex = findBranchLastIndex(entity);
            size_t length = lastChildIndex - entityIndex + 1;

            if (index > entityIndex){
                if (index > 0){ // Prevent underflow
                    index--;
                }
            }

            if (length == 1){
                transforms->moveEntityToIndex(entity, index);
            }else{
                if (index > entityIndex){
                    // Only adjust if moving past the entire range
                    if (index >= entityIndex + length) {
                        index = index - length + 1;
                    } else {
                        // Target is within our own range - nothing to do
                        return;
                    }
                }
                transforms->moveEntityRangeToIndex(entity, transforms->getEntity(lastChildIndex), index);
            }

            sortComponentsByTransform(entityManager.getSignature(entity));
        }
    }
}

void EntityRegistry::moveChildToTop(Entity entity){
    moveChildAux(entity, true, false);
}

void EntityRegistry::moveChildUp(Entity entity){
    moveChildAux(entity, true, true);
}

void EntityRegistry::moveChildDown(Entity entity){
    moveChildAux(entity, false, true);
}

void EntityRegistry::moveChildToBottom(Entity entity){
    moveChildAux(entity, false, false);
}