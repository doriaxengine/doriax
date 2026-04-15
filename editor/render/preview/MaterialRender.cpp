#include "MaterialRender.h"

#include "Configs.h"

using namespace doriax;

editor::MaterialRender::MaterialRender(){
    scene = new Scene(EntityPool::System);
    camera = new Camera(scene);
    light = new Light(scene);
    sphere = new Shape(scene);

    scene->setBackgroundColor(0.0, 0.0, 0.0, 0.0);
    scene->setCamera(camera);

    sphere->createSphere(1.0);

    light->setDirection(-0.4, -0.5, -0.5);
    light->setIntensity(6.0);
    light->setType(LightType::DIRECTIONAL);

    camera->setPosition(0, 0, 3);
    camera->setTarget(0, 0, 0);
    camera->setType(CameraType::CAMERA_PERSPECTIVE);
    camera->setFramebufferSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    camera->setRenderToTexture(true);
}

editor::MaterialRender::~MaterialRender(){
    delete camera;
    delete light;
    delete sphere;
    delete scene;
}

void editor::MaterialRender::applyMaterial(const Material& material){
    sphere->setMaterial(material);
}

const Material editor::MaterialRender::getMaterial(){
    return sphere->getMaterial();
}

Framebuffer* editor::MaterialRender::getFramebuffer(){
    return camera->getFramebuffer();
}

Texture editor::MaterialRender::getTexture(){
    return Texture(getFramebuffer());
}

Scene* editor::MaterialRender::getScene(){
    return scene;
}

Object* editor::MaterialRender::getObject(){
    return sphere;
}