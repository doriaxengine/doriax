//
// (c) 2026 Eduardo Doria.
//

#ifndef BODY2D_COMPONENT_H
#define BODY2D_COMPONENT_H

#include "Engine.h"
#include "math/Vector2.h"
#include "util/HybridArray.h"
#include "box2d/box2d.h"

namespace doriax{

    enum class Shape2DType{
        POLYGON,
        CIRCLE,
        CAPSULE,
        SEGMENT,
        CHAIN // chain is a different type in Box2D 3.x
    };

    struct DORIAX_API Shape2D{
        b2ShapeId shape = b2_nullShapeId;
        b2ChainId chain = b2_nullChainId;

        Shape2DType type = Shape2DType::POLYGON;

        Vector2 pointA = Vector2::ZERO;
        Vector2 pointB = Vector2::ZERO;
        float radius = 0.0f;

        HybridArray<Vector2, MAX_SHAPE_POINTS_2D> vertices;
        uint8_t numVertices = 0;
        bool loop = false;

        float density = 1.0f;
        float friction = 0.6f;
        float restitution = 0.0f;

        bool enableHitEvents = false;
        bool contactEvents = false;
        bool preSolveEvents = false;
        bool sensorEvents = false;

        uint16_t categoryBits = 1;
        uint16_t maskBits = 0xFFFF;
        int16_t groupIndex = 0;
    };

    struct Body2DComponent{
        b2BodyId body = b2_nullBodyId;

        HybridArray<Shape2D, MAX_SHAPES> shapes;
        size_t numShapes = 0;

        bool needReloadBody = true;
        bool needUpdateShapes = true;

        BodyType type = BodyType::STATIC;
        bool newBody = true;
    };

}

#endif //BODY2D_COMPONENT_H