// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace doriax::editor {

    class CodeEditor;
    class ImageViewerWindow;
    class Properties;

    class EditorHost {
    public:
        virtual ~EditorHost() = default;

        virtual void enqueueMainThreadTask(std::function<void()> task);
        virtual void processMainThreadTasks();
        virtual bool isMainThread() const;

        virtual void addNewSceneToDock(uint32_t sceneId);
        virtual void clearSceneWindowState(uint32_t sceneId);
        virtual void prepareForProjectSwitch();
        virtual void resetLastActivatedScene();
        virtual bool shouldSyncEngineApi() const;
        // false on the command line, so an export doesn't change the editor's recent projects
        virtual bool recordsRecentProjects() const;
        // false on the command line and in benchmarks, which may run on a project the editor has open
        virtual bool locksProjects() const;
        virtual void updateResourcesPath();
        virtual void updateWindowTitle(const std::string& projectName);
        virtual void stopTransientPreviews();
        virtual void flushSceneMaterialWrites(uint32_t sceneId);
        virtual void saveAllCodeEditors();
        virtual void requestScenePlayFocus(uint32_t sceneId);
        // Runs the change between frames; false if another one is pending
        virtual bool requestProjectChange(std::function<void()> change);

        // Refresh the loading UI and native events at project loading checkpoints.
        virtual void reportLoadingProgress(const std::string& status = {});

        virtual void registerAlert(std::string title, std::string message);
        virtual void registerConfirmAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo = nullptr);
        virtual void registerThreeButtonAlert(std::string title, std::string message, std::function<void()> onYes, std::function<void()> onNo = nullptr, std::function<void()> onCancel = nullptr);
        // callback(false) if the dialog is cancelled or the save fails
        virtual void registerSaveSceneDialog(uint32_t sceneId, std::function<void(bool)> callback = nullptr);
        virtual void registerProjectSaveDialog(std::function<void(bool)> callback = nullptr);

        virtual Properties* getPropertiesWindow() const;
        virtual CodeEditor* getCodeEditor() const;
        virtual ImageViewerWindow* getImageViewerWindow() const;
    };

    void setEditorHost(EditorHost* host);
    EditorHost& getEditorHost();

}
