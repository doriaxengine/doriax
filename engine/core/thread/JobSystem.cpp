//
// (c) 2026 Eduardo Doria.
//

#include "thread/JobSystem.h"
#include "thread/ThreadPoolManager.h"
#include <thread>

namespace doriax {

    JobSystem& JobSystem::get() {
        static JobSystem instance;
        return instance;
    }

    void JobSystem::initialize(size_t workerCount) {
        ThreadPoolManager::initialize(workerCount);
    }

    void JobSystem::shutdown() {
        ThreadPoolManager::shutdown();
    }

    size_t JobSystem::workerCount() const {
        return std::max<size_t>(1u, std::thread::hardware_concurrency());
    }

    size_t JobSystem::queuedJobCount() const {
        return ThreadPoolManager::getInstance().getQueueSize();
    }

}
