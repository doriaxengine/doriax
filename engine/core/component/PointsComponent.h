//
// (c) 2026 Eduardo Doria.
//

#ifndef POINTS_COMPONENT_H
#define POINTS_COMPONENT_H

#include "util/SpriteFrameData.h"
#include "util/HybridArray.h"
#include "Engine.h"

namespace doriax{

    struct PointData{
        Vector3 position = Vector3(0.0, 0.0, 0.0);
        Vector4 color = Vector4(1.0, 1.0, 1.0, 1.0);
        float size = 1;
        float rotation = 0;
        Rect textureRect = Rect(0.0, 0.0, 1.0, 1.0);
        bool visible = true;
    };

    struct PointRenderData{
        Vector3 position;
        Vector4 color;
        float size;
        float rotation;
        Rect textureRect;
    };

    struct DORIAX_API PointsComponent{
        bool loaded = false;
        bool loadCalled = false;

        ExternalBuffer buffer;

        std::vector<PointData> points;
        std::vector<PointRenderData> renderPoints; //must be sorted

        unsigned int numFramesRect = 0;
        HybridArray<SpriteFrameData, MAX_SPRITE_FRAMES> framesRect;

        unsigned int maxPoints = 100;
        unsigned int numVisible = 0;

        ObjectRender render;
        std::shared_ptr<ShaderRender> shader;
        uint32_t shaderProperties = 0;
        int slotVSParams = -1;

        Texture texture;

        bool transparent = false;
        bool autoTransparency = true;

        bool hasTextureRect = false;

        bool needUpdate = true;
        bool needUpdateBuffer = false;
        bool needUpdateTexture = false;
        bool needReload = false;
    };
    
}

#endif //POINTS_COMPONENT_H