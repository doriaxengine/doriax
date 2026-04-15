//
// (c) 2026 Eduardo Doria.
//

#include "SkyBox.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

SkyBox::SkyBox(Scene* scene): EntityHandle(scene){
    addComponent<SkyComponent>();
}

SkyBox::SkyBox(Scene* scene, Entity entity): EntityHandle(scene, entity){
}

SkyBox::~SkyBox(){
}

bool SkyBox::load(){
    SkyComponent& sky = getComponent<SkyComponent>();

    return scene->getSystem<RenderSystem>()->loadSky(entity, sky, PIP_DEFAULT | PIP_RTT);
}

void SkyBox::setTextures(const std::string& id,
                        TextureData texturePositiveX, TextureData textureNegativeX,
                        TextureData texturePositiveY, TextureData textureNegativeY,
                        TextureData texturePositiveZ, TextureData textureNegativeZ){

    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubeDatas(id, texturePositiveX, textureNegativeX, texturePositiveY, textureNegativeY, texturePositiveZ, textureNegativeZ);

    sky.needUpdateTexture = true;
}

void SkyBox::setTextures(const std::string& texturePositiveX, const std::string& textureNegativeX,
                        const std::string& texturePositiveY, const std::string& textureNegativeY,
                        const std::string& texturePositiveZ, const std::string& textureNegativeZ){
    
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePaths(texturePositiveX, textureNegativeX, texturePositiveY, textureNegativeY, texturePositiveZ, textureNegativeZ);

    sky.needUpdateTexture = true;
}

void SkyBox::setTexture(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubeMap(texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setTexturePositiveX(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePath(0, texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setTextureNegativeX(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePath(1, texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setTexturePositiveY(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePath(2, texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setTextureNegativeY(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePath(3, texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setTexturePositiveZ(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePath(4, texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setTextureNegativeZ(const std::string& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.texture.setCubePath(5, texture);

    sky.needUpdateTexture = true;
}

void SkyBox::setColor(Vector4 color){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.color = color;
}

void SkyBox::setColor(const float r, const float g, const float b){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.color = Vector4(r, g, b, sky.color.w);
}

void SkyBox::setColor(const float r, const float g, const float b, const float a){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.color = Vector4(r, g, b, a);
}

void SkyBox::setAlpha(const float alpha){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.color = Vector4(sky.color.x, sky.color.y, sky.color.z, alpha);
}

Vector4 SkyBox::getColor() const{
    SkyComponent& sky = getComponent<SkyComponent>();

    return sky.color;
}

float SkyBox::getAlpha() const{
    SkyComponent& sky = getComponent<SkyComponent>();

    return sky.color.w;
}

void SkyBox::setRotation(float angle){
    SkyComponent& sky = getComponent<SkyComponent>();

    sky.rotation = angle;
    sky.needUpdateSky = true;
}

float SkyBox::getRotation() const{
    SkyComponent& sky = getComponent<SkyComponent>();

    return sky.rotation;
}