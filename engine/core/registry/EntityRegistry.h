#ifndef ENTITYREGISTRY_H
#define ENTITYREGISTRY_H

#include "Entity.h"
#include "EntityManager.h"
#include "ComponentManager.h"
#include "Signature.h"
#include <memory>

#include "component/MeshComponent.h"
#include "component/ModelComponent.h"
#include "component/BoneComponent.h"
#include "component/SkyComponent.h"
#include "component/FogComponent.h"
#include "component/ImageComponent.h"
#include "component/UILayoutComponent.h"
#include "component/UIContainerComponent.h"
#include "component/SpriteComponent.h"
#include "component/SpriteAnimationComponent.h"
#include "component/Transform.h"
#include "component/LightComponent.h"
#include "component/ActionComponent.h"
#include "component/TimedActionComponent.h"
#include "component/PositionActionComponent.h"
#include "component/RotationActionComponent.h"
#include "component/ScaleActionComponent.h"
#include "component/ColorActionComponent.h"
#include "component/AlphaActionComponent.h"
#include "component/ParticlesComponent.h"
#include "component/PointsComponent.h"
#include "component/LinesComponent.h"
#include "component/TextComponent.h"
#include "component/UIComponent.h"
#include "component/ButtonComponent.h"
#include "component/PanelComponent.h"
#include "component/ScrollbarComponent.h"
#include "component/TextEditComponent.h"
#include "component/MeshPolygonComponent.h"
#include "component/PolygonComponent.h"
#include "component/AnimationComponent.h"
#include "component/KeyframeTracksComponent.h"
#include "component/MorphTracksComponent.h"
#include "component/RotateTracksComponent.h"
#include "component/TranslateTracksComponent.h"
#include "component/ScaleTracksComponent.h"
#include "component/ScriptComponent.h"
#include "component/TerrainComponent.h"
#include "component/AudioComponent.h"
#include "component/TilemapComponent.h"
#include "component/Body2DComponent.h"
#include "component/Joint2DComponent.h"
#include "component/Body3DComponent.h"
#include "component/Joint3DComponent.h"
#include "component/InstancedMeshComponent.h"
#include "component/BundleComponent.h"

namespace doriax {

    enum class EntityPool { User, System };

    class DORIAX_API EntityRegistry {
    private:
        EntityManager entityManager;
        ComponentManager componentManager;
        EntityPool defaultPool = EntityPool::User;

        void sortComponentsByTransform(Signature entitySignature);
        void moveChildAux(Entity entity, bool increase, bool stopIfFound);
        void changeTransformChildren(Entity entity);

    protected:
        virtual void onComponentAdded(Entity entity, ComponentId componentId) { (void)entity; (void)componentId; }
        virtual void onComponentRemoved(Entity entity, ComponentId componentId) { (void)entity; (void)componentId; }
        virtual void onEntityDestroyed(Entity entity, Signature signature) { (void)entity; (void)signature; }

    public:
        EntityRegistry();
        EntityRegistry(EntityPool defaultPool);
        virtual ~EntityRegistry();

        //Entity methods

        Entity createEntity();
        Entity createUserEntity();   // 101..max
        Entity createSystemEntity(); // 1..100
        bool recreateEntity(Entity entity); // for internal editor use only

        bool isEntityCreated(Entity entity) const;

        void destroyEntity(Entity entity);

        void addEntityChild(Entity parent, Entity child, bool changeTransform);

        void moveChildToIndex(Entity entity, size_t index); // for internal/editor use

        void moveChildToTop(Entity entity);
        void moveChildUp(Entity entity);
        void moveChildDown(Entity entity);
        void moveChildToBottom(Entity entity);

        Signature getSignature(Entity entity) const;

        void setEntityName(Entity entity, const std::string& name);
        std::string getEntityName(Entity entity) const;

        Entity findOldestParent(Entity entity);
        bool isParentOf(Entity parent, Entity child);
        size_t findBranchLastIndex(Entity entity);

        Entity getLastEntity() const;
        std::vector<Entity> getEntityList() const;

        void setDefaultEntityPool(EntityPool pool);
        EntityPool getDefaultEntityPool() const;

        void clear();

        // Component methods

        template<typename T>
        void registerComponent(){
            componentManager.registerComponent<T>();
        }

        template<typename T>
        void addComponent(Entity entity){
            addComponent<T>(entity, T{});
        }

        template<typename T>
        void addComponent(Entity entity, T component){
            componentManager.addComponent<T>(entity, component);
            auto signature = entityManager.getSignature(entity);
            signature.set(componentManager.getComponentId<T>(), true);
            entityManager.setSignature(entity, signature);

            onComponentAdded(entity, componentManager.getComponentId<T>());
        }

        template<typename T>
        void removeComponent(Entity entity){
            onComponentRemoved(entity, componentManager.getComponentId<T>());

            if (componentManager.getComponentId<T>() == getComponentId<Transform>()) {
                changeTransformChildren(entity);
            }

            componentManager.removeComponent<T>(entity);
            auto signature = entityManager.getSignature(entity);
            signature.set(componentManager.getComponentId<T>(), false);
            entityManager.setSignature(entity, signature); 
        }

        template<typename T>
        T* findComponent(Entity entity) {
            return componentManager.findComponent<T>(entity);
        }

        template<typename T>
        T& getComponent(Entity entity) const {
            return componentManager.getComponent<T>(entity);
        }

        template<typename T>
        T* findComponentFromIndex(size_t index) {
            return componentManager.findComponentFromIndex<T>(index);
        }

        template<typename T>
        T& getComponentFromIndex(size_t index) {
            return componentManager.getComponentFromIndex<T>(index);
        }

        template<typename T>
        ComponentId getComponentId() const{
            return componentManager.getComponentId<T>();
        }

        template<typename T>
        std::shared_ptr<ComponentArray<T>> getComponentArray() const{
            return componentManager.getComponentArray<T>();
        }
    };

} // namespace doriax

#endif // ENTITYREGISTRY_H