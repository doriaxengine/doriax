#ifndef sphere_h
#define sphere_h

#include <cmath>
#include "Vector3.h"

namespace doriax{

    class Plane;
    class AABB;
    class OBB;

    class DORIAX_API Sphere {
    public:
        Vector3 center;
        float radius;

        Sphere();
        Sphere(Vector3 c, float r);
        Sphere(const Sphere& s);

        Sphere& operator = (const Sphere& s);
        bool operator == (const Sphere& s);
        bool operator != (const Sphere& s);

        std::string toString() const;

        bool contains(const Vector3& point) const;

        bool intersects(const Sphere& other) const;
        bool intersects(const AABB& aabb) const;
        bool intersects(const OBB& obb) const;
        bool intersects(const Plane& plane) const;
        bool intersects(const Vector3& v) const;

        void merge(const Sphere& other);

        float surfaceArea() const;
        float volume() const;
    };

}

#endif