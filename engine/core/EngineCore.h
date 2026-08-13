//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_ENGINE_CORE_H
#define DORIAX_ENGINE_CORE_H

#include "Export.h"
#include "Log.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace doriax {

    struct DORIAX_API EngineConfig {
        std::string appName = "Doriax";
        std::string logPrefix = "[Doriax]";
        bool debugMode = true;
        bool asyncAssetLoading = true;
        bool enableValidation = true;
        bool multiThreadedECS = true;
        bool enableHotReload = false;
        size_t workerThreadCount = std::max<size_t>(1u, std::thread::hardware_concurrency() > 0 ? std::thread::hardware_concurrency() : 1u);

        static EngineConfig& get();
    };

    class DORIAX_API EngineCore {
    public:
        using ModuleInitHook = std::function<void()>;

        static void initialize();
        static void shutdown();
        static bool isInitialized();

        static void setDebugMode(bool enabled);
        static bool isDebugModeEnabled();

        static void setWorkerThreadCount(size_t count);
        static size_t getWorkerThreadCount();

        static void registerModule(const std::string& name, ModuleInitHook initHook);
        static void executeModuleInitHooks();

    private:
        static std::atomic<bool> initialized;
        static std::vector<std::pair<std::string, ModuleInitHook>> moduleInitHooks;
        static std::mutex moduleMutex;
    };

}

#endif // DORIAX_ENGINE_CORE_H
