// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Workspace.h"

#include "Out.h"
#include "Stream.h"
#include "render/SceneRender2D.h"
#include "render/SceneRender3D.h"
#include "util/TextEncoding.h"

#include <fstream>

namespace fs = std::filesystem;

namespace doriax::editor {

namespace {

const char* tabTypeToString(TabType type) {
    switch (type) {
        case TabType::CODE_EDITOR:  return "codeeditor";
        case TabType::IMAGE_VIEWER: return "imageviewer";
        case TabType::SCENE:
        default:                    return "scene";
    }
}

bool stringToTabType(const std::string& str, TabType& out) {
    if (str == "scene")       { out = TabType::SCENE;        return true; }
    if (str == "codeeditor")  { out = TabType::CODE_EDITOR;  return true; }
    if (str == "imageviewer") { out = TabType::IMAGE_VIEWER; return true; }
    return false;
}

std::vector<TabEntry> decodeTabs(const YAML::Node& node) {
    std::vector<TabEntry> tabs;
    for (const auto& tabNode : node) {
        if (!tabNode.IsMap() || !tabNode["type"] || !tabNode["filepath"]) continue;
        TabType type;
        if (!stringToTabType(tabNode["type"].as<std::string>(), type)) continue;
        tabs.push_back({type, tabNode["filepath"].as<std::string>()});
    }
    return tabs;
}

Workspace::SceneState decodeSceneState(const YAML::Node& node) {
    Workspace::SceneState state;
    state.hasDisplaySettings = Stream::hasSceneDisplaySettings(node);
    Stream::decodeSceneDisplaySettings(node, state.displaySettings);
    if (node["editorCamera"]) {
        state.editorCamera = YAML::Clone(node["editorCamera"]);
    }
    return state;
}

// Every field here is a view preference, so a section hand-edited into something a
// conversion chokes on costs that section and nothing else.
template <typename Fn>
void readSection(const char* what, const std::string& source, Fn&& read) {
    try {
        read();
    } catch (const std::exception& e) {
        Out::warning("Ignoring %s in \"%s\": %s", what, source.c_str(), e.what());
    }
}

} // namespace

fs::path Workspace::getUserDirectory(const fs::path& projectPath) {
    return projectPath / ".doriax" / "user";
}

fs::path Workspace::getWorkspaceFile(const fs::path& projectPath) {
    return getUserDirectory(projectPath) / "workspace.yaml";
}

std::string Workspace::sceneKey(const fs::path& sceneFilepath) {
    return sceneFilepath.lexically_normal().generic_string();
}

void Workspace::clear() {
    tabs.clear();
    tabsDefined = false;
    selectedScene = NULL_PROJECT_SCENE;
    selectedSceneDefined = false;
    terrainEditorSettings = TerrainEditorSettings();
    terrainDefined = false;
    simulateTouch = false;
    sceneStates.clear();
    adoptedLegacy = false;
}

const Workspace::SceneState* Workspace::getSceneState(const fs::path& sceneFilepath) const {
    auto it = sceneStates.find(sceneKey(sceneFilepath));
    return it == sceneStates.end() ? nullptr : &it->second;
}

void Workspace::load(const fs::path& projectPath) {
    clear();

    if (projectPath.empty()) {
        return;
    }

    const fs::path file = getWorkspaceFile(projectPath);
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return;
    }

    YAML::Node root;
    try {
        root = TextEncoding::loadYamlFile(file);
    } catch (const std::exception& e) {
        Out::warning("Ignoring unreadable editor workspace \"%s\": %s", file.string().c_str(), e.what());
        return;
    }

    if (!root.IsMap()) {
        return;
    }

    const std::string source = file.string();

    // Committed only once whole: a half-read list that still counts as defined would
    // shadow the complete one project.yaml may hold
    readSection("open tabs", source, [&]() {
        if (!root["tabs"] || !root["tabs"].IsSequence()) return;
        tabs = decodeTabs(root["tabs"]);
        tabsDefined = true;
    });

    readSection("the selected scene", source, [&]() {
        if (!root["selectedScene"]) return;
        selectedScene = root["selectedScene"].as<uint32_t>();
        selectedSceneDefined = true;
    });

    readSection("the terrain brush", source, [&]() {
        if (!root["terrainEditor"] || !root["terrainEditor"].IsMap()) return;
        terrainEditorSettings = Stream::decodeTerrainEditorSettings(root["terrainEditor"]);
        terrainDefined = true;
    });

    readSection("touch simulation", source, [&]() {
        if (!root["simulateTouch"]) return;
        simulateTouch = root["simulateTouch"].as<bool>();
    });

    if (root["scenes"] && root["scenes"].IsMap()) {
        for (const auto& entry : root["scenes"]) {
            readSection("a scene view state", source, [&]() {
                const std::string key = sceneKey(entry.first.as<std::string>());
                if (key.empty() || !entry.second.IsMap()) return;
                sceneStates[key] = decodeSceneState(entry.second);
            });
        }
    }
}

