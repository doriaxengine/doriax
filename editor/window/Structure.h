#ifndef STRUCTURE_H
#define STRUCTURE_H

#include "Project.h"
#include "SceneWindow.h"
#include "imgui.h"
#include <list>
#include <string>
#include <vector>
#include <filesystem>
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

    class Structure{
    private:

        Project* project;
        SceneWindow* sceneWindow;

        char nameBuffer[256];
        bool matchCase = false;
        char searchBuffer[256] = "";

        std::vector<uint32_t> selectedScenes;

        Entity openParent;

        std::vector<Entity> getTopLevelSelectedEntities(Entity draggedEntity);

        void showNewEntityMenu(bool isScene, Entity parent, bool addToBundle);
        void showIconMenu();
        void showTreeNode(TreeNode& node);
        void pushNodeImGuiId(const TreeNode& node);
        void popNodeImGuiId(const TreeNode& node);
        void drawInsertionMarker(const ImVec2& p1, const ImVec2& p2);
        std::string getObjectIcon(Signature signature, Scene* scene);
        void handleEntityFilesDrop(const std::vector<std::string>& filePaths, Entity parent = NULL_ENTITY);
        void handleSceneFilesDropAsChildScenes(const std::vector<std::string>& filePaths, uint32_t ownerSceneId);
        void moveEntityToRootLevel(Entity sourceEntity, const std::unordered_set<Entity>& entitiesSet);
        void showAddChildSceneMenu();

        // Search-related methods
        bool nodeMatchesSearch(const TreeNode& node, const std::string& searchLower);
        bool hasMatchingDescendant(const TreeNode& node, const std::string& searchLower);
        void markMatchingNodes(TreeNode& node, const std::string& searchLower);

    public:
        static constexpr const char* WINDOW_NAME = "Structure";

        Structure(Project* project, SceneWindow* sceneWindow);

        void show();
    };

}

#endif /* STRUCTURE_H */