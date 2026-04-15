//
// (c) 2026 Eduardo Doria.
//

#ifndef BODY3D_COMPONENT_H
#define BODY3D_COMPONENT_H

#include "Engine.h"
#include "ecs/Entity.h"
#include "util/HybridArray.h"

#include "Jolt/Jolt.h"
#include "Jolt/Physics/Body/Body.h"

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

    struct DORIAX_API Shape3D{
        JPH::ShapeRefC shape = NULL;
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
        JPH::BodyID body;

        HybridArray<Shape3D, MAX_SHAPES> shapes;
        size_t numShapes = 0;

        bool needReloadBody = true;
        bool needUpdateShapes = true;

        bool overrideMassProperties = false;
        Vector3 solidBoxSize;
        float solidBoxDensity;
        
        BodyType type = BodyType::STATIC;
        bool newBody = true;
    };

}

#endif //BODY3D_COMPONENT_H