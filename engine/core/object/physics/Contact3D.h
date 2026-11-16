// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef Contact3D_H
#define Contact3D_H

#include "Entity.h"
#include "Body3D.h"

#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Collision/ContactListener.h"

namespace doriax{

    class DORIAX_API Contact3D{
    private:
        Scene* scene;
        // of the body a side is, 0 for a character
        size_t shapeIndex1;
        size_t shapeIndex2;
        const JPH::ContactManifold* contactManifold;
        JPH::ContactSettings* contactSettings;

    public:
        Contact3D(Scene* scene, size_t shapeIndex1, size_t shapeIndex2, const JPH::ContactManifold* contactManifold, JPH::ContactSettings* contactSettings);
        virtual ~Contact3D();

        Contact3D(const Contact3D& rhs);
        Contact3D& operator=(const Contact3D& rhs);

        const JPH::ContactManifold* getJoltContactManifold() const;
        JPH::ContactSettings* getJoltContactSettings() const;

        // ContactManifold
        Vector3 getBaseOffset() const;
        Vector3 getWorldSpaceNormal() const;
        float getPenetrationDepth() const;
        size_t getShapeIndex1() const;
        size_t getShapeIndex2() const;
        Vector3 getRelativeContactPointsOnA(size_t index) const;
        Vector3 getRelativeContactPointsOnB(size_t index) const;


        // ContactSettings
        float getCombinedFriction() const;
        void setCombinedFriction(float combinedFriction);

        float getCombinedRestitution() const;
        void setCombinedRestitution(float combinedRestitution);

        bool isSensor() const;
        void setIsSensor(bool sensor);
    };
}

#endif //Contact3D_H