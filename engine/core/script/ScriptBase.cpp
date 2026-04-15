//
// (c) 2026 Eduardo Doria.
//

#include "ScriptBase.h"

using namespace doriax;

ScriptBase::ScriptBase(Scene* scene, Entity entity)
    : scene(scene), entity(entity) {
}

Scene* ScriptBase::getScene() const {
    return scene;
}

Entity ScriptBase::getEntity() const {
    return entity;
}