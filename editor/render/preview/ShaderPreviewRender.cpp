// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ShaderPreviewRender.h"

#include "Engine.h"
#include "util/ProjectUtils.h"

using namespace doriax;

editor::ShaderPreviewRender::ShaderPreviewRender(unsigned int size){
    scene = new Scene(EntityPool::System);
    camera = new Camera(scene);
    light = new Light(scene);
    sky = new SkyBox(scene);
    shape = new Shape(scene);
    shapeType = ShapeType::SPHERE;

    scene->setBackgroundColor(0.12, 0.12, 0.13, 1.0);
    scene->setCamera(camera);

    // the sky lights the shape without being drawn
    SkyComponent& skyComp = sky->getComponent<SkyComponent>();
    ProjectUtils::setDefaultSkyTexture(skyComp.texture);
    skyComp.visible = false;

    light->setDirection(-0.4, -0.6, -0.5);
    light->setIntensity(2.0);
    light->setType(LightType::DIRECTIONAL);

    camera->setPosition(0, 1.1, 2.9);
    camera->setTarget(0, 0, 0);
    camera->setType(CameraType::CAMERA_PERSPECTIVE);
    camera->setFramebufferSize(size, size);
    camera->setRenderToTexture(true);

    createShape();
}

editor::ShaderPreviewRender::~ShaderPreviewRender(){
    delete camera;
    delete light;
    delete sky;
    delete shape;
    delete scene;
}

void editor::ShaderPreviewRender::createShape(){
    if (shapeType == ShapeType::CUBE)
        shape->createBox(1.3, 1.3, 1.3);
    else if (shapeType == ShapeType::PLANE)
        shape->createPlane(2.0, 2.0);
    else
        shape->createSphere(1.0);

    Material material;
    material.shader = shader;
    shape->setMaterial(material);
}

void editor::ShaderPreviewRender::setShader(const std::string& shader){
    if (this->shader == shader)
        return;
    this->shader = shader;
    Material material = shape->getMaterial();
    material.shader = shader;
    shape->setMaterial(material);
    reload();
}

const std::string& editor::ShaderPreviewRender::getShader() const{
    return shader;
}

void editor::ShaderPreviewRender::setShape(ShapeType type){
    if (shapeType == type)
        return;
    shapeType = type;
    createShape();
}

editor::ShaderPreviewRender::ShapeType editor::ShaderPreviewRender::getShape() const{
    return shapeType;
}

void editor::ShaderPreviewRender::reload(){
    shape->getComponent<MeshComponent>().needReload = true;
}

void editor::ShaderPreviewRender::render(){
    Engine::executeSceneOnce(scene);
}

Texture editor::ShaderPreviewRender::getTexture(){
    return Texture(camera->getFramebuffer());
}

Scene* editor::ShaderPreviewRender::getScene(){
    return scene;
}
