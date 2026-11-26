// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Engine.h"
#include "System.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <thread>

using namespace doriax;

namespace {
    volatile std::sig_atomic_t running = 1;

    void stop(int){
        running = 0;
    }

    // A server has no window: the canvas has the size the project asked for
    class SystemHeadless : public System {
    public:
        int getScreenWidth() override {
            return DEFAULT_WINDOW_WIDTH;
        }

        int getScreenHeight() override {
            return DEFAULT_WINDOW_HEIGHT;
        }

        void quit() override {
            running = 0;
        }

        std::string getAssetPath() override {
            return "assets";
        }

        std::string getUserDataPath() override {
            return ".";
        }

        std::string getLuaPath() override {
            return "lua";
        }
    };
}

int main(int argc, char* argv[]){
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    Engine::systemInit(argc, argv, new SystemHeadless());
    Engine::systemViewLoaded();
    Engine::systemViewChanged();

    // no display paces the frames
    const std::chrono::microseconds frameTime(16667);
    auto next = std::chrono::steady_clock::now();
    while (running){
        Engine::systemUpdate();

        next = std::max(next + frameTime, std::chrono::steady_clock::now());
        std::this_thread::sleep_until(next);
    }

    Engine::systemViewDestroyed();
    Engine::systemShutdown();

    return 0;
}
