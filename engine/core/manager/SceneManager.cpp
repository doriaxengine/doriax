// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SceneManager.h"
#include "Engine.h"
#include "Scene.h"
#include "Log.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/AudioSystem.h"
#include "subsystem/MeshSystem.h"
#include "pool/TexturePool.h"
#include "pool/TextureDataPool.h"
#include "pool/SoundPool.h"
#include "thread/ResourceProgress.h"

#include <algorithm>
#if defined(DORIAX_LINUX) && defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace doriax;

std::vector<SceneManager::SceneEntry> SceneManager::entries;
uint32_t SceneManager::currentId = 0;
std::optional<uint32_t> SceneManager::pendingId;
std::map<uint32_t, Scene*> SceneManager::scenePtrs;

uint32_t SceneManager::loadingSceneId = 0;
float SceneManager::loadingDelay = 0.0f;
float SceneManager::loadingTimeout = 5.0f;
SceneManager::LoadingState SceneManager::loadingState = SceneManager::LoadingState::None;
bool SceneManager::loadingSceneShown = false;
bool SceneManager::loadingSceneReady = false;
double SceneManager::loadingStateTime = 0.0;
size_t SceneManager::loadingCount = 0;
double SceneManager::loadingProgressTime = 0.0;
float SceneManager::loadingProgress = 0.0f;
int SceneManager::loadingHolds = 0;

std::vector<SceneManager::Preload> SceneManager::preloads;

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

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory) {
    registerScene(id, name, std::move(loadFactory), nullptr, std::vector<uint32_t>{id});
}

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, const std::vector<uint32_t>& sceneIds) {
    registerScene(id, name, std::move(loadFactory), nullptr, sceneIds);
}

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::initializer_list<uint32_t> sceneIds) {
    registerScene(id, name, std::move(loadFactory), nullptr, std::vector<uint32_t>(sceneIds));
}

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::function<void()> addFactory) {
    registerScene(id, name, std::move(loadFactory), std::move(addFactory), std::vector<uint32_t>{id});
}

void SceneManager::registerScene(uint32_t id, const std::string& name, std::function<void()> loadFactory, std::function<void()> addFactory, const std::vector<uint32_t>& sceneIds, std::function<SceneResources()> resources) {
    std::vector<uint32_t> stackSceneIds = buildSceneStackIds(id, sceneIds);

    // Overwrite if the id already exists
    for (auto& entry : entries) {
        if (entry.id == id) {
            entry.name = name;
            entry.loadFactory = std::move(loadFactory);
            entry.addFactory = std::move(addFactory);
            entry.sceneIds = std::move(stackSceneIds);
            entry.resources = std::move(resources);
            return;
        }
    }
    entries.push_back({id, name, std::move(loadFactory), std::move(addFactory), std::move(stackSceneIds), std::move(resources)});
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

    // Copied for the same reason as in addChildScene: the factory runs scripts
    std::function<void()> loadFactory = entry->loadFactory;

    std::vector<Scene*> loadingScenes;
    if (loadingSceneShown) {
        loadingScenes = getRunningScenes(loadingSceneId);
    }

    Engine::removeAllScenes();

    // still running, so the factory does not delete them
    for (Scene* scene : loadingScenes) {
        Engine::addSceneLayer(scene);
    }

    currentId = id;
    loadFactory();

    if (loadingSceneShown) {
        raiseLoadingScene();
    }
}

bool SceneManager::loadScene(uint32_t id) {
    if (!findEntry(id)) {
        Log::error("SceneManager: scene id %u not found", id);
        return false;
    }

    // input callbacks run between frames, but still inside the scenes being replaced, and the
    // start scene waits for the first frame when a loading scene can cover it
    const bool coverStart = loadingSceneId != 0 && canShowLoadingScene(id);
    if (Engine::isFrameRunning() || Engine::getMainScene() || coverStart) {
        if (pendingId && *pendingId != id) {
            Log::warn("SceneManager: scene %u replaces pending scene %u", id, *pendingId);
        }
        pendingId = id;
        return true;
    }

    startLoading(id);
    return true;
}

bool SceneManager::isLoadPending() {
    return pendingId.has_value();
}

void SceneManager::applyPendingLoad() {
    if (!pendingId) return;

    uint32_t id = *pendingId;

    if (loadingState != LoadingState::Covering && canShowLoadingScene(id) && addChildScene(loadingSceneId)) {
        raiseLoadingScene();
        loadingSceneShown = true;
        loadingSceneReady = false;
        loadingState = LoadingState::Covering;
        loadingStateTime = Engine::getSystemTime();
        resetLoadProgress();
        return;
    }

    if (loadingState == LoadingState::Covering) {
        // the switch freezes on the last presented frame, which must show the loading scene
        if (!loadingSceneReady || Engine::getSystemTime() - loadingStateTime < loadingDelay) {
            return;
        }
    }

    startLoading(id);
}

