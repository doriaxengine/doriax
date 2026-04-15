//
// (c) 2026 Eduardo Doria.
//

#ifndef MESHPOLYGON_COMPONENT_H
#define MESHPOLYGON_COMPONENT_H

#include "math/Vector3.h"
#include "math/Vector4.h"
#include "component/PolygonComponent.h"

namespace doriax{

    struct DORIAX_API MeshPolygonComponent{
        unsigned int width = 0;
        unsigned int height = 0;

        bool automaticFlipY = true;
        bool flipY = false;

        std::vector<PolygonPoint> points;

        bool needUpdatePolygon = true;
    };

}

#endif //MESHPOLYGON_COMPONENT_H