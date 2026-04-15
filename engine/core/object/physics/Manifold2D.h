//
// (c) 2026 Eduardo Doria.
//

#ifndef MANIFOLD2D_H
#define MANIFOLD2D_H

#include "Scene.h"
#include "box2d/box2d.h"

namespace doriax{

    class DORIAX_API Manifold2D{
        private:
            Scene* scene;
            const b2Manifold* manifold;

        public:
            Manifold2D(Scene* scene, const b2Manifold* manifold);
            virtual ~Manifold2D();

            Manifold2D(const Manifold2D& rhs);
            Manifold2D& operator=(const Manifold2D& rhs);

            const b2Manifold* getBox2DManifold() const;
    
            Vector2 getManifoldPointAnchorA(int32_t index) const;
            Vector2 getManifoldPointAnchorB(int32_t index) const;
            Vector2 getManifoldPointPosition(int32_t index) const;
            float getManifoldPointNormalImpulse(int32_t index) const;
            float getManifoldPointNormalVelocity(int32_t index) const;
            float getManifoldPointTangentImpulse(int32_t index) const;
            float getManifoldPointSeparation(int32_t index) const;
            bool isManifoldPointPersisted(int32_t index) const;

            Vector2 getNormal() const;
            int32_t getPointCount() const;
    };

}

#endif //MANIFOLD2D_H