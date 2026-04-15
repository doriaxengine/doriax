#pragma once

#include "object/Camera.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace doriax::editor {
    class GraphicUtils {
    public:
        static void saveFramebufferImage(Framebuffer* framebuffer, fs::path path, bool flipY = false, std::function<void()> onComplete = nullptr);

        static Vector2 getUILayoutCenter(Scene* scene, Entity entity, const UILayoutComponent& layout);
    };
}