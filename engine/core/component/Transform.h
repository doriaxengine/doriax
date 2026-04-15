//
// (c) 2026 Eduardo Doria.
//

#ifndef TRANSFORM_H
#define TRANSFORM_H

#include "math/Vector3.h"
#include "math/Quaternion.h"
#include "math/Matrix4.h"
#include "ecs/Entity.h"

namespace doriax{

    struct DORIAX_API Transform{
        Vector3 position;
        Quaternion rotation;
        Vector3 scale = Vector3(1.0, 1.0, 1.0);

        Vector3 worldPosition;
        Quaternion worldRotation;
        Vector3 worldScale;

        Matrix4 localMatrix;

        Matrix4 modelMatrix;
        Matrix4 normalMatrix;

        // internal use only, it depends of camera (same scene can have multiple cameras)
        Matrix4 modelViewProjectionMatrix;

        bool visible = true;

        Entity parent = NULL_ENTITY;

        float distanceToCamera = 0;

        Quaternion billboardRotation;

        bool billboard = false;
        bool fakeBillboard = false;
        bool cylindricalBillboard = false;

        bool needUpdateChildVisibility = false;
        bool needUpdate = true;
    };

}

#endif //TRANSFORM_H