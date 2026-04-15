#pragma once

#include "command/Command.h"
#include "Scene.h"
#include "Catalog.h"

namespace doriax::editor {

    template <typename T>
    struct ScenePropertyCmdValue {
        T oldValue;
        T newValue;
    };

    template <typename T>
    class ScenePropertyCmd : public Command {

    private:
        SceneProject* sceneProject;
        std::string propertyName;
        ScenePropertyCmdValue<T> value;

        bool wasModified;

    public:
        ScenePropertyCmd(SceneProject* sceneProject, const std::string& propertyName, const T& newValue) {
            this->sceneProject = sceneProject;
            this->propertyName = propertyName;
            this->value.newValue = newValue;

            this->wasModified = sceneProject->isModified;
        }

        bool execute() override {
            // Store the old value before changing it
            value.oldValue = Catalog::getSceneProperty<T>(sceneProject->scene, propertyName);
            Catalog::setSceneProperty<T>(sceneProject->scene, propertyName, value.newValue);

            sceneProject->isModified = true;

            return true;
        }

        void undo() override {
            Catalog::setSceneProperty<T>(sceneProject->scene, propertyName, value.oldValue);

            sceneProject->isModified = wasModified;
        }

        bool mergeWith(editor::Command* otherCommand) override {
            ScenePropertyCmd* otherCmd = dynamic_cast<ScenePropertyCmd*>(otherCommand);
            if (otherCmd != nullptr) {
                if (sceneProject->scene == otherCmd->sceneProject->scene && propertyName == otherCmd->propertyName) {
                    // Update oldValue from the earliest command
                    value.oldValue = otherCmd->value.oldValue;
                    value.newValue = otherCmd->value.newValue;

                    wasModified = wasModified && otherCmd->wasModified;

                    return true;
                }
            }
            return false;
        }
    };

}