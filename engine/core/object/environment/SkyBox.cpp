// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

    auto renderSystem = scene->getSystem<RenderSystem>();
    return renderSystem->loadSky(entity, sky, renderSystem->getScenePipelines());
}

void SkyBox::setTextures(const std::string& id,
                        TextureData texturePositiveX, TextureData textureNegativeX,
                        TextureData texturePositiveY, TextureData textureNegativeY,
                        TextureData texturePositiveZ, TextureData textureNegativeZ){

    SkyComponent& sky = getComponent<SkyComponent>();

    // front, back, left, right, up, down
    sky.texture.setCubeDatas(id, texturePositiveZ, textureNegativeZ, textureNegativeX, texturePositiveX, texturePositiveY, textureNegativeY);

    sky.needUpdateTexture = true;
}

void SkyBox::setTextures(const std::string& texturePositiveX, const std::string& textureNegativeX,
                        const std::string& texturePositiveY, const std::string& textureNegativeY,
                        const std::string& texturePositiveZ, const std::string& textureNegativeZ){
    
    SkyComponent& sky = getComponent<SkyComponent>();

    // front, back, left, right, up, down
    sky.texture.setCubePaths(texturePositiveZ, textureNegativeZ, textureNegativeX, texturePositiveX, texturePositiveY, textureNegativeY);

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

void SkyBox::setShader(const std::string& path){
    SkyComponent& sky = getComponent<SkyComponent>();

    if (sky.shader != path){
        sky.shader = path;

        sky.needReload = true;
    }
}

std::string SkyBox::getShader() const{
    SkyComponent& sky = getComponent<SkyComponent>();

    return sky.shader;
}

void SkyBox::setShaderUniform(const std::string& name, const Vector4& value){
    SkyComponent& sky = getComponent<SkyComponent>();

    ShaderUniforms::set(sky.shaderUniforms, name, value);
    sky.needUpdateShaderUniforms = true;
}

void SkyBox::setShaderUniform(const std::string& name, const Vector3& value){
    setShaderUniform(name, Vector4(value.x, value.y, value.z, 0.0f));
}

void SkyBox::setShaderUniform(const std::string& name, const Vector2& value){
    setShaderUniform(name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void SkyBox::setShaderUniform(const std::string& name, float value){
    setShaderUniform(name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 SkyBox::getShaderUniform(const std::string& name) const{
    SkyComponent& sky = getComponent<SkyComponent>();

    return ShaderUniforms::get(sky.shaderUniforms, name);
}

bool SkyBox::removeShaderUniform(const std::string& name){
    SkyComponent& sky = getComponent<SkyComponent>();

    if (!ShaderUniforms::remove(sky.shaderUniforms, name))
        return false;

    sky.needUpdateShaderUniforms = true;
    return true;
}

void SkyBox::setShaderTexture(const std::string& name, const Texture& texture){
    SkyComponent& sky = getComponent<SkyComponent>();

    ShaderTextures::set(sky.shaderTextures, name, texture);
    sky.needReload = true;
}

Texture SkyBox::getShaderTexture(const std::string& name) const{
    SkyComponent& sky = getComponent<SkyComponent>();

    return ShaderTextures::get(sky.shaderTextures, name);
}