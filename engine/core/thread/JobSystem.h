//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_JOB_SYSTEM_H
#define DORIAX_JOB_SYSTEM_H

#include "Export.h"
#include "thread/ThreadPoolManager.h"
#include <future>
#include <functional>
#include <string>
#include <vector>

namespace doriax {

    class DORIAX_API JobSystem {
    public:
        struct JobDescription {
            std::string name;
            std::function<void()> task;
        };

        static JobSystem& get();

        void initialize(size_t workerCount = std::thread::hardware_concurrency());
        void shutdown();

        template<typename F, typename... Args>
        auto schedule(F&& func, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
            return ThreadPoolManager::getInstance().enqueue(std::forward<F>(func), std::forward<Args>(args)...);
        }

        template<typename F>
        void parallelFor(size_t begin, size_t end, F&& func) {
            if (begin >= end) {
                return;
            }

            std::vector<std::future<void>> futures;
            futures.reserve(std::max<size_t>(1u, (end - begin) / 4u));

            for (size_t i = begin; i < end; ++i) {
                futures.push_back(schedule([&func, i]() {
                    func(i);
                }));
            }

            for (auto& future : futures) {
                future.wait();
            }
        }

        size_t workerCount() const;
        size_t queuedJobCount() const;
    };

}

#endif // DORIAX_JOB_SYSTEM_H
