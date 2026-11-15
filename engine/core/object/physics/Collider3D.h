// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef COLLIDER3D_H
#define COLLIDER3D_H

#include "EntityHandle.h"
#include "math/Vector3.h"

namespace doriax{

    class Object;
    class Body3D;
    class Character3D;

    // what an entity is in the 3D physics world: a Body3D or a Character3D
    class DORIAX_API Collider3D: public EntityHandle{
    public:
        Collider3D(Scene* scene, Entity entity);

        Object getAttachedObject();

        // an entity with both is its body
        bool isBody3D() const;
        bool isCharacter3D() const;

        Body3D getBody3D() const;
        Character3D getCharacter3D() const;

        Vector3 getPosition() const;
        Vector3 getLinearVelocity() const;
    };
}

#endif //COLLIDER3D_H
