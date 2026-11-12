// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "command/CommandHistory.h"
#include "graph/Graph.h"
#include "render/preview/ShaderPreviewRender.h"
#include "shader/graph/ShaderGraphCompiler.h"
#include "widget/NodeCanvas.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace doriax::editor {

    class Project;
    class GraphEditCmd;
    struct NodeType;

    // .shader graphs open for editing, each in a docked window with its own undo history
    class GraphEditorWindow {
    private:
        struct Instance {
            std::filesystem::path filepath;          // project relative
            GraphDocument document;
            std::string text;                        // the document as last applied
            std::string savedText;                   // what the file holds
            std::string loadError;
            std::unique_ptr<CommandHistory> history;
            GraphEditCmd* lastEdit = nullptr;        // the step a drag or typing still extends
            NodeCanvas canvas;
            ShaderGraphResult compiled;
            std::filesystem::file_time_type writeTime;
            bool isOpen = true;
            bool focusRequested = false;
            bool changedOnDisk = false;
            bool showCode = false;
            double previewAt = -1.0;                 // when the scene builds the edits, -1 for none
            std::unique_ptr<ShaderPreviewRender> preview;
            uint32_t previewGeneration = 0;

            // the add-node search
            bool addRequested = false;
            ImVec2 addPosition;
            int addFromNode = 0;
            std::string addFromPin;
            bool addFromOutput = true;
            std::string addFromType;
            std::string search;

            int parameter = 0;                       // the one being edited
            std::string parameterName;               // its name while typed
            int inspected = 0;                       // what the side panel shows
        };

        Project* project;
        std::unordered_map<std::string, std::unique_ptr<Instance>> instances;
        // previews of closed graphs, freed once the engine no longer draws them
        std::vector<std::unique_ptr<ShaderPreviewRender>> retiredPreviews;
        Instance* focused = nullptr;                 // this frame
        Instance* lastFocused = nullptr;             // what save and undo act on, while a menu has the focus
        std::vector<std::string> functionGraphs;     // what a Subgraph's picker lists, found as it opens
        std::filesystem::path openRequested;         // a function a Subgraph node asked to open

        std::string toRelativePath(const std::filesystem::path& path) const;
        bool load(Instance& instance);
        bool save(Instance& instance);
        // with the project's function graphs as edited
        ShaderGraphResult compile(const Instance& instance) const;
        // one undoable step: what changed in the document since it was last applied
        void edit(Instance& instance, const std::string& mergeKey = std::string());
        void applyText(Instance& instance, const std::string& text);
        void documentChanged(Instance& instance, const std::string& text);
        void close(const std::string& key);
        void erase(const std::string& key);

        void drawToolbar(Instance& instance);
        void drawPreview(Instance& instance);
        void drawParameters(Instance& instance);
        void drawInspector(Instance& instance);
        bool drawLayoutInspector(Instance& instance);
        void drawErrors(Instance& instance);
        void drawAddPopup(Instance& instance);
        void drawCanvas(Instance& instance);
        void applyCanvasEdits(Instance& instance, const std::vector<CanvasEdit>& edits);
        CanvasModel buildModel(const Instance& instance) const;
        void addNode(Instance& instance, const NodeType& type);
        std::string copySelection(const Instance& instance, const std::vector<int>& ids) const;
        void paste(Instance& instance, const std::string& text, const ImVec2& position);
        void convertToCode(Instance& instance);

    public:
        explicit GraphEditorWindow(Project* project);
        ~GraphEditorWindow();

        void openFile(const std::filesystem::path& path, bool dockToCentral = true);
        void closeFile(const std::filesystem::path& path);
        void closeAll();
        void show();

        bool isFileOpen(const std::filesystem::path& path) const;
        bool isFileModified(const std::filesystem::path& path) const;
        // an outside edit of an open graph, taken as one undoable step and saved; false when not open
        bool replaceFileText(const std::filesystem::path& path, const std::string& text);
        // changes the graphs with unsaved edits, as an undoable step each; the others change on disk
        void remapEditedGraphs(const std::function<bool(GraphDocument&)>& remap);
        bool isFocused() const;
        bool handleFileRename(const std::filesystem::path& oldPath, const std::filesystem::path& newPath);

        bool saveLastFocused();
        void saveAll();
        bool hasUnsavedChanges() const;
        bool hasLastFocusedUnsavedChanges() const;
        bool canUndoLastFocused() const;
        bool canRedoLastFocused() const;
        void undoLastFocused();
        void redoLastFocused();

        static std::string getWindowId(const std::filesystem::path& relativePath);
    };

}
