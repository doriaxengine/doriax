//
// (c) 2026 Eduardo Doria.
//

#include "SpriteAnimation.h"

using namespace doriax;

SpriteAnimation::SpriteAnimation(Scene* scene): Action(scene){
    addComponent<SpriteAnimationComponent>();
}

void SpriteAnimation::setAnimation(std::vector<int> frames, std::vector<int> framesTime, bool loop){
    if (frames.size() <= 0 || framesTime.size() <= 0) {
        Log::error("Cannot set animation without frames or intervals");
        return;
    }

    SpriteAnimationComponent& spriteanim = getComponent<SpriteAnimationComponent>();

    spriteanim.framesTimeSize = framesTime.size();
    for (int i = 0; i < (int)framesTime.size(); i++){
        if (!spriteanim.framesTime.validIndex(i)){
            Log::warn("Sprite animation framesTime truncated from %zu to %d", framesTime.size(), i);
            spriteanim.framesTimeSize = i;
            break;
        }
        spriteanim.framesTime[i] = framesTime[i];
    }

    spriteanim.framesSize = frames.size();
    for (int i = 0; i < (int)frames.size(); i++){
        if (!spriteanim.frames.validIndex(i)){
            Log::warn("Sprite animation frames truncated from %zu to %d", frames.size(), i);
            spriteanim.framesSize = i;
            break;
        }
        spriteanim.frames[i] = frames[i];
    }

    spriteanim.loop = loop;
    spriteanim.frameIndex = 0;
    spriteanim.frameTimeIndex = 0;
    spriteanim.spriteFrameCount = 0;
}

void SpriteAnimation::setAnimation(int startFrame, int endFrame, int interval, bool loop){
    std::vector<int> frames;
    std::vector<int> framesTime;

    if (startFrame > 0 || endFrame > 0) {
        for (int i=startFrame; i<=endFrame; i++){
            frames.push_back(i);
            framesTime.push_back(interval);
        }
    }

    setAnimation(frames, framesTime, loop);
}