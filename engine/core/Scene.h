//
// (c) 2026 Eduardo Doria.
//

#ifndef SCENE_H
#define SCENE_H

#include "Entity.h"
#include "SubSystem.h"
#include "EntityRegistry.h"
#include "Export.h"
#include <vector>
#include <unordered_map>


namespace doriax{

    class Camera;

    enum class LightState {
        OFF,
        ON,
        AUTO
    };

    enum class UIEventState {
        NOT_SET,
        ENABLED,
        DISABLED
    };

    class DORIAX_API Scene : public EntityRegistry {
    private:

        Entity camera;
        Entity defaultCamera;

        Vector4 backgroundColor;
        bool shadowsPCF;

        LightState lightState;
        Vector3 globalIllumColor;
        float globalIllumIntensity;

        UIEventState uiEventState;

        std::vector<std::pair<std::string, std::shared_ptr<SubSystem>>> systems;

        void init();
        Entity createDefaultCamera();

    protected:

        void onComponentAdded(Entity entity, ComponentId componentId) override {
            for (auto const& pair : systems) {
                pair.second->onComponentAdded(entity, componentId);
            }
        }

        void onComponentRemoved(Entity entity, ComponentId componentId) override {
            for (auto const& pair : systems) {
                pair.second->onComponentRemoved(entity, componentId);
            }
        }

        void onEntityDestroyed(Entity entity, Signature signature) override {
            for (auto const& pair : systems) {
                for (ComponentId componentId = 0; componentId < signature.size(); ++componentId) {
                    if (signature.test(componentId)) {
                        pair.second->onComponentRemoved(entity, componentId);
                    }
                }
            }
        }

    public:

        Scene();
        Scene(EntityPool defaultPool);
        virtual ~Scene();

        void load();
        void destroy();
        void draw();
        void update(double dt);

        void updateCameraSize();

        void setCamera(Camera* camera);
        void setCamera(Entity camera);
        Entity getCamera() const;

        void setBackgroundColor(Vector4 color);
        void setBackgroundColor(float red, float green, float blue);
        void setBackgroundColor(float red, float green, float blue, float alpha);
        Vector4 getBackgroundColor() const;

        void setShadowsPCF(bool shadowsPCF);
        bool isShadowsPCF() const;

        void setLightState(LightState state);
        LightState getLightState() const;

        void setGlobalIllumination(float intensity, Vector3 color);
        void setGlobalIllumination(float intensity);
        void setGlobalIllumination(Vector3 color);

        float getGlobalIlluminationIntensity() const;
        Vector3 getGlobalIlluminationColor() const;
        Vector3 getGlobalIlluminationColorLinear() const;

        bool canReceiveUIEvents();

        UIEventState getEnableUIEvents() const;
        void setEnableUIEvents(UIEventState enableUIEvents);
        void enableUIEvents();

        bool isEnableUIEvents() const;
        void setEnableUIEvents(bool enableUIEvents);

        // System methods

        template<typename T>
        std::shared_ptr<T> registerSystem(){
            const char* typeName = typeid(T).name();

            auto it = std::find_if(systems.begin(), systems.end(), [&](const auto& pair) { return pair.first == typeName; });

            assert(it == systems.end() && "Registering system more than once");

            auto system = std::make_shared<T>(this);
            systems.push_back(std::make_pair(typeName, system));
            return system;
        }

        template<typename T>
        std::shared_ptr<T> getSystem(){
            const char* typeName = typeid(T).name();

            auto it = std::find_if(systems.begin(), systems.end(), [&](const auto& pair) { return pair.first == typeName; });

            assert(it != systems.end() && "System not found.");

            return std::dynamic_pointer_cast<T>(it->second);
        }
    };

}

#endif //SCENE_H