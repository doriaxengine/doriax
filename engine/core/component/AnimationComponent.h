//
// (c) 2026 Eduardo Doria.
//

#ifndef ANIMATION_COMPONENT_H
#define ANIMATION_COMPONENT_H

#include "Engine.h"

namespace doriax{

    struct ActionFrame{
        float startTime = 0;
        float duration = 0;
        Entity action = NULL_ENTITY;
        uint32_t track = 0; // Used for editor timeline organization
    };

    struct DORIAX_API AnimationComponent{
        std::vector<ActionFrame> actions;
        bool ownedActions = false;
        bool loop = false;

        std::string name;

        float duration = -1; // -1 is infinite
    };

    
}

#endif //ANIMATION_COMPONENT_H