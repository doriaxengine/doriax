#pragma once
#include <cstddef>
#include <cstdint>
#include "ecs/Entity.h"

namespace doriax {

    struct EntityPayload {
        Entity entity;
        Entity parent;
        size_t order;
        bool hasTransform;
        uint32_t entitySceneId = 0; // source scene ID (0 = current scene)
    }; // YAML string follows

    struct MaterialPayload {
        uint32_t magic;
        uint32_t sceneId;
        Entity entity;
        unsigned int submeshIndex;
    }; // YAML string follows

    struct TileRectPayload {
        uint32_t sceneId;
        Entity entity;
        int rectIndex;
    };

}