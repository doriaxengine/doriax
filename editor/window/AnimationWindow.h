#ifndef ANIMATIONWINDOW_H
#define ANIMATIONWINDOW_H

#include "Project.h"
#include "imgui.h"
#include "yaml-cpp/yaml.h"

#include <unordered_set>

namespace doriax::editor{

    class AnimationWindow{
    private:

        Project* project;

        // Playback state
        bool isPlaying;
        float currentTime;
        float playbackSpeed;

        // Timeline view state
        float pixelsPerSecond;
        float scrollX;
        float minPixelsPerSecond;
        float maxPixelsPerSecond;

        // Selection
        int selectedFrameIndex;
        bool isDraggingPlayhead;
        bool isDraggingFrame;
        int draggingFrameIndex;
        float dragStartTime;
        uint32_t dragStartTrack;

        // Resize
        bool isResizingFrame;
        int resizingFrameIndex;
        int resizeSide; // -1 = left edge, 1 = right edge
        float resizeStartTime;
        float resizeStartDuration;

        // Snap
        bool snapToGrid;
        float snapInterval;

        // Cached entity
        Entity selectedEntity;
        uint32_t selectedSceneId;

        // Tab notification
        bool hasNotification;
        bool isWindowVisible;

        // Animation preview
        bool isPreviewing;
        struct PreviewEntityState {
            Entity entity = NULL_ENTITY;
            Entity parent = NULL_ENTITY;
            YAML::Node components;
        };
        std::vector<PreviewEntityState> previewState;

        // Helpers
        void drawToolbar(float width, AnimationComponent& anim, Scene* scene, SceneProject* sceneProject);
        void drawTimeRuler(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd);
        bool drawTracks(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd,
                AnimationComponent& anim, SceneProject* sceneProject);
        bool drawPlayhead(ImVec2 canvasPos, ImVec2 canvasSize, float timeStart, float timeEnd);

        float snapTime(float time) const;
        float timeToX(float time, float timeStart, ImVec2 canvasPos) const;
        float xToTime(float x, float timeStart, ImVec2 canvasPos) const;

        std::string getActionLabel(Entity actionEntity, Scene* scene) const;
        std::string getAnimationEntityLabel(Entity entity, AnimationComponent& anim, Scene* scene) const;
        bool canPreviewEntity(Entity entity, Scene* scene) const;
        void collectPreviewEntitiesRecursive(Scene* scene, Entity entity, std::vector<Entity>& entities,
                             std::unordered_set<Entity>& visitedAnimations,
                             std::unordered_set<Entity>& collectedEntities) const;
        PreviewEntityState buildPreviewEntityState(Scene* scene, Entity entity) const;
        void restorePreviewState(Scene* scene) const;
        void applyPreviewModelBindPose(Scene* scene) const;
        float getAnimationDuration(const AnimationComponent& anim) const;
        void seekPreview(Scene* scene, SceneProject* sceneProject, float time);

        void startPreview(Scene* scene, SceneProject* sceneProject);
        void stopPreview(Scene* scene, SceneProject* sceneProject, bool applyBindPose = false);

    public:
        static constexpr const char* WINDOW_NAME = "Animation";

        AnimationWindow(Project* project);

        void show();

        void selectEntity(Entity entity, uint32_t sceneId);

        bool isPreviewingEntity(Entity entity, uint32_t sceneId) const;
        bool getIsPlaying() const;
        bool getIsPreviewing() const;
        float getCurrentTime() const;

        void externalPlay(Entity entity, uint32_t sceneId);
        void externalStop();
        void externalPause();
        void seekPreviewExternal(Scene* scene, SceneProject* sceneProject, float time);
    };

}

#endif /* ANIMATIONWINDOW_H */
