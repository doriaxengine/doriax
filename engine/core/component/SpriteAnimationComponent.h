//
// (c) 2026 Eduardo Doria.
//

#ifndef SPRITEANIMATION_COMPONENT_H
#define SPRITEANIMATION_COMPONENT_H

#include "Engine.h"
#include "util/HybridArray.h"

namespace doriax{

    struct DORIAX_API SpriteAnimationComponent{
        std::string name;

        bool loop = true;

        unsigned int framesTimeSize = 0;
        HybridArray<int, MAX_SPRITE_FRAMES> framesTime;

        unsigned int framesSize = 0;
        HybridArray<int, MAX_SPRITE_FRAMES> frames;

        int frameIndex = 0;
        int frameTimeIndex = 0;

        unsigned int spriteFrameCount = 0;
    };
    
}

#endif //SPRITEANIMATION_COMPONENT_H