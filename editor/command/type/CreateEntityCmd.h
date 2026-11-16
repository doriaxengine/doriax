// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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
        WALL,
        MIRROR,
        SPHERE,
        CYLINDER,
        CAPSULE,
        TORUS,
        IMAGE,
        SPRITE,
        TILEMAP,
        TEXT,
        BUTTON,
        SCROLLBAR,
        PROGRESSBAR,
        TEXTEDIT,
        PANEL,
        POLYGON,
        CONTAINER,
        POINT_LIGHT,
        DIRECTIONAL_LIGHT,
        SPOT_LIGHT,
        POINT_LIGHT_2D,
        OCCLUDER_2D,
        JOINT2D,
        JOINT3D,
        BODY2D,
        BODY3D,
        SKY,
        FOG,
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
        TRANSLATE_TRACKS,
        ROTATE_TRACKS,
        SCALE_TRACKS,
        MORPH_TRACKS,
        MODEL,
        PARTICLES,
        POINTS,
        LINES,
        MESH_POLYGON,
        TERRAIN,
        REFLECTION_PROBE,
        WATER,
        WATER_EXCLUSION,
        CHARACTER3D
    };

    class CreateEntityCmd: public Command{

    private:
        struct ChildEntityData{
            Entity entity = NULL_ENTITY;
            Entity parent = NULL_ENTITY;
        };

        Project* project;
        uint32_t sceneId;
        std::string entityName;

        Entity entity;
        std::vector<ChildEntityData> childEntities;
        Entity parent;
        EntityCreationType type;
        std::vector<Entity> lastSelected;
        bool addToBundle;
        bool quiet = false;
        bool wasModified;
        bool wasMainCamera;

        // Component type -> property name -> property setter function
        std::unordered_map<ComponentType, std::unordered_map<std::string, std::function<void(Entity)>>> propertySetters;
        uint64_t updateFlags;

    public:
        CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, bool addToBundle = false);
        CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, EntityCreationType type, bool addToBundle = false);
        CreateEntityCmd(Project* project, uint32_t sceneId, std::string entityName, EntityCreationType type, Entity parent, bool addToBundle = false);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;

        Entity getEntity();

        // Skips the selection change, scene focus and log line, for brush-rate creation
        void setQuiet(bool quiet);

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
