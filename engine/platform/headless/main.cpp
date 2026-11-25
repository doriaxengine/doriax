// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Engine.h"
#include "System.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

using namespace doriax;

namespace {
    std::atomic<bool> running{true};

    void stop(int){
        running = false;
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
            running = false;
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
