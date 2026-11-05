// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef BODY2D_COMPONENT_H
#define BODY2D_COMPONENT_H

#include "Engine.h"
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/Quaternion.h"
#include "util/HybridArray.h"
#include <vector>
#ifdef DORIAX_PHYSICS_2D
#include "box2d/box2d.h"
#endif

namespace doriax{

    enum class Shape2DType{
        POLYGON,
        CIRCLE,
        CAPSULE,
        SEGMENT,
        CHAIN // chain is a different type in Box2D 3.x
    };

    enum class Shape2DSource{
        NONE,
        ENTITY_TILEMAP // polygon boxes over the entity's tiles whose rect has collision
    };

    struct DORIAX_API Shape2D{
#ifdef DORIAX_PHYSICS_2D
        b2ShapeId shape = b2_nullShapeId;
        b2ChainId chain = b2_nullChainId;
        // one Box2D polygon per box of a tilemap source
        std::vector<b2ShapeId> tileShapes;
#endif

        Shape2DType type = Shape2DType::POLYGON;
        Shape2DSource source = Shape2DSource::NONE;

        Vector2 pointA = Vector2::ZERO;
        Vector2 pointB = Vector2::ZERO;
        float radius = 0.0f;

        HybridArray<Vector2, MAX_SHAPE_POINTS_2D> vertices;
        uint8_t numVertices = 0;
        bool loop = false;

        // mass = area * density, in metres after pointsToMeterScale2D.
        // Shape3D::density is volume-based and defaults to 1000.
        float density = 1.0f;
        float friction = 0.6f;
        float restitution = 0.0f;

        // overlap only: always fires the sensor contact events, never collides
        bool sensor = false;

        bool enableHitEvents = false;
        bool contactEvents = false;
        bool preSolveEvents = false;
        bool sensorEvents = false;

        uint16_t categoryBits = 1;
        uint16_t maskBits = 0xFFFF;
        int16_t groupIndex = 0;
    };

    struct Body2DComponent{
#ifdef DORIAX_PHYSICS_2D
        b2BodyId body = b2_nullBodyId;
#endif

        HybridArray<Shape2D, MAX_SHAPES_2D> shapes;
        size_t numShapes = 0;

        bool needReloadBody = true;
        bool needUpdateShapes = true;

        BodyType type = BodyType::STATIC;
        bool newBody = true;
        Vector2 loadedScale = Vector2::UNIT_SCALE;
        bool followingTransform = false; // kinematic, moving to its Transform with a velocity
#ifdef DORIAX_PHYSICS_2D
        // pose before the last step, drawn towards the current one
        b2Transform previousTransform = b2Transform_identity;
#endif
        // pose physics last wrote to the Transform
        Vector3 syncedPosition;
        Quaternion syncedRotation;
    };

}

#endif //BODY2D_COMPONENT_H
