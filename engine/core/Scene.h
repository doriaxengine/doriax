// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SCENE_H
#define SCENE_H

#include "Entity.h"
#include "SubSystem.h"
#include "EntityRegistry.h"
#include "Export.h"
#include "SceneSettings.h"
#include "render/Render.h"
#include "math/Vector2.h"
#include <string>
#include <vector>


namespace doriax{

    class Camera;

    // LightState and ShadowQuality live in SceneSettings.h (they are scene-setting types).

    enum class UIEventState {
        NOT_SET,
        ENABLED,
        DISABLED
    };

    class DORIAX_API Scene : public EntityRegistry {
    private:

        Entity camera;
        Entity defaultCamera;
        Entity viewCamera;

        // All persistent scene configuration lives here (defaults from SceneSettings). The
        // public getters/setters below delegate to it, keeping the side effects (shader
        // reloads) and color-space conversions that can't live in a plain-data struct.
        // Gravity's live value stays in PhysicsSystem; settings.gravity* is only the default.
        SceneSettings settings;

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
            if (entity == viewCamera) {
                viewCamera = NULL_ENTITY;
            }
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
        void fixedUpdate(double dt);

        void removeSubscriptionsByTag(const std::string& substring);

        void updateCameraSize();

        void setCamera(Camera* camera);
        void setCamera(Entity camera);
        Entity getCamera() const;

        // draws from this camera while getCamera() keeps the game one
        void setViewCamera(Entity camera);
        Entity getRenderCamera() const;

        void setBackgroundColor(Vector4 color);
        void setBackgroundColor(float red, float green, float blue);
        void setBackgroundColor(float red, float green, float blue, float alpha);
        Vector4 getBackgroundColor() const;

        void setShadowQuality(ShadowQuality quality);
        ShadowQuality getShadowQuality() const;

        void setMeshLodEnabled(bool enabled);
        bool isMeshLodEnabled() const;

        void setMeshLodThreshold(float pixels);
        float getMeshLodThreshold() const;

        void setDepthPrepassEnabled(bool enabled);
        bool isDepthPrepassEnabled() const;

        void setSSAOEnabled(bool ssaoEnabled);
        bool isSSAOEnabled() const;

        void setSSAORadius(float radius);
        float getSSAORadius() const;

        void setSSAOIntensity(float intensity);
        float getSSAOIntensity() const;

        void setSSAOBias(float bias);
        float getSSAOBias() const;

        void setSSAODebug(bool debug);
        bool isSSAODebug() const;

        void setSSREnabled(bool ssrEnabled);
        bool isSSREnabled() const;

        void setSSRMaxDistance(float maxDistance);
        float getSSRMaxDistance() const;

        void setSSRThickness(float thickness);
        float getSSRThickness() const;

        void setSSRMaxSteps(int maxSteps);
        int getSSRMaxSteps() const;

        void setSSRIntensity(float intensity);
        float getSSRIntensity() const;

        void setSSRBlur(float blur);
        float getSSRBlur() const;

        void setSSRDebugMode(int mode);
        int getSSRDebugMode() const;

        void setFixedResolutionEnabled(bool fixedResolutionEnabled);
        bool isFixedResolutionEnabled() const;

        void setFixedResolutionWidth(unsigned int width);
        unsigned int getFixedResolutionWidth() const;

        void setFixedResolutionHeight(unsigned int height);
        unsigned int getFixedResolutionHeight() const;

        void setFixedResolutionSize(unsigned int width, unsigned int height);

        void setFixedResolutionFilter(TextureFilter filter);
        TextureFilter getFixedResolutionFilter() const;

        void setLightState(LightState state);
        LightState getLightState() const;

        void setGlobalIllumination(float intensity, Vector3 color);
        void setGlobalIllumination(float intensity);
        void setGlobalIllumination(Vector3 color);

        float getGlobalIlluminationIntensity() const;
        Vector3 getGlobalIlluminationColor() const;
        Vector3 getGlobalIlluminationColorLinear() const;

