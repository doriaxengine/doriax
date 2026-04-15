//
// (c) 2026 Eduardo Doria.
//

#ifndef LINES_COMPONENT_H
#define LINES_COMPONENT_H

#include "buffer/Buffer.h"
#include "Engine.h"

namespace doriax{

    struct LineData{
        Vector3 pointA = Vector3(0,0,0);
        Vector4 colorA = Vector4(1,1,1,1);

        Vector3 pointB = Vector3(0,0,0);
        Vector4 colorB = Vector4(1,1,1,1);

        bool operator==(const LineData& other) const {
            return pointA == other.pointA &&
                colorA == other.colorA &&
                pointB == other.pointB &&
                colorB == other.colorB;
        }

        bool operator!=(const LineData& other) const {
            return !(*this == other);
        }
    };

    struct DORIAX_API LinesComponent{
        bool loaded = false;
        bool loadCalled = false;

        ExternalBuffer buffer;

        std::vector<LineData> lines;

        unsigned int maxLines = 100;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        uint32_t shaderProperties = 0;
        int slotVSParams = -1;

        bool needUpdateBuffer = false;
        bool needReload = false;
    };
    
}

#endif //LINES_COMPONENT_H