// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CameraTextureLink.h"

#include "Catalog.h"
#include "component/CameraComponent.h"

using namespace doriax;

namespace {
    const std::string ID_PREFIX = "camera|";
}

std::string editor::CameraTextureLink::makeId(Entity cameraEntity){
    return ID_PREFIX + std::to_string(cameraEntity);
}

Entity editor::CameraTextureLink::parse(const std::string& textureId){
    if (textureId.compare(0, ID_PREFIX.size(), ID_PREFIX) != 0)
        return NULL_ENTITY;

    try {
        return static_cast<Entity>(std::stoul(textureId.substr(ID_PREFIX.size())));
    } catch (const std::exception&) {
        return NULL_ENTITY;
    }
}

bool editor::CameraTextureLink::isCameraTexture(const Texture& texture){
    return parse(texture.getId()) != NULL_ENTITY;
}

Texture editor::CameraTextureLink::make(EntityRegistry* registry, Entity cameraEntity){
    Texture texture;

    CameraComponent* camera = registry->findComponent<CameraComponent>(cameraEntity);
    if (camera){
        texture.setFramebuffer(camera->framebuffer);
        texture.setId(makeId(cameraEntity));
    }

    return texture;
}

std::string editor::CameraTextureLink::cameraName(EntityRegistry* registry, const Texture& texture){
    Entity cameraEntity = parse(texture.getId());
    if (cameraEntity == NULL_ENTITY)
        return "";

    if (registry && registry->findComponent<CameraComponent>(cameraEntity))
        return registry->getEntityName(cameraEntity);

    return "< Missing camera >";
}

bool editor::CameraTextureLink::isCameraUsed(EntityRegistry* registry, Entity cameraEntity){
    for (Entity entity : registry->getEntityList()){
        for (ComponentType cpType : Catalog::findComponents(registry, entity)){
            std::map<std::string, PropertyData> properties = Catalog::findEntityProperties(registry, entity, cpType);
            for (auto& [name, prop] : properties){
                if (prop.type != PropertyType::Texture || !prop.ref)
                    continue;

                if (parse(static_cast<Texture*>(prop.ref)->getId()) == cameraEntity)
                    return true;
            }
        }
    }

    return false;
}

bool editor::CameraTextureLink::resolve(EntityRegistry* registry){
    bool changed = false;

    auto resolveTexture = [&](Texture& texture) -> bool {
        Entity cameraEntity = parse(texture.getId());
        if (cameraEntity == NULL_ENTITY)
            return false;

        CameraComponent* camera = registry->findComponent<CameraComponent>(cameraEntity);
        Framebuffer* target = camera ? camera->framebuffer : nullptr;

        if (texture.getFramebuffer() == target)
            return false;

        if (!camera) {
            Log::verbose("CameraTextureLink: camera entity %u no longer exists, clearing framebuffer of texture '%s'",
                         (unsigned int)cameraEntity, texture.getId().c_str());
        }

        std::string textureId = texture.getId();
        texture.setFramebuffer(target);
        texture.setId(textureId);
        return true;
    };

    for (Entity entity : registry->getEntityList()){
        for (ComponentType cpType : Catalog::findComponents(registry, entity)){
            std::map<std::string, PropertyData> properties = Catalog::findEntityProperties(registry, entity, cpType);
            for (auto& [name, prop] : properties){
                if (prop.type != PropertyType::Texture || !prop.ref)
                    continue;

                Texture* texture = static_cast<Texture*>(prop.ref);
                if (resolveTexture(*texture)){
                    Catalog::updateEntity(registry, entity, prop.updateFlags);
                    changed = true;
                }
            }
        }
    }

    Scene* scene = dynamic_cast<Scene*>(registry);
    if (!scene)
        return changed;

    for (ShaderType type : {ShaderType::MESH, ShaderType::UI, ShaderType::SKYBOX,
                            ShaderType::POINTS, ShaderType::LINES, ShaderType::WATER}) {
        auto textures = scene->getDefaultShaderTextures(type);
        bool texturesChanged = false;
        for (auto& entry : textures) {
            if (resolveTexture(entry.second))
                texturesChanged = true;
        }
        if (texturesChanged) {
            scene->setDefaultShaderTextures(type, textures);
            changed = true;
        }
    }

    auto passes = scene->getPostProcessPasses();
    bool passesChanged = false;
    for (auto& pass : passes) {
        for (auto& entry : pass.customTextures) {
            if (resolveTexture(entry.second))
                passesChanged = true;
        }
    }
    if (passesChanged) {
        scene->setPostProcessPasses(passes);
        changed = true;
    }

    return changed;
}
