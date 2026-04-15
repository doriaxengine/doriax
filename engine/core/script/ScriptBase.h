//
// (c) 2026 Eduardo Doria.
//

#ifndef SCRIPT_BASE_H
#define SCRIPT_BASE_H

#include "Scene.h"
#include "Entity.h"

namespace doriax {

    class DORIAX_API ScriptBase {
    protected:
        Scene* scene;
        Entity entity;

    public:
        ScriptBase(Scene* scene, Entity entity);
        ~ScriptBase() = default;

        Scene* getScene() const;
        Entity getEntity() const;
    };

}

#endif //SCRIPT_BASE_H