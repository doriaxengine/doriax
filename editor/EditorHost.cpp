// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "EditorHost.h"

#include "Out.h"

using namespace doriax::editor;

void EditorHost::enqueueMainThreadTask(std::function<void()> task) {
    if (task) {
        task();
    }
}

void EditorHost::processMainThreadTasks() {
}

bool EditorHost::isMainThread() const {
    return true;
}

void EditorHost::addNewSceneToDock(uint32_t) {
}

void EditorHost::clearSceneWindowState(uint32_t) {
}

void EditorHost::prepareForProjectSwitch() {
}

void EditorHost::resetLastActivatedScene() {
}

bool EditorHost::shouldSyncEngineApi() const {
    return false;
}

bool EditorHost::recordsRecentProjects() const {
    return false;
}

bool EditorHost::locksProjects() const {
    return false;
}

void EditorHost::updateResourcesPath() {
}

void EditorHost::updateWindowTitle(const std::string&) {
}

void EditorHost::stopTransientPreviews() {
}

void EditorHost::flushSceneMaterialWrites(uint32_t) {
}

void EditorHost::saveAllCodeEditors() {
}

void EditorHost::requestScenePlayFocus(uint32_t) {
}

bool EditorHost::requestProjectChange(std::function<void()> change) {
    if (change) {
        change();
    }
    return true;
}

void EditorHost::reportLoadingProgress(const std::string&) {
}

void EditorHost::registerAlert(std::string title, std::string message) {
    if (!title.empty()) {
        Out::warning("%s: %s", title.c_str(), message.c_str());
    } else {
        Out::warning(message);
    }
}

void EditorHost::registerConfirmAlert(std::string, std::string, std::function<void()> onYes, std::function<void()>) {
    if (onYes) {
        onYes();
    }
}

void EditorHost::registerThreeButtonAlert(std::string, std::string, std::function<void()> onYes, std::function<void()>, std::function<void()>) {
    if (onYes) {
        onYes();
    }
}

void EditorHost::registerSaveSceneDialog(uint32_t, std::function<void(bool)> callback) {
    if (callback) {
        callback(true);
    }
}

void EditorHost::registerProjectSaveDialog(std::function<void(bool)> callback) {
    if (callback) {
        callback(true);
    }
}

Properties* EditorHost::getPropertiesWindow() const {
    return nullptr;
}

CodeEditor* EditorHost::getCodeEditor() const {
    return nullptr;
}

ImageViewerWindow* EditorHost::getImageViewerWindow() const {
    return nullptr;
}

static EditorHost s_defaultHost;
static EditorHost* s_activeHost = &s_defaultHost;

void doriax::editor::setEditorHost(EditorHost* host) {
    s_activeHost = host ? host : &s_defaultHost;
}

EditorHost& doriax::editor::getEditorHost() {
    return *s_activeHost;
}