        void setAmbientLight2D(float intensity, Vector3 color);
        void setAmbientLight2D(float intensity);
        void setAmbientLight2D(Vector3 color);

        float getAmbientLight2DIntensity() const;
        Vector3 getAmbientLight2DColor() const;
        Vector3 getAmbientLight2DColorLinear() const;

        void setShadow2DQuality(ShadowQuality quality);
        ShadowQuality getShadow2DQuality() const;

        // convenience forwarders to PhysicsSystem gravity
        Vector2 getGravity2D() const;
        void setGravity2D(Vector2 gravity);
        void setGravity2D(float x, float y);

        Vector3 getGravity3D() const;
        void setGravity3D(Vector3 gravity);
        void setGravity3D(float x, float y, float z);

        void setDefaultMeshShader(const std::string& path);
        const std::string& getDefaultMeshShader() const;

        void setDefaultUIShader(const std::string& path);
        const std::string& getDefaultUIShader() const;

        void setDefaultSkyShader(const std::string& path);
        const std::string& getDefaultSkyShader() const;

        void setDefaultPointsShader(const std::string& path);
        const std::string& getDefaultPointsShader() const;

        void setDefaultLinesShader(const std::string& path);
        const std::string& getDefaultLinesShader() const;

        void setDefaultWaterShader(const std::string& path);
        const std::string& getDefaultWaterShader() const;

        void setDefaultCustomShader(ShaderType type, const std::string& path);
        const std::string& getDefaultCustomShader(ShaderType type) const;

        // custom block values of a default shader, under those of the components inheriting it
        void setDefaultShaderUniforms(ShaderType type, const ShaderUniformValues& values);
        const ShaderUniformValues& getDefaultShaderUniforms(ShaderType type) const;
        void setDefaultShaderUniform(ShaderType type, const std::string& name, const Vector4& value);
        void setDefaultShaderUniform(ShaderType type, const std::string& name, const Vector3& value);
        void setDefaultShaderUniform(ShaderType type, const std::string& name, const Vector2& value);
        void setDefaultShaderUniform(ShaderType type, const std::string& name, float value);
        Vector4 getDefaultShaderUniform(ShaderType type, const std::string& name) const;
        bool removeDefaultShaderUniform(ShaderType type, const std::string& name);
        // the values a component with this customShader inherits, null when it has its own fork
        const ShaderUniformValues* getInheritedShaderUniforms(ShaderType type, const std::string& customShader) const;

        void setDefaultShaderTextures(ShaderType type, const std::vector<std::pair<std::string, Texture>>& textures);
        const std::vector<std::pair<std::string, Texture>>& getDefaultShaderTextures(ShaderType type) const;
        void setDefaultShaderTexture(ShaderType type, const std::string& name, const Texture& texture);
        Texture getDefaultShaderTexture(ShaderType type, const std::string& name) const;
        bool removeDefaultShaderTexture(ShaderType type, const std::string& name);
        const std::vector<std::pair<std::string, Texture>>* getInheritedShaderTextures(ShaderType type, const std::string& customShader) const;

        // ordered user post-process chain; setting it rebuilds the RenderSystem passes
        // unless only uniform values changed
        const std::vector<PostProcessPass>& getPostProcessPasses() const;
        void setPostProcessPasses(const std::vector<PostProcessPass>& passes);

        // one u_fs_postParams member of a pass; rewrites the value without rebuilding the chain
        void setPostProcessUniform(unsigned int index, const std::string& name, const Vector4& value);
        void setPostProcessUniform(unsigned int index, const std::string& name, const Vector3& value);
        void setPostProcessUniform(unsigned int index, const std::string& name, const Vector2& value);
        void setPostProcessUniform(unsigned int index, const std::string& name, float value);
        Vector4 getPostProcessUniform(unsigned int index, const std::string& name) const;
        bool removePostProcessUniform(unsigned int index, const std::string& name);
        void setPostProcessPassEnabled(unsigned int index, bool enabled);
        bool isPostProcessPassEnabled(unsigned int index) const;

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