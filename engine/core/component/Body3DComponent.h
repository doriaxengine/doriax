// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef BODY3D_COMPONENT_H
#define BODY3D_COMPONENT_H

#include "Engine.h"
#include "ecs/Entity.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"
#include "util/HybridArray.h"
#include <cstdint>

#ifdef DORIAX_PHYSICS_3D
#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"
#endif

namespace doriax{

    enum class Shape3DType{
        SPHERE,
        BOX,
        CAPSULE,
        TAPERED_CAPSULE,
        CYLINDER,
        CONVEX_HULL,
        MESH,
        HEIGHTFIELD
    };

    enum class Shape3DSource{
        NONE,
        RAW_VERTICES,
        RAW_MESH,
        ENTITY_MESH,
        ENTITY_HEIGHTFIELD
    };

    enum class Body3DMotionQuality{
        DISCRETE,
        LINEAR_CAST
    };

    enum class Body3DAllowedDOF : uint8_t{
        TRANSLATION_X = 1 << 0,
        TRANSLATION_Y = 1 << 1,
        TRANSLATION_Z = 1 << 2,
        ROTATION_X = 1 << 3,
        ROTATION_Y = 1 << 4,
        ROTATION_Z = 1 << 5,
        ALL = 0x3F
    };

    struct DORIAX_API Shape3D{
#ifdef DORIAX_PHYSICS_3D
        JPH::ShapeRefC shape = NULL;
#endif
        Vector3 position = Vector3::ZERO;
        Quaternion rotation = Quaternion::IDENTITY;

        Shape3DType type = Shape3DType::SPHERE;

        float width = 1.0f;
        float height = 1.0f;
        float depth = 1.0f;

        float radius = 1.0f;
        float halfHeight = 0.5f;
        float topRadius = 0.5f;
        float bottomRadius = 0.5f;
        // kg/m3: mass = scaled volume * density (radius-1 sphere is ~4189 kg).
        // Shape2D::density is area-based and defaults to 1.
        float density = 1000.0f;

        Shape3DSource source = Shape3DSource::NONE;
        Entity sourceEntity = NULL_ENTITY;
        unsigned int samplesSize = 0;

        HybridArray<Vector3, MAX_SHAPE_VERTICES_3D> vertices;
        uint16_t numVertices = 0;
        HybridArray<uint16_t, MAX_SHAPE_INDICES_3D> indices;
        uint16_t numIndices = 0;
    };

    struct Body3DComponent{
#ifdef DORIAX_PHYSICS_3D
        JPH::BodyID body;
#endif

        HybridArray<Shape3D, MAX_SHAPES> shapes;
        size_t numShapes = 0;

        bool needReloadBody = true;
        bool needUpdateShapes = true;

        bool overrideMassProperties = false;
        Vector3 solidBoxSize;
        float solidBoxDensity;
        
        BodyType type = BodyType::STATIC;
        Body3DMotionQuality motionQuality = Body3DMotionQuality::DISCRETE;
        float gravityFactor = 1.0f;
#ifdef DORIAX_PHYSICS_3D
        JPH::EAllowedDOFs allowedDOFs = JPH::EAllowedDOFs::All;
#else
        uint8_t allowedDOFs = static_cast<uint8_t>(Body3DAllowedDOF::ALL);
#endif
        bool sensor = false;
        bool newBody = true;
        Vector3 loadedScale = Vector3::UNIT_SCALE;
    };

}

#endif //BODY3D_COMPONENT_H
