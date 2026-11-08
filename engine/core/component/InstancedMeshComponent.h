// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef INSTANCED_MESH_COMPONENT_H
#define INSTANCED_MESH_COMPONENT_H

#include "Engine.h"
#include "math/Rect.h"

#include <vector>

namespace doriax{

    struct InstanceData{
        Vector3 position = Vector3(0.0, 0.0, 0.0);
        Quaternion rotation;
        Vector3 scale = Vector3(1.0, 1.0, 1.0);
        Vector4 color = Vector4(1.0, 1.0, 1.0, 1.0);  //linear color;
        Rect textureRect = Rect(0.0, 0.0, 1.0, 1.0);
        bool visible = true;
    };

    struct InstanceRenderData{
        Matrix4 instanceMatrix;
        Vector4 color;
        Rect textureRect;
    };

    struct InstanceBounds{
        Vector3 center; // model space, so an entity move needs no rebuild
        float radius = 0;
        float scale = 1; // largest instance axis
    };

    // slice of a RenderSystem instance view, nearest first and grouped by detail level
    struct InstanceViewRange{
        unsigned int offset = 0;
        unsigned int lodCount[MAX_MESH_LODS] = {};

        unsigned int count() const{
            unsigned int total = 0;
            for (unsigned int c : lodCount) total += c;
            return total;
        }
    };

    // settings the views depend on, compared each frame since setters do not flag a rebuild
    struct InstanceViewSettings{
        bool cull = true;
        bool castShadows = true;
        bool lodEnabled = true;
        float lodBias = 1;
        float cullDistance = 0;
        float windReach = 0; // swayed vertices reach past the instance spheres

        bool operator!=(const InstanceViewSettings& o) const{
            return cull != o.cull || castShadows != o.castShadows || lodEnabled != o.lodEnabled ||
                   lodBias != o.lodBias || cullDistance != o.cullDistance || windReach != o.windReach;
        }
    };

    struct DORIAX_API InstancedMeshComponent{
        ExternalBuffer buffer;

        std::vector<InstanceData> instances;
        std::vector<InstanceRenderData> renderInstances; //must be sorted
        std::vector<InstanceBounds> renderBounds; // parallel to renderInstances

        unsigned int maxInstances = 100;
        unsigned int numVisible = 0;

        // culled slices: [0] main camera, [1 + s] shadow atlas slot s; other cameras draw them all
        InstanceViewRange views[1 + MAX_SHADOW_ATLAS_SLOTS];
        InstanceViewSettings viewSettings; // the views were built with these
        bool cullInstances = true; // off for shaders that move instances away from their bounds

        // world-space distance from the main camera past which instances are dropped from
        // the culled views (0 = unlimited); a hard cut, so hide it with fog
        float cullDistance = 0;

        // Instances shrink to nothing between fadeStart and fadeEnd, both model-space distances.
        // distanceFade picks the shader variant, so an empty range disables it without a rebuild.
        bool distanceFade = false;
        float fadeStart = 0;
        float fadeEnd = 0;
        Vector3 fadeEyeLocal; //camera in model space, where the range is measured

        // instances nearer than this model-space XZ distance are dropped (0 = none), set by foliage
        float cullNearDistance = 0;

        // fade range the render lists were cut at (render system cache)
        float lastFadeCullEnd = -1;
        float lastCullNear = 0;
        Vector3 lastFadeCullEye;
        float lastCullSlack = 0; // eye movement the lists still cover

        bool instancedBillboard = false;
        bool instancedCylindricalBillboard = false;

        bool needUpdateBuffer = false;
        bool needUpdateInstances = true;
    };

}

#endif //INSTANCED_MESH_COMPONENT_H
