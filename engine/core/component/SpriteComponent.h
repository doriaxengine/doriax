//
// (c) 2026 Eduardo Doria.
//

#ifndef SPRITE_COMPONENT_H
#define SPRITE_COMPONENT_H

#include "util/SpriteFrameData.h"
#include "util/HybridArray.h"
#include "Engine.h"

namespace doriax{

    enum class PivotPreset{
        CENTER,
        TOP_CENTER,
        BOTTOM_CENTER,
        LEFT_CENTER,
        RIGHT_CENTER,
        TOP_LEFT,
        BOTTOM_LEFT,
        TOP_RIGHT,
        BOTTOM_RIGHT
    };

    struct DORIAX_API SpriteComponent{
        unsigned int width = 0;
        unsigned int height = 0;

        bool automaticFlipY = true;
        bool flipY = false;

        float textureScaleFactor = 0.0;

        unsigned int numFramesRect = 0;
        HybridArray<SpriteFrameData, MAX_SPRITE_FRAMES> framesRect;

        PivotPreset pivotPreset = PivotPreset::BOTTOM_LEFT;

        bool needUpdateSprite = true;
    };
    
}

#endif //SPRITE_COMPONENT_H