void Workspace::adoptLegacyProjectState(const YAML::Node& projectNode) {
    if (!projectNode.IsMap()) {
        return;
    }

    const std::string source = "project.yaml";

    readSection("open tabs", source, [&]() {
        if (tabsDefined || !projectNode["tabs"] || !projectNode["tabs"].IsSequence()) return;
        tabs = decodeTabs(projectNode["tabs"]);
        tabsDefined = true;
        adoptedLegacy = true;
    });

    readSection("the selected scene", source, [&]() {
        if (selectedSceneDefined || !projectNode["selectedScene"]) return;
        selectedScene = projectNode["selectedScene"].as<uint32_t>();
        selectedSceneDefined = true;
        adoptedLegacy = true;
    });

    readSection("the terrain brush", source, [&]() {
        if (terrainDefined || !projectNode["terrainEditor"] || !projectNode["terrainEditor"].IsMap()) return;
        terrainEditorSettings = Stream::decodeTerrainEditorSettings(projectNode["terrainEditor"]);
        terrainDefined = true;
        adoptedLegacy = true;
    });

    if (projectNode["scenes"] && projectNode["scenes"].IsSequence()) {
        for (const auto& sceneNode : projectNode["scenes"]) {
            readSection("a scene view state", source, [&]() {
                if (!sceneNode.IsMap() || !sceneNode["filepath"]) return;

                // A scene the project only lists is already in the new format, and
                // taking it as legacy would mark every fresh clone as needing a
                // migration it does not need
                if (!sceneNode["editorCamera"] && !Stream::hasSceneDisplaySettings(sceneNode)) return;

                const std::string key = sceneKey(sceneNode["filepath"].as<std::string>());
                if (key.empty() || sceneStates.count(key)) return;

                sceneStates[key] = decodeSceneState(sceneNode);
                adoptedLegacy = true;
            });
        }
    }
}

std::map<std::string, YAML::Node> Workspace::statesForUnresolvedScenes(const Project* project) const {
    std::map<std::string, YAML::Node> retained;
    if (!project) {
        return retained;
    }

    for (const auto& [position, filepath] : project->getUnresolvedScenes()) {
        const std::string key = sceneKey(filepath);
        auto it = sceneStates.find(key);
        if (it == sceneStates.end()) {
            continue;
        }

        YAML::Node sceneNode = it->second.hasDisplaySettings
            ? Stream::encodeSceneDisplaySettings(it->second.displaySettings)
            : YAML::Node(YAML::NodeType::Map);
        if (it->second.editorCamera.IsDefined()) {
            sceneNode["editorCamera"] = YAML::Clone(it->second.editorCamera);
        }
        if (sceneNode.size()) {
            retained[key] = sceneNode;
        }
    }

    return retained;
}

bool Workspace::save(const Project* project) {
    if (!project || project->getProjectPath().empty()) {
        return false;
    }

    const fs::path projectPath = project->getProjectPath();
    const fs::path file = getWorkspaceFile(projectPath);

    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        Out::error("Failed to create editor workspace directory: %s", ec.message().c_str());
        return false;
    }

    YAML::Node root;
    root["selectedScene"] = project->getSelectedSceneId();

    YAML::Node tabsNode(YAML::NodeType::Sequence);
    for (const TabEntry& tab : project->getTabs()) {
        YAML::Node tabNode;
        tabNode["type"] = tabTypeToString(tab.type);
        tabNode["filepath"] = tab.filepath;
        tabsNode.push_back(tabNode);
    }
    root["tabs"] = tabsNode;

    root["terrainEditor"] = Stream::encodeTerrainEditorSettings(project->getTerrainEditorSettings());
    root["simulateTouch"] = project->isSimulateTouch();

    YAML::Node scenesNode(YAML::NodeType::Map);
    for (const SceneProject& sceneProject : project->getScenes()) {
        if (sceneProject.filepath.empty()) {
            continue;
        }

        YAML::Node sceneNode = Stream::encodeSceneDisplaySettings(sceneProject.displaySettings);

        // A closed scene has no renderer, so its camera is whatever was last read
        if (sceneProject.sceneRender) {
            if (Camera* editorCam = sceneProject.sceneRender->getCamera()) {
                float zoom = 0.0f;
                float walkSpeedOffset = 0.0f;
                if (sceneProject.sceneType == SceneType::SCENE_2D || sceneProject.sceneType == SceneType::SCENE_UI) {
                    zoom = static_cast<SceneRender2D*>(sceneProject.sceneRender)->getZoom();
                } else if (sceneProject.sceneType == SceneType::SCENE_3D) {
                    walkSpeedOffset = static_cast<SceneRender3D*>(sceneProject.sceneRender)->getWalkSpeedOffset();
                }
                sceneNode["editorCamera"] = Stream::encodeEditorCamera(editorCam, zoom, walkSpeedOffset);
            }
        } else if (sceneProject.editorCameraState.IsDefined()) {
            sceneNode["editorCamera"] = sceneProject.editorCameraState;
        }

        scenesNode[sceneKey(sceneProject.filepath)] = sceneNode;
    }

    // Scenes this checkout could not open keep what was last known about them
    for (const auto& [key, sceneNode] : project->getUnresolvedSceneStates()) {
        if (!scenesNode[key]) {
            scenesNode[key] = sceneNode;
        }
    }
    root["scenes"] = scenesNode;

    try {
        std::ofstream fout(file.string());
        if (!fout) {
            Out::error("Failed to open editor workspace for writing: %s", file.string().c_str());
            return false;
        }

        fout << "# Editor state for this checkout only. Not committed; deleting it\n"
                "# costs nothing but the layout it holds.\n";
        fout << YAML::Dump(root) << "\n";
        fout.close();

        if (!fout) {
            Out::error("Failed to write editor workspace: %s", file.string().c_str());
            return false;
        }
    } catch (const std::exception& e) {
        Out::error("Failed to save editor workspace: \"%s\"", e.what());
        return false;
    }

    return true;
}

} // namespace doriax::editor
