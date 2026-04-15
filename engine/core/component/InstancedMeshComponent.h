//
// (c) 2026 Eduardo Doria.
//

#ifndef INSTANCED_MESH_COMPONENT_H
#define INSTANCED_MESH_COMPONENT_H

#include "math/Rect.h"

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

    struct DORIAX_API InstancedMeshComponent{
        ExternalBuffer buffer;

        std::vector<InstanceData> instances;
        std::vector<InstanceRenderData> renderInstances; //must be sorted

        unsigned int maxInstances = 100;
        unsigned int numVisible = 0;

        bool instancedBillboard = false;
        bool instancedCylindricalBillboard = false;

        bool needUpdateBuffer = false;
        bool needUpdateInstances = true;
    };

}

#endif //INSTANCED_MESH_COMPONENT_H
