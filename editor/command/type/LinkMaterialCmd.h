#pragma once

#include "command/Command.h"
#include "Project.h"
#include "Catalog.h"
#include <string>
#include <functional>

namespace doriax::editor{

    class LinkMaterialCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;
        Entity entity;
        ComponentType componentType;
        std::string propertyName;
        unsigned int submeshIndex;
        bool wasModified;
        std::function<void()> onValueChanged;

        Material newMaterial;
        Material oldMaterial;
        std::string oldLinkedFilePath;

    public:
        LinkMaterialCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType componentType,
                        std::string propertyName, unsigned int submeshIndex,
                        Material newMaterial, std::function<void()> onValueChanged = nullptr);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
