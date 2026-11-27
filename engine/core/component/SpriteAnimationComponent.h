// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SPRITEANIMATION_COMPONENT_H
#define SPRITEANIMATION_COMPONENT_H

#include "Engine.h"
#include "util/HybridArray.h"

namespace doriax{

    struct DORIAX_API SpriteAnimationComponent{
        bool loop = true;

        unsigned int framesTimeSize = 0;
        HybridArray<int, MAX_SPRITE_FRAMES> framesTime;

        unsigned int framesSize = 0;
        HybridArray<int, MAX_SPRITE_FRAMES> frames;

        int frameIndex = 0;
        int frameTimeIndex = 0;

        float spriteFrameCount = 0; // ms into the current frame, fractions kept between updates
    };
    
}

#endif //SPRITEANIMATION_COMPONENT_H