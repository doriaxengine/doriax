// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef WATER_EXCLUSION_COMPONENT_H
#define WATER_EXCLUSION_COMPONENT_H

#include "ecs/Entity.h"
#include "math/AABB.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Vector4.h"
#include <cstdint>
#include <deque>
#include <vector>

#define MAX_WATER_EXCLUSIONS 8 // per water draw, nearest to the camera first
#define WATER_EXCLUSION_MAP_SIZE 128

namespace doriax{

    enum class WaterExclusionShape{
        BOX,
        SPHERE,
        HULL
    };

    // a place the bow passed, which diverging waves spread from
    struct WaterWakeSample{
        Vector2 position;
        Vector2 direction;
        float speed = 0.0f;
        float time = 0.0f;
    };

    // Volume that keeps water out, like a boat: the water surface inside it is not drawn,
    // a camera inside sees no underwater fade and the bodies inside do not float
    struct DORIAX_API WaterExclusionComponent{
        WaterExclusionShape shape = WaterExclusionShape::HULL;
        Vector3 center = Vector3(0.0f, 0.0f, 0.0f); // of the box or sphere, local
        Vector3 size = Vector3(1.0f, 1.0f, 1.0f); // of the box or sphere, local
        // left on the water while moving, 0 = none
        float wakeFoam = 0.0f;
        float wakeWaves = 0.0f;

        // convex hull of the entity's meshes in its local space, built by RenderSystem
        std::vector<Vector4> hullPlanes; // xyz = outward normal, w = distance
        std::vector<Vector3> hullPoints;
        AABB hullBounds;
        std::vector<unsigned char> hullMap; // RGBA texels: bottom, top, outside the walls
        uint64_t hullSignature = 0; // of the geometry the hull was built from
        int hullLayer = -1; // in RenderSystem's hull map array

        AABB aabb;
        AABB worldAABB;

        // tracked by RenderSystem
        std::deque<WaterWakeSample> wakeHistory;
        Vector3 wakeLastPosition;
        Vector2 wakeVelocity;
        Vector2 wakeLastStern;
        float wakeTime = 0.0f;
        bool wakeTracked = false;
        bool wakeSternValid = false;
        Entity wakeWater = NULL_ENTITY; // the water it crosses
    };

}

#endif //WATER_EXCLUSION_COMPONENT_H