std::vector<Scene*> SceneManager::getRunningScenes(uint32_t id) {
    std::vector<Scene*> scenes;
    SceneEntry* entry = findEntry(id);
    if (!entry) return scenes;

    for (uint32_t sceneId : entry->sceneIds) {
        Scene* scene = getScenePtr(sceneId);
        if (scene && Engine::isSceneRunning(scene)) {
            scenes.push_back(scene);
        }
    }

    return scenes;
}

bool SceneManager::canShowLoadingScene(uint32_t id) {
    SceneEntry* loadingEntry = findEntry(loadingSceneId);
    SceneEntry* entry = findEntry(id);
    if (!loadingEntry || !entry) return false;

    // not for a stack that includes it
    for (uint32_t sceneId : loadingEntry->sceneIds) {
        if (std::find(entry->sceneIds.begin(), entry->sceneIds.end(), sceneId) != entry->sceneIds.end()) {
            return false;
        }
    }

    return true;
}

void SceneManager::raiseLoadingScene() {
    for (Scene* scene : getRunningScenes(loadingSceneId)) {
        if (scene == Engine::getMainScene()) continue;
        Engine::removeScene(scene);
        Engine::addSceneLayer(scene);
    }
}

void SceneManager::startLoading(uint32_t id) {
    pendingId.reset();
    // before the factory, so the scripts it builds see isLoading()
    loadingState = LoadingState::Loading;
    runFactory(id);
    resetLoadProgress();
}

void SceneManager::resetLoadProgress() {
    loadingCount = 0;
    loadingProgressTime = Engine::getSystemTime();
    loadingProgress = 0.0f;
}

bool SceneManager::isLoadProgressing(size_t loaded, double now) {
    if (loaded != loadingCount || ResourceProgress::hasActiveBuilds()) {
        loadingCount = loaded;
        loadingProgressTime = now;
    }

    return now - loadingProgressTime < loadingTimeout;
}

void SceneManager::getLoadCount(uint32_t id, size_t& loaded, size_t& total) {
    loaded = 0;
    total = 0;

    for (Scene* scene : getRunningScenes(id)) {
        size_t sceneLoaded = 0;
        size_t sceneTotal = 0;
        scene->getSystem<RenderSystem>()->getLoadCount(sceneLoaded, sceneTotal);
        loaded += sceneLoaded;
        total += sceneTotal;

        scene->getSystem<AudioSystem>()->getLoadCount(sceneLoaded, sceneTotal);
        loaded += sceneLoaded;
        total += sceneTotal;
    }
}

bool SceneManager::setLoadingScene(const std::string& name) {
    if (name.empty()) {
        loadingSceneId = 0;
        return true;
    }

    uint32_t id = getSceneId(name);
    if (id == 0) {
        Log::error("SceneManager: scene '%s' not found", name.c_str());
        return false;
    }

    return setLoadingScene(id);
}

bool SceneManager::setLoadingScene(uint32_t id) {
    if (id != 0 && !findEntry(id)) {
        Log::error("SceneManager: scene id %u not found", id);
        return false;
    }

    loadingSceneId = id;
    return true;
}

uint32_t SceneManager::getLoadingSceneId() {
    return loadingSceneId;
}

void SceneManager::setLoadingDelay(float seconds) {
    loadingDelay = std::max(0.0f, seconds);
}

float SceneManager::getLoadingDelay() {
    return loadingDelay;
}

void SceneManager::setLoadingTimeout(float seconds) {
    loadingTimeout = std::max(0.0f, seconds);
}

float SceneManager::getLoadingTimeout() {
    return loadingTimeout;
}

bool SceneManager::isLoading() {
    return pendingId.has_value() || loadingState != LoadingState::None;
}

float SceneManager::getLoadingProgress() {
    if (pendingId || loadingState == LoadingState::Covering) return 0.0f;
    if (loadingState == LoadingState::Loading) return loadingProgress;

    return 1.0f;
}

bool SceneManager::holdLoading() {
    if (!isLoading()) return false;

    loadingHolds++;
    return true;
}

void SceneManager::releaseLoading() {
    if (loadingHolds > 0) loadingHolds--;
}

bool SceneManager::isCoveredByLoading(Scene* scene) {
    if (!loadingSceneShown) return false;

    std::vector<Scene*> loadingScenes = getRunningScenes(loadingSceneId);
    return std::find(loadingScenes.begin(), loadingScenes.end(), scene) == loadingScenes.end();
}

void SceneManager::setCurrentScene(uint32_t id) {
    pendingId.reset();
    currentId = id;
    loadingState = LoadingState::Loading;
    resetLoadProgress();
}

