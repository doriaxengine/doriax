//
// (c) 2026 Eduardo Doria.
//

#ifndef CONTACT2D_H
#define CONTACT2D_H

#include "Entity.h"
#include "Body2D.h"
#include "Manifold2D.h"

#include "box2d/box2d.h"

namespace doriax{

    class DORIAX_API Contact2D{
    private:
        Scene* scene;
        b2ContactData contact;

    public:
        Contact2D(Scene* scene, b2ContactData contact);
        virtual ~Contact2D();

        Contact2D(const Contact2D& rhs);
        Contact2D& operator=(const Contact2D& rhs);

        b2ContactData getBox2DContact() const;

        Manifold2D getManifold() const;

        Entity getBodyEntityA() const;
        Body2D getBodyA() const;
        size_t getShapeIndexA() const; // fixture in Box2D

        Entity getBodyEntityB() const;
        Body2D getBodyB() const;
        size_t getShapeIndexB() const; // fixture in Box2D
    };

}

#endif //CONTACT2D_H