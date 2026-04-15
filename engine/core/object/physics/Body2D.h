//
// (c) 2026 Eduardo Doria.
//

#ifndef BODY2D_H
#define BODY2D_H

#include "EntityHandle.h"
#include "math/Vector2.h"
#include "component/Body2DComponent.h"


namespace doriax{

    class Contact2D;
    class Object;

    class DORIAX_API Body2D: public EntityHandle{
    protected:
        void checkBody(const Body2DComponent& body) const;

    public:
        Body2D(Scene* scene, Entity entity);
        virtual ~Body2D();

        Body2D(const Body2D& rhs);
        Body2D& operator=(const Body2D& rhs);

        b2BodyId getBox2DBody() const;
        b2ShapeId getBox2DShape(size_t index) const;
        b2ChainId getBox2DChain(size_t index) const;

        Object getAttachedObject();

        float getPointsToMeterScale() const;

        void load();

        int createBoxShape(float width, float height);
        int createCenteredBoxShape(float width, float height);
        int createCenteredBoxShape(float width, float height, Vector2 center, float angle);
        int createRoundedBoxShape(float width, float height, float radius);
        int createPolygonShape(std::vector<Vector2> vertices);
        int createCircleShape(Vector2 center, float radius);
        int createCapsuleShape(Vector2 center1, Vector2 center2, float radius);
        int createSegmentShape(Vector2 point1, Vector2 point2);
        int createChainShape(std::vector<Vector2> vertices, bool loop);

        void removeAllShapes();

        std::vector<Contact2D> getBodyContacts();
        std::vector<Contact2D> getShapeContacts(size_t index);

        size_t getNumShapes() const;
        Shape2DType getShapeType(size_t index) const;

        void setShapeDensity(float density);
        void setShapeFriction(float friction);
        void setShapeRestitution(float restitution);

        void setShapeDensity(size_t index, float density);
        void setShapeFriction(size_t index, float friction);
        void setShapeRestitution(size_t index, float restitution);

        void setShapeEnableHitEvents(bool hitEvents);
        void setShapeContactEvents(bool contactEvents);
        void setShapePreSolveEvents(bool preSolveEvent);
        void setShapeSensorEvents(bool sensorEvents);

        void setShapeEnableHitEvents(size_t index, bool hitEvents);
        void setShapeContactEvents(size_t index, bool contactEvents);
        void setShapePreSolveEvents(size_t index, bool preSolveEvent);
        void setShapeSensorEvents(size_t index, bool sensorEvents);

        float getShapeDensity() const;
        float getShapeFriction() const;
        float getShapeRestitution() const;

        float getShapeDensity(size_t index) const;
        float getShapeFriction(size_t index) const;
        float getShapeRestitution(size_t index) const;

        bool isShapeEnableHitEvents() const;
        bool isShapeContactEvents() const;
        bool isShapePreSolveEvents() const;
        bool isShapeSensorEvents() const;

        bool isShapeEnableHitEvents(size_t index) const;
        bool isShapeContactEvents(size_t index) const;
        bool isShapePreSolveEvents(size_t index) const;
        bool isShapeSensorEvents(size_t index) const;

        void setLinearVelocity(Vector2 linearVelocity);
        void setAngularVelocity(float angularVelocity);
        void setLinearDamping(float linearDamping);
        void setAngularDamping(float angularDamping);
        void setEnableSleep(bool enableSleep);
        void setAwake(bool awake);
        void setFixedRotation(bool fixedRotation);
        void setBullet(bool bullet);
        void setType(BodyType type);
        void setEnabled(bool enabled);
        void setGravityScale(float gravityScale);

        Vector2 getLinearVelocity() const;
        float getAngularVelocity() const;
        float getLinearDamping() const;
        float getAngularDamping() const;
        bool isEnableSleep() const;
        bool isAwake() const;
        bool isFixedRotation() const;
        bool isBullet() const;
        BodyType getType() const;
        bool isEnabled() const;
        float getGravityScale() const;

        void setBitsFilter(uint16_t categoryBits, uint16_t maskBits);
        void setBitsFilter(size_t shapeIndex, uint16_t categoryBits, uint16_t maskBits);

        void setCategoryBitsFilter(uint16_t categoryBits);
        void setMaskBitsFilter(uint16_t maskBits);
        void setGroupIndexFilter(int16_t groupIndex);

        void setCategoryBitsFilter(size_t shapeIndex, uint16_t categoryBits);
        void setMaskBitsFilter(size_t shapeIndex, uint16_t maskBits);
        void setGroupIndexFilter(size_t shapeIndex, int16_t groupIndex);

        uint16_t getCategoryBitsFilter() const;
        uint16_t getMaskBitsFilter() const;
        int16_t getGroupIndexFilter() const;

        uint16_t getCategoryBitsFilter(size_t shapeIndex) const;
        uint16_t getMaskBitsFilter(size_t shapeIndex) const;
        int16_t getGroupIndexFilter(size_t shapeIndex) const;

        float getMass() const;
        float getRotationalInertia() const;

        void applyMassFromShapes();

        void applyForce(const Vector2& force, const Vector2& point, bool wake);
        void applyForceToCenter(const Vector2& force, bool wake);
        void applyTorque(float torque, bool wake);
        void applyLinearImpulse(const Vector2& impulse, const Vector2& point, bool wake);
        void applyLinearImpulseToCenter(const Vector2& impulse, bool wake);
        void applyAngularImpulse(float impulse, bool wake);
    };
}

#endif //BODY2D_H