SceneManager::Preload* SceneManager::findPreload(uint32_t id) {
    for (auto& preload : preloads) {
        if (preload.id == id) return &preload;
    }
    return nullptr;
}

bool SceneManager::preloadScene(const std::string& name) {
    uint32_t id = getSceneId(name);
    if (id == 0) {
        Log::error("SceneManager: scene '%s' not found", name.c_str());
        return false;
    }

    return preloadScene(id);
}

bool SceneManager::preloadScene(uint32_t id) {
    if (!findEntry(id)) {
        Log::error("SceneManager: scene id %u not found", id);
        return false;
    }

    // started at the end of the frame, not inside the calling script
    Preload* preload = findPreload(id);
    if (!preload) {
        preloads.push_back({id});
    } else if (preload->cancelled) {
        preload->cancelled = false;
        preload->started = false;
    }
    return true;
}

float SceneManager::getPreloadProgress(const std::string& name) {
    return getPreloadProgress(getSceneId(name));
}

float SceneManager::getPreloadProgress(uint32_t id) {
    Preload* preload = findPreload(id);
    if (!preload || !preload->started || preload->cancelled) return 0.0f;
    if (preload->total == 0) return 1.0f;

    const SceneResources& pending = preload->pending;
    size_t remaining = pending.textures.size() + pending.sounds.size() + pending.models.size();
    return (float)(preload->total - remaining) / (float)preload->total;
}

void SceneManager::cancelPreload(const std::string& name) {
    cancelPreload(getSceneId(name));
}

void SceneManager::cancelPreload(uint32_t id) {
    Preload* preload = findPreload(id);
    if (!preload) return;

    // loads in flight still end, then everything no scene uses is released
    preload->cancelled = true;
    preload->held.clear();
    if (!Engine::isAsyncLoading()) {
        preload->pending.textures.clear();
        preload->pending.sounds.clear();
    }
}

void SceneManager::startPreload(uint32_t id) {
    SceneEntry* entry = findEntry(id);
    Preload* preload = findPreload(id);
    if (!entry || !preload) return;

    // copied, a resource function can register scenes and add preloads
    std::vector<uint32_t> sceneIds = entry->sceneIds;
    SceneResources pending = preload->pending;

    auto addFile = [](std::vector<std::string>& files, const std::string& file) {
        if (std::find(files.begin(), files.end(), file) == files.end()) {
            files.push_back(file);
        }
    };

    for (uint32_t sceneId : sceneIds) {
        // a running scene has its files already
        Scene* scene = getScenePtr(sceneId);
        if (scene && Engine::isSceneRunning(scene)) continue;

        SceneEntry* sceneEntry = findEntry(sceneId);
        if (!sceneEntry || !sceneEntry->resources) continue;

        SceneResources resources = sceneEntry->resources();
        for (const Texture& texture : resources.textures) {
            auto sameRender = [&texture](const Texture& other) { return other.getRenderId() == texture.getRenderId(); };
            if (std::none_of(pending.textures.begin(), pending.textures.end(), sameRender)) {
                pending.textures.push_back(texture);
            }
        }
        for (const std::string& sound : resources.sounds) {
            addFile(pending.sounds, sound);
        }
        for (const std::string& model : resources.models) {
            addFile(pending.models, model);
        }
    }

    preload = findPreload(id);
    if (!preload) return;

    preload->started = true;
    preload->total = pending.textures.size() + pending.sounds.size() + pending.models.size();
    preload->pending = std::move(pending);
}

void SceneManager::releasePreload(Preload& preload) {
    preload.held.clear();
    for (const auto& release : preload.releases) {
        release();
    }
    preload.releases.clear();
}

