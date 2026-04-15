//
// (c) 2026 Eduardo Doria.
//

#include "Sprite.h"

#include "util/Color.h"
#include "subsystem/MeshSystem.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

Sprite::Sprite(Scene* scene): Mesh(scene){
    addComponent<SpriteComponent>();
    animation = NULL;
}

Sprite::Sprite(Scene* scene, Entity entity): Mesh(scene, entity){
    animation = NULL;
}

Sprite::~Sprite(){
    if (animation)
        delete animation;
}

Sprite::Sprite(const Sprite& rhs): Mesh(rhs){
    animation = rhs.animation;
}

Sprite& Sprite::operator=(const Sprite& rhs){
    Mesh::operator =(rhs);

    animation = rhs.animation;

    return *this;
}

bool Sprite::createSprite(){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();

    return scene->getSystem<MeshSystem>()->createOrUpdateSprite(spritecomp, mesh);
}

void Sprite::setSize(unsigned int width, unsigned int height){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    if ((spritecomp.width != width) || (spritecomp.height != height)){
        spritecomp.width = width;
        spritecomp.height = height;

        spritecomp.needUpdateSprite = true;
    }
}

void Sprite::setWidth(unsigned int width){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    if (spritecomp.width != width){
        spritecomp.width = width;

        spritecomp.needUpdateSprite = true;
    }
}

void Sprite::setHeight(unsigned int height){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    if (spritecomp.height != height){
        spritecomp.height = height;

        spritecomp.needUpdateSprite = true;
    }
}

unsigned int Sprite::getWidth() const{
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    return spritecomp.width;
}

unsigned int Sprite::getHeight() const{
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    return spritecomp.height;
}

void Sprite::setFlipY(bool flipY){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    spritecomp.automaticFlipY = false;
    if (spritecomp.flipY != flipY){
        spritecomp.flipY = flipY;

        spritecomp.needUpdateSprite = true;
    }
}

bool Sprite::isFlipY() const{
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    return spritecomp.flipY;
}

void Sprite::setTextureScaleFactor(float textureScaleFactor){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    
    if (spritecomp.textureScaleFactor != textureScaleFactor){
        spritecomp.textureScaleFactor = textureScaleFactor;
        spritecomp.needUpdateSprite = true;
    }
}

float Sprite::getTextureScaleFactor() const{
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    
    return spritecomp.textureScaleFactor;
}

void Sprite::setTextureRect(float x, float y, float width, float height){
    setTextureRect(Rect(x, y, width, height));
}

void Sprite::setTextureRect(Rect textureRect){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.submeshes[0].textureRect = textureRect;
}

Rect Sprite::getTextureRect() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.submeshes[0].textureRect;
}

void Sprite::setPivotPreset(PivotPreset pivotPreset){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    spritecomp.pivotPreset = pivotPreset;
    spritecomp.needUpdateSprite = true;
}

PivotPreset Sprite::getPivotPreset() const{
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    return spritecomp.pivotPreset;
}

void Sprite::addFrame(int id, const std::string& name, Rect rect){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    if (spritecomp.framesRect.validIndex(id)){
        spritecomp.framesRect[id] = {name, rect};
        if ((unsigned int)(id + 1) > spritecomp.numFramesRect){
            spritecomp.numFramesRect = id + 1;
        }
    }else{
        Log::error("Cannot set frame id %s less than 0 or greater than %i", name.c_str(), spritecomp.framesRect.size());
    }
}

void Sprite::addFrame(const std::string& name, float x, float y, float width, float height){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    int id = (int)spritecomp.numFramesRect;

    if (spritecomp.framesRect.validIndex(id)){
        addFrame(id, name, Rect(x, y, width, height));
    }else{
        Log::error("Cannot set frame %s. Sprite frames reached limit of %i", name.c_str(), spritecomp.framesRect.size());
    }
}

void Sprite::addFrame(float x, float y, float width, float height){
    addFrame("", x, y, width, height);
}

void Sprite::addFrame(Rect rect){
    addFrame(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight());
}

void Sprite::removeFrame(int id){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    if (id >= 0 && (unsigned int)id < spritecomp.numFramesRect){
        for (unsigned int i = id; i + 1 < spritecomp.numFramesRect; i++){
            spritecomp.framesRect[i] = spritecomp.framesRect[i + 1];
        }
        spritecomp.numFramesRect--;
        spritecomp.framesRect[spritecomp.numFramesRect] = SpriteFrameData{};
    }
}

void Sprite::removeFrame(const std::string& name){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();

    for (int id = (int)spritecomp.numFramesRect - 1; id >= 0; id--){
        if (spritecomp.framesRect[id].name == name){
            removeFrame(id);
        }
    }
}

void Sprite::setFrame(int id){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    if (id >= 0 && (unsigned int)id < spritecomp.numFramesRect){
        setTextureRect(spritecomp.framesRect[id].rect);
    }else{
        Log::error("Cannot use invalid sprite frame: %i", id);
    }
}

void Sprite::setFrame(const std::string& name){
    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    for (unsigned int id = 0; id < spritecomp.numFramesRect; id++) {
        if (spritecomp.framesRect[id].name == name){
            setTextureRect(spritecomp.framesRect[id].rect);
            return;
        }
    }
    Log::error("Cannot use nonexistent sprite frame: %s", name.c_str());
}

void Sprite::startAnimation(std::vector<int> frames, std::vector<int> framesTime, bool loop){
    if (!animation){
        animation = new SpriteAnimation(scene);
        animation->setTarget(entity);
    }else if (animation->isRunning()){
        stopAnimation();
    }

    if (frames.size() > 0 && framesTime.size() > 0){
        animation->setAnimation(frames, framesTime, loop);
        animation->start();
    }else{
        Log::error("Cannot start animation without frames or intervals");
    }
}

void Sprite::startAnimation(int startFrame, int endFrame, int interval, bool loop){
    if (!animation){
        animation = new SpriteAnimation(scene);
        animation->setTarget(entity);
    }else if (animation->isRunning()){
        stopAnimation();
    }

    animation->setAnimation(startFrame, endFrame, interval, loop);
    animation->start();
}

void Sprite::startAnimation(const std::string& name, int interval, bool loop){
    if (!animation){
        animation = new SpriteAnimation(scene);
        animation->setTarget(entity);
    }else if (animation->isRunning()){
        stopAnimation();
    }

    SpriteComponent& spritecomp = getComponent<SpriteComponent>();
    std::vector<int> frames;
    std::vector<int> framesTime;

    for (unsigned int id = 0; id < spritecomp.numFramesRect; id++){
        if (spritecomp.framesRect[id].name.find(name) != std::string::npos){
            frames.push_back(id);
            framesTime.push_back(interval);
        }
    }

    if (frames.size() > 0){
        animation->setAnimation(frames, framesTime, loop);
        animation->start();
    }else{
        Log::error("Cannot start animation '%s' without frames", name.c_str());
    }
}

void Sprite::pauseAnimation(){
    if (animation){
        animation->pause();
    }
}

void Sprite::resumeAnimation(){
    if (animation){
        animation->start();
    }
}

void Sprite::stopAnimation(){
    if (animation){
        animation->stop();
    }
}
