// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "Project.h"
#include "SceneWindow.h"
#include "imgui.h"
#include <list>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace doriax::editor{

    struct TreeNode {
        std::string icon;
        std::string name;
        std::filesystem::path bundleFilepath;
        std::filesystem::path nestedBundleFilepath;
        bool isScene = false;
        bool isChildScene = false;
        bool isBundle = false;
        bool isBundleRoot = false;
        bool hasBundleParent = false;
        bool isParentBundle = false;
        bool isMainCamera = false;
        bool isBone = false;
        bool isLocked = false;
        bool isPlayCreated = false;         // Made by the running scene, gone again on Stop
        bool canEditModelHierarchy = false;
        std::string hierarchyReason;        // Why a model node or part stays put, shown in its tooltip
        bool separator = false;
        bool hasTransform = false;
        bool matchesSearch = false;         // Node matches search term
        bool hasMatchingDescendant = false; // Has a descendant that matches search
        uint32_t id = 0;
        size_t order = 0;
        uint32_t parent = NULL_ENTITY;
        uint32_t childSceneId = 0;          // Scene ID for child scene nodes
        uint32_t ownerSceneId = 0;          // Parent/owner scene ID (for child scene nodes)
        uint32_t entitySceneId = 0;         // Scene ID that owns this entity (for child scene entity nodes)
        std::list<TreeNode> children;
    };

    struct EntitySelectionEntry {
        uint32_t sceneId = NULL_PROJECT_SCENE;
        Entity entity = NULL_ENTITY;
    };

    class Structure{
    private:

        Project* project;
        SceneWindow* sceneWindow;

        char nameBuffer[256];
        bool matchCase = false;
        char searchBuffer[256] = "";

        std::vector<uint32_t> selectedScenes;
        std::vector<EntitySelectionEntry> visibleEntitySelectionOrder;
        std::unordered_map<uint32_t, std::unordered_set<Entity>> collapsedEntities;

        bool windowOpen;
        bool focusRequested;

        // Cached entity tree — rebuilding costs several ms/frame for large scenes, so it
        // is only rebuilt when the structure changes (see cache keys) or on a timer.
        TreeNode cachedRoot;
        std::unordered_set<Entity> cachedSceneEntitiesSet;
        std::unordered_set<Entity> cachedPlayCreatedEntities;
        uint32_t cacheSceneId = NULL_PROJECT_SCENE;
        uint64_t cacheStructureVersion = UINT64_MAX;
        size_t cacheEntityCount = SIZE_MAX;
        size_t cacheChildSignature = SIZE_MAX;
        double cacheBuildTime = -1.0;

        Entity openParent;
        uint32_t selectionAnchorSceneId = NULL_PROJECT_SCENE;
        Entity selectionAnchorEntity = NULL_ENTITY;

        // Selection made outside this panel: expand its ancestors and scroll to its row
        EntitySelectionEntry revealRequest;
        std::unordered_set<const TreeNode*> revealOpenNodes;
        const TreeNode* revealTargetNode = nullptr;

        std::vector<Entity> getTopLevelSelectedEntities(Entity draggedEntity);
        std::vector<Entity> getMovableDraggedEntities(Entity draggedEntity, const TreeNode& targetNode, InsertionType type);
        void moveDraggedEntitiesToTarget(const std::vector<Entity>& draggedEntities, Entity target, InsertionType type);
        void moveDraggedEntitiesToRootLevel(const std::vector<Entity>& draggedEntities, const std::unordered_set<Entity>& entitiesSet);
        uint32_t getNodeSceneId(const TreeNode& node) const;
        void collectVisibleEntitySelectionOrder(const TreeNode& node, bool hasSearch);
        std::vector<Entity> getEntitySelectionRange(uint32_t sceneId, Entity startEntity, Entity endEntity) const;
        void removeSelectedEntity(uint32_t sceneId, Entity entity);
        void applyEntitySelection(uint32_t sceneId, Entity entity, bool shiftPressed, bool ctrlPressed);
        void clearSubSelectionForEntity(uint32_t sceneId, Entity entity);
        void resetEntitySelectionAnchor();
        void moveCameraToEditorView(Entity cameraEntity);

        void showNewEntityMenu(bool isScene, Entity parent, bool addToBundle);
        void rebuildEntityTree(SceneProject* sceneProject, TreeNode& root, std::unordered_set<Entity>& sceneEntitiesSet);
        void showIconMenu();
        void showTreeNode(TreeNode& node);
        void syncSceneWindowSelectionHierarchy(const TreeNode& node, uint32_t collapsedSceneId = NULL_PROJECT_SCENE, Entity collapsedAncestor = NULL_ENTITY);
        bool collectRevealPath(TreeNode& node);
        void pushNodeImGuiId(const TreeNode& node);
        void popNodeImGuiId(const TreeNode& node);
        void drawInsertionMarker(const ImVec2& p1, const ImVec2& p2);
        void handleEntityFilesDrop(const std::vector<std::string>& filePaths, Entity parent = NULL_ENTITY);
        void handleSceneFilesDropAsChildScenes(const std::vector<std::string>& filePaths, uint32_t ownerSceneId);
        void showAddChildSceneMenu();

        std::vector<char> buildEntityPayload(const TreeNode& node);
        std::filesystem::path getBundleSaveDirectory() const;
        void saveNodeAsBundle(const TreeNode& node);

        // Search-related methods
        bool nodeMatchesSearch(const TreeNode& node, const std::string& searchLower);
        bool hasMatchingDescendant(const TreeNode& node, const std::string& searchLower);
        void markMatchingNodes(TreeNode& node, const std::string& searchLower);

    public:
        static constexpr const char* WINDOW_NAME = "Structure";

        static std::string getObjectIcon(Signature signature, Scene* scene);

        Structure(Project* project, SceneWindow* sceneWindow);

        void show();
        void revealEntity(uint32_t sceneId, Entity entity);
        void setOpen(bool open);
        bool isOpen() const;
    };

}

