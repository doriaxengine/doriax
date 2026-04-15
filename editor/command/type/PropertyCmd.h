#pragma once

#include "command/Command.h"
#include "Scene.h"
#include "math/Vector3.h"
#include "ecs/Entity.h"
#include "component/Transform.h"
#include "Catalog.h"
#include <functional>
#include <type_traits>


namespace doriax::editor{


    template<typename T, typename = void>
    struct is_iterable_container : std::false_type {};

    template<typename T>
    struct is_iterable_container<T, std::void_t<decltype(std::declval<T>().begin()), decltype(std::declval<T>().end()), typename T::value_type>> : std::true_type {};

    template<typename T, typename = void>
    struct is_equality_comparable : std::false_type {};

    template<typename T>
    struct is_equality_comparable<T, std::void_t<decltype(std::declval<const T&>() == std::declval<const T&>())>> : std::true_type {};

    template<typename T, typename = void>
    struct safe_has_equality_operator : std::false_type {};

    template<typename T>
    struct safe_has_equality_operator<T, std::enable_if_t<!is_iterable_container<T>::value>>
        : is_equality_comparable<T> {};

    template<typename T>
    struct safe_has_equality_operator<T, std::enable_if_t<is_iterable_container<T>::value>>
        : is_equality_comparable<typename T::value_type> {};

    template<typename T>
    inline constexpr bool has_equality_operator_v = safe_has_equality_operator<T>::value;


    template<typename T>
    struct PropertyCmdValue{
        T oldValue;
        T newValue;
    };

    template<typename T>
    class PropertyCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;
        ComponentType type;
        std::string propertyName;
        bool wasModified;
        std::function<void()> onValueChanged;

        std::map<Entity,PropertyCmdValue<T>> values;

    public:

        PropertyCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType type, std::string propertyName, T newValue, std::function<void()> onValueChanged = nullptr){
            this->project = project;
            this->sceneId = sceneId;
            this->type = type;
            this->propertyName = propertyName;
            this->onValueChanged = onValueChanged;

            this->values[entity].newValue = newValue;
            this->wasModified = project->getScene(sceneId)->isModified;
        }

        bool execute() override{
            SceneProject* sceneProject = project->getScene(sceneId);
            if (!sceneProject){
                return false;
            }
            for (auto& [entity, value] : values){
                PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, type, propertyName);
                T* valueRef = static_cast<T*>(prop.ref);

                value.oldValue = T(*valueRef);
                *valueRef = value.newValue;

                if constexpr (has_equality_operator_v<T>) {
                    if (value.oldValue == value.newValue){
                        continue;
                    }
                }

                Catalog::updateEntity(sceneProject->scene, entity, prop.updateFlags);

                if (project->isEntityInBundle(sceneId, entity)){
                    project->bundlePropertyChanged(sceneId, entity, type, {propertyName});
                }
            }

            sceneProject->isModified = true;

            if (onValueChanged) {
                onValueChanged();
            }

            return true;
        }

        void undo() override{
            SceneProject* sceneProject = project->getScene(sceneId);
            if (!sceneProject){
                return;
            }
            for (auto const& [entity, value] : values){
                PropertyData prop = Catalog::getProperty(sceneProject->scene, entity, type, propertyName);
                T* valueRef = static_cast<T*>(prop.ref);

                *valueRef = value.oldValue;

                if constexpr (has_equality_operator_v<T>) {
                    if (value.oldValue == value.newValue){
                        continue;
                    }
                }

                Catalog::updateEntity(sceneProject->scene, entity, prop.updateFlags);

                if (project->isEntityInBundle(sceneId, entity)){
                    project->bundlePropertyChanged(sceneId, entity, type, {propertyName});
                }
            }

            sceneProject->isModified = wasModified;

            if (onValueChanged) {
                onValueChanged();
            }
        }

        bool mergeWith(editor::Command* otherCommand) override{
            PropertyCmd* otherCmd = dynamic_cast<PropertyCmd*>(otherCommand);
            if (otherCmd != nullptr){
                if (sceneId == otherCmd->sceneId && propertyName == otherCmd->propertyName){
                    for (auto const& [otherEntity, otherValue] : otherCmd->values){
                        if (values.find(otherEntity) != values.end()) {
                            values[otherEntity].oldValue = otherValue.oldValue;
                        }else{
                            values[otherEntity] = otherValue;
                        }
                    }
                    wasModified = wasModified && otherCmd->wasModified;
                    // Keep the most recent callback
                    if (otherCmd->onValueChanged) {
                        onValueChanged = otherCmd->onValueChanged;
                    }
                    return true;
                }
            }

            return false;
        }

    };

}