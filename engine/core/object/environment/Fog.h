//
// (c) 2026 Eduardo Doria.
//

#ifndef FOG_H
#define FOG_H

#include "object/EntityHandle.h"
#include "component/FogComponent.h"

namespace doriax{

    class DORIAX_API Fog: public EntityHandle {

    public:
        Fog(Scene* scene);
        Fog(Scene* scene, Entity entity);
        virtual ~Fog();

        FogType getType() const;
        Vector3 getColor() const;
        float getDensity() const;
        float getLinearStart() const;
        float getLinearEnd() const;

        void setType(FogType type);
        void setColor(Vector3 color);
        void setColor(float red, float green, float blue);
        void setDensity(float density);
        void setLinearStart(float start);
        void setLinearEnd(float end);
        void setLinearStartEnd(float start, float end);
    };
}

#endif //FOG_H