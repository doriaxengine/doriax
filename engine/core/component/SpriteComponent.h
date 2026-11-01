// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

        // Frame chosen before the texture size was known, in pixels
        Rect pendingFrameRect;
        bool needUpdateFrameRect = false;

        bool needUpdateSprite = true;
        bool needUpdateBatch = false; // set after changing its MeshComponent directly
    };
    
}

#endif //SPRITE_COMPONENT_H