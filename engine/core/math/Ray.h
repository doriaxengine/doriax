// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef ray_h
#define ray_h

#include "Vector3.h"
#include "Plane.h"
#include "AABB.h"
#include "Sphere.h"
#include "OBB.h"
#include "Entity.h"
#include <cstdint>
#include <vector>

namespace doriax {

    class Scene;
#ifdef DORIAX_PHYSICS_2D
    class Body2D;
#endif
#ifdef DORIAX_PHYSICS_3D
    class Body3D;
#endif

    enum class RayFilter{
        BODY_2D,
        BODY_3D,
        BOUNDS
    };

    struct RayReturn{
        bool hit;
        float distance;
        Vector3 point;
        Vector3 normal;
        Entity body;
        size_t shapeIndex;

        operator bool() const {
            return hit;
        }
    };

    class DORIAX_API Ray{

    private:

        Vector3 origin;
        Vector3 direction; // direction and length of the ray (anything beyond this length will not be a hit)

    public:

        static const RayReturn NO_HIT;

        Ray();
        Ray(const Ray &ray);
        Ray(Vector3 origin, Vector3 direction);

        Ray &operator=(const Ray &);
        Vector3 operator*(float t);

        void setOrigin(Vector3 point);
        Vector3 getOrigin() const;

        void setDirection(Vector3 direction);
        Vector3 getDirection() const;

        Vector3 getPoint(float distance) const;

        RayReturn intersects(const Plane& plane) const;
        RayReturn intersects(const AABB& box) const;
        RayReturn intersects(const OBB& obb) const;
        RayReturn intersects(const Sphere& sphere) const;
#ifdef DORIAX_PHYSICS_2D
        RayReturn intersects(const Body2D& body) const;
        RayReturn intersects(const Body2D& body, size_t shape) const;
#endif
#ifdef DORIAX_PHYSICS_3D
        RayReturn intersects(const Body3D& body) const;
        RayReturn intersects(const Body3D& body, size_t shape) const;
#endif
        RayReturn intersects(Scene* scene, RayFilter raytest) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, bool onlyStatic) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, uint16_t categoryBits, uint16_t maskBits) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, bool onlyStatic, uint16_t categoryBits, uint16_t maskBits) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, Entity ignoreEntity) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, const std::vector<Entity>& ignoreEntities) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, bool onlyStatic, uint16_t categoryBits, uint16_t maskBits, Entity ignoreEntity) const;
        RayReturn intersects(Scene* scene, RayFilter raytest, bool onlyStatic, uint16_t categoryBits, uint16_t maskBits, const std::vector<Entity>& ignoreEntities) const;
#ifdef DORIAX_PHYSICS_3D
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D) const; // only 3D bodies
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D, uint16_t categoryBits, uint16_t maskBits) const; // only 3D bodies
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D, Entity ignoreEntity) const; // only 3D bodies
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D, const std::vector<Entity>& ignoreEntities) const; // only 3D bodies
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D, uint16_t categoryBits, uint16_t maskBits, Entity ignoreEntity) const; // only 3D bodies
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D, uint16_t categoryBits, uint16_t maskBits, const std::vector<Entity>& ignoreEntities) const; // only 3D bodies
#endif

    private:

        RayReturn intersects(Scene* scene, RayFilter raytest, bool onlyStatic, uint16_t categoryBits, uint16_t maskBits, const std::vector<Entity>* ignoreEntities) const;
#ifdef DORIAX_PHYSICS_3D
        RayReturn intersects(Scene* scene, uint8_t broadPhaseLayer3D, uint16_t categoryBits, uint16_t maskBits, const std::vector<Entity>* ignoreEntities) const;
#endif
    };
    
}


#endif /* ray_h */
