// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "EditorFrame.h"

#include "AppSettings.h"
#include "Backend.h"
#include "Out.h"

#include "imgui.h"

#include <chrono>
#include <cmath>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

using namespace doriax;
using namespace doriax::editor;

namespace {

// Seconds without activity before the loop starts idling
constexpr double IDLE_ENTER_DELAY = 0.5;

double monotonicSeconds(){
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

// On Windows sleep_for wakes on the 15.6 ms system tick, too coarse to pace frames
void sleepSeconds(double seconds){
#if defined(_WIN32)
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>(seconds * 1e7); // relative, in 100 ns units
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)){
        WaitForSingleObject(timer, INFINITE);
        return;
    }
#endif
    std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}

}

void EditorFrame::init(Renderer& renderer, App& app, void (*platformNewFrame)()){
    this->renderer = &renderer;
    this->app = &app;
    this->platformNewFrame = platformNewFrame;
    lastActivityTime = monotonicSeconds();
}

std::string EditorFrame::formatWindowTitle(const std::string& projectName){
    return projectName.empty()
        ? "Empty project - Doriax Engine"
        : projectName + " - Doriax Engine";
}

bool EditorFrame::isIdle() const{
    if (app && app->isBenchmarkMode())
        return false;
    return monotonicSeconds() - lastActivityTime > IDLE_ENTER_DELAY;
}

// Input, an active widget, a scene redraw or queued cross-thread work keeps the
// loop at full rate; its absence lets it idle next frame.
bool EditorFrame::detectActivity(bool& redrawRequested){
    ImGuiIO& io = ImGui::GetIO();

    // The code editor reads keys itself, with no io.WantTextInput or active item
    // to check. Keyboard range only: a resting stick would never let it idle.
    bool typing = io.InputQueueCharacters.Size > 0;
    for (int key = ImGuiKey_Keyboard_BEGIN; !typing && key < ImGuiKey_Keyboard_END; key++){
        typing = ImGui::IsKeyDown(static_cast<ImGuiKey>(key));
    }

    // Consumed apart from the test below, which short-circuits
    const bool appRedraw = app->consumeRedrawRequest();

    // A text field stays active while it has the caret, which blinks at the idle rate
    const bool activeWidget = ImGui::IsAnyItemActive() && !io.WantTextInput;

    const bool activity =
        io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f ||
        io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f ||
        ImGui::IsAnyMouseDown() || typing ||
        activeWidget || app->didRenderScene() ||
        app->hasPendingMainThreadTasks() || redrawRequested || appRedraw;
    redrawRequested = false;

    return activity;
}

bool EditorFrame::run(EditorFrameState& state){
    Project* project = app->getProject();
    app->setWindowFocused(state.focused);
    const bool playSessionActive = project->isPlaySessionActive();

    // Hand the cursor back to the editor while a play session isn't actively
    // running (paused, detached or loading) so a game-held cursor lock can't trap the mouse.
    Backend::setMouseControlSuspended(
        playSessionActive && !project->isGameInputActive());

    const bool idleFrame = !state.forceRedraw && isIdle();

    // Unfocused always drops synchronization, so a blocked present can never
    // stall the event loop that serves clipboard requests.
    const bool vsync = app->isBenchmarkMode() ? false : (playSessionActive
        ? project->isVSyncEnabled()
        : AppSettings::getEditorVSyncEnabled());
    const bool frameSync = state.focused && vsync;
    const bool targetReady =
        state.minimized || renderer->updateTarget(state.width, state.height, frameSync);

    // submits are dropped after a GPU reset, so the window would just stop
    // updating. Checked after updateTarget, which is where a loss often lands.
    if (renderer->isDeviceLost()){
        Out::error("Graphics device was reset, closing the editor without saving");
        return false;
    }
    if (!targetReady){
        return false;
    }

    // Consumed apart from the test below, which short-circuits
    const bool frameRequest = app->consumeFrameRequest();

    // Keeps the caret of a focused text field blinking
    const bool textFocused = ImGui::GetIO().WantTextInput;

    const bool renderRequested = state.forceRedraw || playSessionActive ||
                                 !idleFrame || state.redrawRequested ||
                                 app->hasPendingMainThreadTasks() || frameRequest ||
                                 textFocused;
    const bool frameReady = !state.minimized && renderRequested && renderer->beginFrame();

    renderer->newFrame();
    platformNewFrame();
    ImGui::NewFrame();

    if (frameReady){
        app->engineRender();
    }else{
        // engineRender() is what normally drains main-thread tasks. Keep draining
        // them so AI work, logging and async load callbacks still complete.
        app->processMainThreadTasks();
    }

    app->show();

    const bool activity = detectActivity(state.redrawRequested);
    if (activity){
        lastActivityTime = monotonicSeconds();
    }

    ImGui::Render();
    bool frameSubmitted = false;
    if (frameReady){
        frameSubmitted = renderer->endFrame(ImGui::GetDrawData(), state.width, state.height);
    }

    if (!frameReady || frameSubmitted){
        renderer->renderViewports(renderRequested || activity);
    }

    if (frameSubmitted){
        renderer->present();
    }
    renderer->purgeTextures();

    // A benchmark measures uncapped frames, focused or not
    if (state.minimized || (!state.focused && !app->isBenchmarkMode())){
        // Paced by deadline, so a late wake-up is made up on the next frame
        const double now = monotonicSeconds();
        frameDeadline += state.framePeriod;
        if (std::abs(frameDeadline - now) > state.framePeriod){
            frameDeadline = now;
        }
        if (frameDeadline > now){
            sleepSeconds(frameDeadline - now);
        }
    }

    return true;
}
