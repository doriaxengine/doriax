// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "SpriteAnimation.h"

using namespace doriax;

SpriteAnimation::SpriteAnimation(Scene* scene): Action(scene){
    addComponent<SpriteAnimationComponent>();
}

SpriteAnimation::SpriteAnimation(Scene* scene, Entity entity): Action(scene, entity){

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

    if (startFrame >= 0 && endFrame >= startFrame) {
        // 64-bit, endFrame can be INT_MAX
        long long count = (long long)endFrame - startFrame + 1;
        // a fixed size array keeps its first frames, without building the rest
        if (!decltype(SpriteAnimationComponent::frames)::validIndex((int)(count - 1))){
            Log::warn("Sprite animation frames truncated from %lld to %d", count, MAX_SPRITE_FRAMES);
            count = MAX_SPRITE_FRAMES;
        }

        for (long long i = 0; i < count; i++){
            frames.push_back(startFrame + (int)i);
            framesTime.push_back(interval);
        }
    }

    setAnimation(std::move(frames), std::move(framesTime), loop);
}