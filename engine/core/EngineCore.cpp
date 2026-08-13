//
// (c) 2026 Eduardo Doria.
//

#include "EngineCore.h"
#include "thread/ThreadPoolManager.h"
#include <mutex>

namespace doriax {

    namespace {
        EngineConfig configInstance;
    }

    EngineConfig& EngineConfig::get() {
        return configInstance;
    }

    std::atomic<bool> EngineCore::initialized{false};
    std::vector<std::pair<std::string, EngineCore::ModuleInitHook>> EngineCore::moduleInitHooks;
    std::mutex EngineCore::moduleMutex;

    void EngineCore::initialize() {
        if (initialized.load()) {
            return;
        }

        EngineConfig::get().workerThreadCount = std::max<size_t>(1u, EngineConfig::get().workerThreadCount);
        ThreadPoolManager::initialize(EngineConfig::get().workerThreadCount);
        Log::info("Engine initialized with %zu worker threads", EngineConfig::get().workerThreadCount);
        initialized.store(true);
        executeModuleInitHooks();
    }

    void EngineCore::shutdown() {
        if (!initialized.load()) {
            return;
        }

        ThreadPoolManager::shutdown();
        moduleInitHooks.clear();
        initialized.store(false);
        Log::info("Engine shutdown complete");
    }

    bool EngineCore::isInitialized() {
        return initialized.load();
    }

    void EngineCore::setDebugMode(bool enabled) {
        EngineConfig::get().debugMode = enabled;
    }

    bool EngineCore::isDebugModeEnabled() {
        return EngineConfig::get().debugMode;
    }

    void EngineCore::setWorkerThreadCount(size_t count) {
        EngineConfig::get().workerThreadCount = std::max<size_t>(1u, count);
        if (initialized.load()) {
            ThreadPoolManager::initialize(EngineConfig::get().workerThreadCount);
        }
    }

    size_t EngineCore::getWorkerThreadCount() {
        return EngineConfig::get().workerThreadCount;
    }

    void EngineCore::registerModule(const std::string& name, ModuleInitHook initHook) {
        if (!initHook) {
            return;
        }

        std::lock_guard<std::mutex> lock(moduleMutex);
        moduleInitHooks.emplace_back(name, std::move(initHook));
    }

    void EngineCore::executeModuleInitHooks() {
        std::lock_guard<std::mutex> lock(moduleMutex);
        for (auto& [name, hook] : moduleInitHooks) {
            if (hook) {
                try {
                    hook();
                } catch (const std::exception& e) {
                    Log::error("Module '%s' failed during init: %s", name.c_str(), e.what());
                }
            }
        }
    }

}