void SceneManager::updatePreloads() {
    // a synchronous load stalls this thread, so only one goes per frame
    const bool oneFile = !Engine::isAsyncLoading();

    // by index, a resource function can add a preload
    for (size_t i = 0; i < preloads.size();) {
        if (!preloads[i].started && !preloads[i].cancelled) {
            startPreload(preloads[i].id);
        }

        Preload& preload = preloads[i];
        SceneResources& pending = preload.pending;

        auto keep = [&preload](std::shared_ptr<void> data, std::function<void()> release) {
            if (!data) return;
            if (!preload.cancelled) preload.held.push_back(std::move(data));
            preload.releases.push_back(std::move(release));
        };

        // true once a load ended, keeping what it loaded
        auto finished = [&keep](const auto& result, std::function<void()> release) {
            if (result.state == ResourceLoadState::Loading) return false;
            keep(result.data, std::move(release));
            return true;
        };

        for (auto it = pending.textures.begin(); it != pending.textures.end();) {
            const std::string id = it->getId();
            const std::string renderId = it->getRenderId();
            auto release = [id, renderId]() {
                TexturePool::remove(renderId);
                TextureDataPool::remove(id);
            };

            // on the GPU for a running scene, held so the switch does not free it
            if (std::shared_ptr<TextureRender> render = TexturePool::get(renderId)) {
                keep(render, release);
                it = pending.textures.erase(it);
                continue;
            }

            it = finished(it->load(), release) ? pending.textures.erase(it) : it + 1;
            if (oneFile) return;
        }

        for (auto it = pending.sounds.begin(); it != pending.sounds.end();) {
            const std::string name = *it;
            auto release = [name]() { SoundPool::remove(name); };
            it = finished(SoundPool::loadFromFile(name, name), release) ? pending.sounds.erase(it) : it + 1;
            if (oneFile) return;
        }

        for (auto it = pending.models.begin(); it != pending.models.end();) {
            std::shared_ptr<void> data;
            if (!MeshSystem::preloadModel(*it, data)) {
                ++it;
                continue;
            }

            const std::string file = *it;
            keep(data, [file]() { MeshSystem::releasePreloadedModel(file); });
            it = pending.models.erase(it);
        }

        const bool inFlight = !pending.textures.empty() || !pending.sounds.empty() || !pending.models.empty();
        if (preload.cancelled && !inFlight) {
            releasePreload(preload);
            preloads.erase(preloads.begin() + i);
        } else {
            i++;
        }
    }
}

void SceneManager::updateLoading() {
    updatePreloads();

    if (loadingState == LoadingState::None) return;

    double now = Engine::getSystemTime();

    if (loadingState == LoadingState::Covering) {
        if (!loadingSceneReady) {
            size_t loaded;
            size_t total;
            getLoadCount(loadingSceneId, loaded, total);
            loadingSceneReady = loaded >= total || !isLoadProgressing(loaded, now);
        }
        return;
    }

    if (loadingState == LoadingState::Loading) {
        size_t loaded;
        size_t total;
        getLoadCount(currentId, loaded, total);

        // never moves back when the stack creates more to load
        if (total > 0) {
            loadingProgress = std::max(loadingProgress, (float)loaded / (float)total);
        }

        if (loaded < total) {
            if (isLoadProgressing(loaded, now)) return;
            Log::warn("SceneManager: %zu of %zu resources of '%s' did not load",
                total - loaded, total, getSceneName(currentId).c_str());
        }

        loadingState = LoadingState::Loaded;
        // a handler can hold the load or start another one
        Engine::onSceneLoaded.call();
    }

    if (loadingHolds > 0 || pendingId) return;

    if (loadingSceneShown) {
        removeChildScene(loadingSceneId);
        loadingSceneShown = false;
    }
    loadingState = LoadingState::None;

    // the new scenes hold their own files now
    cancelPreload(currentId);
#if defined(DORIAX_LINUX) && defined(__GLIBC__)
    // give back the free pages decoders and replaced scenes left in malloc arenas
    malloc_trim(0);
#endif
}

bool SceneManager::addChildScene(uint32_t id) {
    SceneEntry* entry = findEntry(id);
    if (!entry) {
        Log::error("SceneManager: scene id %u not found", id);
        return false;
    }

    // The factory runs scripts, which can register scenes and reallocate entries
    std::vector<uint32_t> sceneIds = entry->sceneIds;
    std::function<void()> addFactory = entry->addFactory;

    // Even when the scenes exist: a registered pointer does not mean the stack is ready to
    // show. The editor keeps the pointers of a stack it switched away from after tearing its
    // scripts down, and only the factory puts them back. Both factories are idempotent.
    if (addFactory) {
        addFactory();
    }

    for (uint32_t sceneId : sceneIds) {
        if (!getScenePtr(sceneId)) {
            Log::error("SceneManager: scene id %u is not loaded", sceneId);
            return false;
        }
    }

    // In stack order, so the root stays below the layers it owns
    for (uint32_t sceneId : sceneIds) {
        Engine::addSceneLayer(getScenePtr(sceneId));
    }

    return true;
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

    loadingSceneId = 0;
    loadingDelay = 0.0f;
    loadingTimeout = 5.0f;
    loadingState = LoadingState::None;
    loadingSceneShown = false;
    loadingHolds = 0;

    // a model still parsing would keep its build open
    for (Preload& preload : preloads) {
        for (const std::string& model : preload.pending.models) {
            MeshSystem::cancelPreloadModel(model);
        }
        releasePreload(preload);
    }
    preloads.clear();
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

uint32_t SceneManager::getScenePtrId(Scene* scene) {
    for (const auto& entry : scenePtrs) {
        if (entry.second == scene) return entry.first;
    }
    return 0;
}

void SceneManager::removeScenePtr(uint32_t id) {
    scenePtrs.erase(id);
}
