//
// (c) 2026 Eduardo Doria.
//

#include "EntityHandle.h"

using namespace doriax;

EntityHandle::EntityHandle(Scene* scene){
    this->scene = scene;
    this->entity = scene->createEntity();
    this->entityOwned = true;
}

EntityHandle::EntityHandle(Scene* scene, Entity entity){
    this->scene = scene;
    this->entity = entity;
    this->entityOwned = false;
}

EntityHandle::~EntityHandle(){
    if (scene && entityOwned)
        scene->destroyEntity(entity);
}

EntityHandle::EntityHandle(const EntityHandle& rhs){
    scene = rhs.scene;
    entity = rhs.entity;
    entityOwned = rhs.entityOwned;
}

EntityHandle& EntityHandle::operator=(const EntityHandle& rhs){
    scene = rhs.scene;
    entity = rhs.entity;
    entityOwned = rhs.entityOwned;

    return *this;
}

Scene* EntityHandle::getScene() const{
    return scene;
}

Entity EntityHandle::getEntity() const{
    return entity;
}

void EntityHandle::setName(const std::string& name){
    scene->setEntityName(entity, name);
}

std::string EntityHandle::getName() const{
    return scene->getEntityName(entity);
}

bool EntityHandle::isEntityOwned() const{
    return entityOwned;
}

void EntityHandle::setEntityOwned(bool entityOwned){
    this->entityOwned = entityOwned;
}