//
// (c) 2026 Eduardo Doria.
//

#ifndef ENTITYHANDLE_H
#define ENTITYHANDLE_H

#include "Scene.h"
#include "Entity.h"

namespace doriax{
    class DORIAX_API EntityHandle{

    protected:
        Entity entity;
        Scene* scene;

        bool entityOwned;

    public:
        EntityHandle(Scene* scene);
        EntityHandle(Scene* scene, Entity entity);
        virtual ~EntityHandle();

        EntityHandle(const EntityHandle& rhs);
        EntityHandle& operator=(const EntityHandle& rhs);

        Scene* getScene() const;
        Entity getEntity() const;

        void setName(const std::string& name);
        std::string getName() const;

        bool isEntityOwned() const;
        void setEntityOwned(bool entityOwned);

        template <typename T>
        void addComponent() {
            scene->addComponent<T>(entity, T{});
        }

        template <typename T>
        void addComponent(T component) {
            scene->addComponent<T>(entity, std::move(component));
        }
    
        template <typename T>
        void removeComponent() {
            scene->removeComponent<T>(entity);
        }
    
        template<typename T>
    	T& getComponent() const {
    		return scene->getComponent<T>(entity);
    	}

    };
}

#endif //ENTITYHANDLE_H