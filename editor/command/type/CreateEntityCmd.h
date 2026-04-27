#pragma once

#include "command/Command.h"
#include "Project.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <any>
#include <functional>

namespace doriax::editor{

    enum class EntityCreationType{
        EMPTY,
        OBJECT,
        BOX,
        PLANE,
        SPHERE,
        CYLINDER,
        CAPSULE,
        TORUS,
        IMAGE,
        SPRITE,
        TILEMAP,
        TEXT,
        BUTTON,
        CONTAINER,
        POINT_LIGHT,
        DIRECTIONAL_LIGHT,
        SPOT_LIGHT,
        JOINT2D,
        JOINT3D,
        BODY2D,
        BODY3D,
        SKY,
        CAMERA,
        SOUND,
        SOUND_3D,
        ANIMATION,
        SPRITE_ANIMATION,
        POSITION_ACTION,
        ROTATION_ACTION,
        SCALE_ACTION,
        COLOR_ACTION,
        ALPHA_ACTION,
        MODEL,
        PARTICLES,
        POINTS,
        TERRAIN
    };

    class CreateEntityCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;
        std::string entityName;

        Entity entity;
        Entity childEntity;
        Entity parent;
        EntityCreationType type;
        std::vector<Entity> lastSelected;
        bool addToBundle;
        bool wasModified;
        bool wasMainCamera;

        // Component type -> property name -> property setter function
        std::unordered_map<ComponentType, std::unordered_map<std::string, std::function<void(Entity)>>> propertySetters;
        int updateFlags;

    public:
        CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, bool addToBundle = false);
        CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, EntityCreationType type, bool addToBundle = false);
        CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, EntityCreationType type, Entity parent, bool addToBundle = false);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;

        Entity getEntity();

        template<typename T>
        void addProperty(ComponentType componentType, const std::string& propertyName, T value) {
            Scene* scene = project->getScene(sceneId)->scene;

            auto properties = Catalog::getProperties(componentType, nullptr);
            auto it = properties.find(propertyName);
            if (it != properties.end()) {
                this->updateFlags |= it->second.updateFlags;
            }

            propertySetters[componentType][propertyName] = [value, scene, propertyName, componentType](Entity entity) {
                T* valueRef = Catalog::getPropertyRef<T>(scene, entity, componentType, propertyName);
                if (valueRef != nullptr) {
                    *valueRef = value;
                }
            };
        }
    };

}