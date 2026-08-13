//
// (c) 2026 Eduardo Doria.
//

#include "scene/Prefab.h"
#include "Scene.h"
#include "Log.h"

namespace doriax {

    Prefab::Prefab() = default;

    Prefab::Prefab(const std::string& name, Factory factory, Destroyer destroyer)
        : name_(name), factory_(std::move(factory)), destroyer_(std::move(destroyer)) {
    }

    const std::string& Prefab::name() const {
        return name_;
    }

    void Prefab::setName(const std::string& name) {
        name_ = name;
    }

    void Prefab::setFactory(Factory factory) {
        factory_ = std::move(factory);
    }

    void Prefab::setDestroyer(Destroyer destroyer) {
        destroyer_ = std::move(destroyer);
    }

    Entity Prefab::instantiate(Scene* scene) const {
        if (!scene) {
            Log::error("Prefab::instantiate: scene is null");
            return NULL_ENTITY;
        }

        if (!factory_) {
            Log::error("Prefab '%s' has no factory", name_.c_str());
            return NULL_ENTITY;
        }

        Entity root = scene->createEntity();
        factory_(scene, root);
        return root;
    }

    bool Prefab::destroy(Scene* scene, Entity rootEntity) const {
        if (!scene || !scene->isEntityCreated(rootEntity)) {
            return false;
        }

        if (destroyer_) {
            destroyer_(scene, rootEntity);
            return true;
        }

        scene->destroyEntity(rootEntity);
        return true;
    }

    PrefabRegistry& PrefabRegistry::get() {
        static PrefabRegistry registry;
        return registry;
    }

    void PrefabRegistry::registerPrefab(const std::string& name, Prefab::Factory factory, Prefab::Destroyer destroyer) {
        prefabs_[name] = Prefab(name, std::move(factory), std::move(destroyer));
    }

    const Prefab* PrefabRegistry::find(const std::string& name) const {
        auto it = prefabs_.find(name);
        return it == prefabs_.end() ? nullptr : &it->second;
    }

    bool PrefabRegistry::has(const std::string& name) const {
        return prefabs_.find(name) != prefabs_.end();
    }

    std::vector<std::string> PrefabRegistry::names() const {
        std::vector<std::string> result;
        result.reserve(prefabs_.size());
        for (const auto& [name, prefab] : prefabs_) {
            (void)prefab;
            result.push_back(name);
        }
        return result;
    }

    void PrefabRegistry::clear() {
        prefabs_.clear();
    }

}
