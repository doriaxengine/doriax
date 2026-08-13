//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_PREFAB_H
#define DORIAX_PREFAB_H

#include "Export.h"
#include "Entity.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace doriax {

    class Scene;

    struct DORIAX_API PrefabInstance {
        std::string name;
        std::vector<Entity> entities;
        Entity root = NULL_ENTITY;
    };

    class DORIAX_API Prefab {
    public:
        using Factory = std::function<void(Scene*, Entity)>;
        using Destroyer = std::function<void(Scene*, Entity)>;

        Prefab();
        Prefab(const std::string& name, Factory factory, Destroyer destroyer = {});

        const std::string& name() const;
        void setName(const std::string& name);

        void setFactory(Factory factory);
        void setDestroyer(Destroyer destroyer);

        Entity instantiate(Scene* scene) const;
        bool destroy(Scene* scene, Entity rootEntity) const;

    private:
        std::string name_;
        Factory factory_;
        Destroyer destroyer_;
        std::unordered_map<Entity, PrefabInstance> instances_;
    };

    class DORIAX_API PrefabRegistry {
    public:
        static PrefabRegistry& get();

        void registerPrefab(const std::string& name, Prefab::Factory factory, Prefab::Destroyer destroyer = {});
        const Prefab* find(const std::string& name) const;
        bool has(const std::string& name) const;
        std::vector<std::string> names() const;
        void clear();

    private:
        std::unordered_map<std::string, Prefab> prefabs_;
    };

}

#endif // DORIAX_PREFAB_H
