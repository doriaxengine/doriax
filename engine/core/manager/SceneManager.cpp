//
// (c) 2026 Eduardo Doria.
//

#include "SceneManager.h"
#include "Engine.h"
#include "Log.h"

#include <algorithm>

using namespace Supernova;

std::vector<SceneManager::SceneEntry> SceneManager::entries;
uint32_t SceneManager::currentId = 0;
std::map<uint32_t, Scene*> SceneManager::scenePtrs;

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> factory) {
    // Overwrite if the id already exists
    for (auto& entry : entries) {
        if (entry.id == id) {
            entry.name = name;
            entry.factory = std::move(factory);
            return;
        }
    }
    entries.push_back({id, name, std::move(factory)});
}

bool SceneManager::loadScene(const std::string& name) {
    for (int i = 0; i < (int)entries.size(); ++i) {
        if (entries[i].name == name) {
            return loadScene(entries[i].id);
        }
    }
    Log::error("SceneManager: scene '%s' not found", name.c_str());
    return false;
}

bool SceneManager::loadScene(uint32_t id) {
    for (int i = 0; i < (int)entries.size(); ++i) {
        if (entries[i].id == id) {
            Engine::removeAllScenes();

            currentId = id;
            entries[i].factory();
            return true;
        }
    }

    Log::error("SceneManager: scene id %u not found", id);
    return false;
}

uint32_t SceneManager::getSceneId(const std::string& name) {
    for (int i = 0; i < (int)entries.size(); ++i) {
        if (entries[i].name == name) return entries[i].id;
    }
    return 0;
}

std::string SceneManager::getSceneName(uint32_t id) {
    for (int i = 0; i < (int)entries.size(); ++i) {
        if (entries[i].id == id) return entries[i].name;
    }
    return "";
}

std::vector<std::string> SceneManager::getSceneNames() {
    std::vector<std::string> names;
    names.reserve(entries.size());
    for (const auto& entry : entries) {
        names.push_back(entry.name);
    }
    return names;
}

int SceneManager::getSceneCount() {
    return (int)entries.size();
}

uint32_t SceneManager::getCurrentSceneId() {
    return currentId;
}

std::string SceneManager::getCurrentSceneName() {
    return getSceneName(currentId);
}

void SceneManager::clearAll() {
    entries.clear();
    currentId = 0;
    scenePtrs.clear();
}

void SceneManager::setScenePtr(uint32_t id, Scene* scene) {
    if (scene) {
        scenePtrs[id] = scene;
    } else {
        scenePtrs.erase(id);
    }
}

Scene* SceneManager::getScenePtr(uint32_t id) {
    auto it = scenePtrs.find(id);
    return (it != scenePtrs.end()) ? it->second : nullptr;
}

void SceneManager::removeScenePtr(uint32_t id) {
    scenePtrs.erase(id);
}
