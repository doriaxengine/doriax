#include "ModelRender.h"

#include "Configs.h"

using namespace doriax;

editor::ModelRender::ModelRender(){
    scene = nullptr;
    camera = nullptr;
    light = nullptr;
    model = nullptr;
}

editor::ModelRender::~ModelRender(){
    clearScene();
}

void editor::ModelRender::clearScene(){
    delete camera;
    delete light;
    delete model;
    delete scene;

    camera = nullptr;
    light = nullptr;
    model = nullptr;
    scene = nullptr;
}

void editor::ModelRender::clearAndInitScene(){
    clearScene();

    scene = new Scene(EntityPool::System);
    camera = new Camera(scene);
    light = new Light(scene);
    model = new Model(scene);

    scene->setBackgroundColor(0.0, 0.0, 0.0, 0.0);
    scene->setCamera(camera);

    light->setDirection(-0.4, -0.5, -0.5);
    light->setIntensity(6.0);
    light->setType(LightType::DIRECTIONAL);

    camera->setTarget(0, 0, 0);
    camera->setType(CameraType::CAMERA_PERSPECTIVE);
    camera->setFramebufferSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    camera->setRenderToTexture(true);
}

bool editor::ModelRender::loadModel(const std::string& filename){
    // Completely recreate the scene to wipe out any child entities from previously loaded models
    clearAndInitScene();

    return model->loadModel(filename);
}

void editor::ModelRender::positionCameraForModel(){
    // Merge local aabb from the model and all its child mesh entities
    AABB aabb;
    size_t lastIndex = scene->findBranchLastIndex(model->getEntity());
    auto transforms = scene->getComponentArray<Transform>();
    size_t startIndex = transforms->getIndex(model->getEntity());

    for (size_t i = startIndex; i <= lastIndex; i++){
        Entity entity = transforms->getEntity(i);
        
        Object obj(scene, entity);
        obj.updateTransform();

        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<MeshComponent>())){
            MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            if (!mesh.aabb.isNull() && !mesh.aabb.isInfinite()){
                Transform& curTransform = scene->getComponent<Transform>(entity);
                aabb.merge(curTransform.modelMatrix * mesh.aabb);
            }
        }
    }

    Vector3 center = aabb.getCenter();
    Vector3 size = aabb.getSize();

    float maxDimension = std::max({size.x, size.y, size.z});
    
    // Avoid issues if the model has a zero-size AABB
    if (maxDimension <= 0.001f) {
        maxDimension = 1.0f;
    }

    // A better distance formula based on FOV (assuming 45 degrees, half FOV = 22.5)
    // distance = (maxDimension / 2) / tan(22.5°)
    float distance = (maxDimension / 2.0f) / tan(22.5f * M_PI / 180.0f);
    distance = std::max(distance, maxDimension * 1.5f);

    // Position camera at an angle for better 3D visualization
    Vector3 cameraOffset = Vector3(0.5f, 0.3f, 0.6f);
    cameraOffset.normalize();
    cameraOffset = cameraOffset * distance;

    Vector3 cameraPosition = center + cameraOffset;

    camera->setPosition(cameraPosition.x, cameraPosition.y, cameraPosition.z);
    camera->setTarget(center.x, center.y, center.z);
}

void editor::ModelRender::fixDarkMaterials(){
    size_t lastIndex = scene->findBranchLastIndex(model->getEntity());
    auto transforms = scene->getComponentArray<Transform>();
    size_t startIndex = transforms->getIndex(model->getEntity());

    for (size_t i = startIndex; i <= lastIndex; i++){
        Entity entity = transforms->getEntity(i);
        Signature signature = scene->getSignature(entity);
        if (signature.test(scene->getComponentId<MeshComponent>())){
            MeshComponent& mesh = scene->getComponent<MeshComponent>(entity);
            for (unsigned int s = 0; s < mesh.numSubmeshes; s++){
                Material& mat = mesh.submeshes[s].material;
                if (mat.baseColorTexture.empty()){
                    float luminance = 0.2126f * mat.baseColorFactor.x + 0.7152f * mat.baseColorFactor.y + 0.0722f * mat.baseColorFactor.z;
                    if (luminance < 0.1f){
                        mat.baseColorFactor = Vector4(0.4f, 0.4f, 0.4f, mat.baseColorFactor.w);
                    }
                    mat.metallicFactor = 0.0f;
                    mat.roughnessFactor = 0.8f;
                }
            }
        }
    }
}

Framebuffer* editor::ModelRender::getFramebuffer(){
    return camera->getFramebuffer();
}

Texture editor::ModelRender::getTexture(){
    return Texture(getFramebuffer());
}

Scene* editor::ModelRender::getScene(){
    return scene;
}

Object* editor::ModelRender::getObject(){
    return model;
}
