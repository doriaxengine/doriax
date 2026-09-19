// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneManager.h"
#include "Engine.h"
#include "Log.h"

#include <algorithm>

using namespace doriax;

std::vector<SceneManager::SceneEntry> SceneManager::entries;
uint32_t SceneManager::currentId = 0;
std::optional<uint32_t> SceneManager::pendingId;
std::map<uint32_t, Scene*> SceneManager::scenePtrs;

std::vector<uint32_t> SceneManager::buildSceneStackIds(uint32_t id, const std::vector<uint32_t>& sceneIds) {
    std::vector<uint32_t> result;
    result.reserve(sceneIds.size() + 1);

    auto addId = [&result](uint32_t sceneId) {
        if (std::find(result.begin(), result.end(), sceneId) == result.end()) {
            result.push_back(sceneId);
        }
    };

    addId(id);
    for (uint32_t sceneId : sceneIds) {
        addId(sceneId);
    }

    return result;
}

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::function<void()> addFactory) {
    registerScene(id, name, std::move(loadFactory), std::move(addFactory), std::vector<uint32_t>{id});
}

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::function<void()> addFactory, const std::vector<uint32_t>& sceneIds) {
    std::vector<uint32_t> stackSceneIds = buildSceneStackIds(id, sceneIds);

    // Overwrite if the id already exists
    for (auto& entry : entries) {
        if (entry.id == id) {
            entry.name = name;
            entry.loadFactory = std::move(loadFactory);
            entry.addFactory = std::move(addFactory);
            entry.sceneIds = std::move(stackSceneIds);
            return;
        }
    }
    entries.push_back({id, name, std::move(loadFactory), std::move(addFactory), std::move(stackSceneIds)});
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

SceneManager::SceneEntry* SceneManager::findEntry(uint32_t id) {
    for (auto& entry : entries) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

void SceneManager::runFactory(uint32_t id) {
    SceneEntry* entry = findEntry(id);
    if (!entry) return;

    Engine::removeAllScenes();

    currentId = id;
    entry->loadFactory();
}

bool SceneManager::loadScene(uint32_t id) {
    if (!findEntry(id)) {
        Log::error("SceneManager: scene id %u not found", id);
        return false;
    }

    if (Engine::isFrameRunning()) {
        if (pendingId && *pendingId != id) {
            Log::warn("SceneManager: scene %u replaces pending scene %u", id, *pendingId);
        }
        pendingId = id;
        return true;
    }

    pendingId.reset();
    runFactory(id);
    return true;
}

bool SceneManager::isLoadPending() {
    return pendingId.has_value();
}

void SceneManager::applyPendingLoad() {
    if (!pendingId) return;

    uint32_t id = *pendingId;
    pendingId.reset();
    runFactory(id);
}

bool SceneManager::addChildScene(uint32_t id) {
    for (const auto& entry : entries) {
        if (entry.id != id) {
            continue;
        }

        for (uint32_t sceneId : entry.sceneIds) {
            Scene* scene = getScenePtr(sceneId);
            if (!scene) {
                Log::warn("SceneManager: scene id %u is not loaded. Loading child scene...", sceneId);

                entry.addFactory();
            }
        }

        for (uint32_t sceneId : entry.sceneIds) {
            Engine::addSceneLayer(getScenePtr(sceneId));
        }

        return true;
    }

    Log::error("SceneManager: scene id %u not found", id);
    return false;
}

bool SceneManager::addChildScene(const std::string& name) {
    uint32_t id = getSceneId(name);
    if (id == 0) {
        Log::error("SceneManager: scene '%s' not found", name.c_str());
        return false;
    }

    return addChildScene(id);
}

bool SceneManager::removeChildScene(uint32_t id) {
    for (const auto& entry : entries) {
        if (entry.id != id) {
            continue;
        }

        bool removedScene = false;
        for (auto it = entry.sceneIds.rbegin(); it != entry.sceneIds.rend(); ++it) {
            Scene* scene = getScenePtr(*it);
            if (!scene || scene == Engine::getMainScene() || !Engine::isSceneRunning(scene)) {
                continue;
            }

            Engine::removeScene(scene);
            removedScene = true;
        }

        return removedScene;
    }

    Log::error("SceneManager: scene id %u not found", id);
    return false;
}

bool SceneManager::removeChildScene(const std::string& name) {
    uint32_t id = getSceneId(name);
    if (id == 0) {
        Log::error("SceneManager: scene '%s' not found", name.c_str());
        return false;
    }

    return removeChildScene(id);
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
    pendingId.reset();
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
