//
// (c) 2026 Eduardo Doria.
//

#ifndef SUBSYSTEM_H
#define SUBSYSTEM_H

#include "Export.h"
#include "Entity.h"
#include "Signature.h"
#include "ComponentManager.h"
#include <set>

namespace doriax{

    class Scene;

    class DORIAX_API SubSystem {

        protected:
        Signature signature;
        Scene* scene;
        bool paused = false;

    public:

        SubSystem(Scene* scene) {
            this->scene = scene;
        }

        virtual void setPaused(bool paused) {
            this->paused = paused;
        }

        virtual bool isPaused() const {
            return paused;
        }

        virtual void load() = 0;
        virtual void draw() = 0;
        virtual void destroy() = 0;
        virtual void update(double dt) = 0;

        virtual void onComponentAdded(Entity entity, ComponentId componentId) { (void)entity; (void)componentId; }
        virtual void onComponentRemoved(Entity entity, ComponentId componentId) { (void)entity; (void)componentId; }

    };

}


#endif //SUBSYSTEM_H