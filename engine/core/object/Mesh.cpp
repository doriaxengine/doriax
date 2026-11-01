// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Mesh.h"
#include "render/ObjectRender.h"
#include "util/Color.h"
#include "component/MirrorComponent.h"
#include "component/ReflectionProbeComponent.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/MeshSystem.h"

#include <cfloat>

using namespace doriax;

// Read while a model has no geometry, on the heap as a game build's mesh is large
static const MeshComponent& getDefaultMesh(){
    static std::unique_ptr<MeshComponent> mesh = std::make_unique<MeshComponent>();
    return *mesh;
}

// In the space of an entity above, from the local poses between them, which are current before
// the transforms update. False when the ancestor is not above it.
static bool getPoseMatrix(Scene* scene, Entity entity, Entity ancestor, Matrix4& matrix){
    matrix = Matrix4();
    while (entity != ancestor){
        Transform* transform = (entity != NULL_ENTITY) ? scene->findComponent<Transform>(entity) : nullptr;
        if (!transform){
            return false;
        }
        matrix = Matrix4::translateMatrix(transform->position) * transform->rotation.getRotationMatrix() * Matrix4::scaleMatrix(transform->scale) * matrix;
        entity = transform->parent;
    }
    return true;
}

Mesh::Mesh(Scene* scene): Object(scene){
    addComponent<MeshComponent>();
}

Mesh::Mesh(Scene* scene, Entity entity): Object(scene, entity){
}

Mesh::~Mesh(){
}

// A model with its geometry on node children has no mesh of its own
ModelComponent* Mesh::findPartsModel() const{
    if (scene->findComponent<MeshComponent>(entity)){
        return nullptr;
    }
    return scene->findComponent<ModelComponent>(entity);
}

// In the model's space. A part moved out of the model comes back through the scene, which a
// zero scale on the model leaves no way back from.
AABB Mesh::getPartsAABB(const ModelComponent& model, AABB MeshComponent::*bounds) const{
    AABB aabb;
    for (const auto& node : model.meshNodesMapping){
        MeshComponent* mesh = scene->findComponent<MeshComponent>(node.second);
        if (!mesh){
            continue;
        }

        Matrix4 pose, modelPose;
        if (getPoseMatrix(scene, node.second, entity, pose)){
            aabb.merge(pose * (mesh->*bounds));
        }else if (getPoseMatrix(scene, entity, NULL_ENTITY, modelPose) && std::fabs(modelPose.determinant()) > FLT_MIN){
            getPoseMatrix(scene, node.second, NULL_ENTITY, pose);
            aabb.merge(modelPose.inverse() * pose * (mesh->*bounds));
        }
    }
    return aabb;
}

std::vector<MeshComponent*> Mesh::findMeshes() const{
    std::vector<MeshComponent*> meshes;
    if (ModelComponent* model = findPartsModel()){
        for (const auto& run : scene->getSystem<MeshSystem>()->getSubmeshRuns(entity, *model)){
            meshes.push_back(run.first);
        }
    }else{
        meshes.push_back(&getComponent<MeshComponent>());
    }
    return meshes;
}

// Until a model has geometry, a mesh of its own keeps the values for the meshes its load builds
std::vector<MeshComponent*> Mesh::getMeshes(){
    std::vector<MeshComponent*> meshes = findMeshes();
    if (meshes.empty()){
        addComponent<MeshComponent>();
        meshes.push_back(&getComponent<MeshComponent>());
    }
    // the setters change them
    updateSpriteBatch();
    return meshes;
}

void Mesh::updateSpriteBatch(){
    if (SpriteComponent* sprite = scene->findComponent<SpriteComponent>(entity)){
        sprite->needUpdateBatch = true;
    }
}

const MeshComponent& Mesh::getMesh() const{
    if (ModelComponent* model = findPartsModel()){
        auto runs = scene->getSystem<MeshSystem>()->getSubmeshRuns(entity, *model);
        return runs.empty() ? getDefaultMesh() : *runs[0].first;
    }
    return getComponent<MeshComponent>();
}

// Numbered across the parts as in the merged model, null past the last one
MeshComponent* Mesh::findSubmesh(unsigned int& submesh) const{
    if (ModelComponent* model = findPartsModel()){
        for (const auto& [mesh, base] : scene->getSystem<MeshSystem>()->getSubmeshRuns(entity, *model)){
            if (submesh >= base && submesh < base + mesh->numSubmeshes){
                submesh -= base;
                return mesh;
            }
        }
        return nullptr;
    }
    return &getComponent<MeshComponent>();
}

const Submesh& Mesh::getSubmesh(unsigned int submesh) const{
    MeshComponent* mesh = findSubmesh(submesh);
    return mesh ? mesh->submeshes[submesh] : getDefaultMesh().submeshes[0];
}

bool Mesh::load(){
    if (ModelComponent* model = findPartsModel()){
        bool loaded = true;
        for (const auto& node : model->meshNodesMapping){
            if (scene->findComponent<MeshComponent>(node.second)){
                loaded = Mesh(scene, node.second).load() && loaded;
            }
        }
        return loaded;
    }

    MeshComponent& mesh = getComponent<MeshComponent>();

    TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
    InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);

    auto renderSystem = scene->getSystem<RenderSystem>();
    return renderSystem->loadMesh(entity, mesh, renderSystem->getScenePipelines(), instmesh, terrain);
}

void Mesh::setTexture(const std::string& path){
    for (MeshComponent* mesh : getMeshes()){
        mesh->submeshes[0].material.baseColorTexture.setPath(path);

        mesh->submeshes[0].needUpdateTexture = true;
    }
}

void Mesh::setTexture(const std::string& id, TextureData data){
    for (MeshComponent* mesh : getMeshes()){
        mesh->submeshes[0].material.baseColorTexture.setData(id, data);

        mesh->submeshes[0].needUpdateTexture = true;
    }
}

void Mesh::setTexture(Framebuffer* framebuffer){
    for (MeshComponent* mesh : getMeshes()){
        mesh->submeshes[0].material.baseColorTexture.setFramebuffer(framebuffer);

        mesh->submeshes[0].needUpdateTexture = true;
    }
}

void Mesh::setColor(Vector4 color){
    for (MeshComponent* mesh : getMeshes()){
        mesh->submeshes[0].material.baseColorFactor = Color::sRGBToLinear(color);
    }
}

void Mesh::setColor(const float red, const float green, const float blue, const float alpha){
    setColor(Vector4(red, green, blue, alpha));
}

void Mesh::setColor(const float red, const float green, const float blue){
    setColor(Vector4(red, green, blue, getColor().w));
}

void Mesh::setAlpha(const float alpha){
    Vector4 color = getColor();
    setColor(Vector4(color.x, color.y, color.z, alpha));
}

Vector4 Mesh::getColor() const{
    return Color::linearTosRGB(getSubmesh(0).material.baseColorFactor);
}

float Mesh::getAlpha() const{
    return getColor().w;
}

void Mesh::setMaterial(const Material& material){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->submeshes[0].material != material){
            mesh->submeshes[0].material = material;
            mesh->submeshes[0].needUpdateTexture = true;
        }
    }
}

Material Mesh::getMaterial() const{
    return getMaterial(0);
}

void Mesh::setMaterial(unsigned int submesh, const Material& material){
    MeshComponent* mesh = findSubmesh(submesh);

    if (mesh && mesh->submeshes[submesh].material != material){
        mesh->submeshes[submesh].material = material;
        mesh->submeshes[submesh].needUpdateTexture = true;
        updateSpriteBatch();
    }
}

Material Mesh::getMaterial(unsigned int submesh) const{
    return getSubmesh(submesh).material;
}

void Mesh::setPrimitiveType(PrimitiveType primitiveType){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->submeshes[0].primitiveType != primitiveType){
            mesh->submeshes[0].primitiveType = primitiveType;

            mesh->needReload = true;
        }
    }
}

PrimitiveType Mesh::getPrimitiveType() const{
    return getPrimitiveType(0);
}

void Mesh::setPrimitiveType(unsigned int submesh, PrimitiveType primitiveType){
    MeshComponent* mesh = findSubmesh(submesh);

    if (mesh && mesh->submeshes[submesh].primitiveType != primitiveType){
        mesh->submeshes[submesh].primitiveType = primitiveType;

        mesh->needReload = true;
        updateSpriteBatch();
    }
}

PrimitiveType Mesh::getPrimitiveType(unsigned int submesh) const{
    return getSubmesh(submesh).primitiveType;
}

void Mesh::setFaceCulling(bool faceCulling){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->submeshes[0].faceCulling != faceCulling){
            mesh->submeshes[0].faceCulling = faceCulling;

            mesh->needReload = true;
        }
    }
}

bool Mesh::isFaceCulling() const{
    return isFaceCulling(0);
}

void Mesh::setFaceCulling(unsigned int submesh, bool faceCulling){
    MeshComponent* mesh = findSubmesh(submesh);

    if (mesh && mesh->submeshes[submesh].faceCulling != faceCulling){
        mesh->submeshes[submesh].faceCulling = faceCulling;

        mesh->needReload = true;
        updateSpriteBatch();
    }
}

bool Mesh::isFaceCulling(unsigned int submesh) const{
    return getSubmesh(submesh).faceCulling;
}


void Mesh::setCastShadowsWithTexture(bool castShadowsWithTexture){
    for (MeshComponent* mesh : getMeshes()){
        mesh->submeshes[0].textureShadow = castShadowsWithTexture;

        mesh->needReload = true;
    }
}

bool Mesh::isCastShadowsWithTexture() const{
    return isCastShadowsWithTexture(0);
}

void Mesh::setCastShadowsWithTexture(unsigned int submesh, bool castShadowsWithTexture){
    if (MeshComponent* mesh = findSubmesh(submesh)){
        mesh->submeshes[submesh].textureShadow = castShadowsWithTexture;

        mesh->needReload = true;
        updateSpriteBatch();
    }
}

bool Mesh::isCastShadowsWithTexture(unsigned int submesh) const{
    return getSubmesh(submesh).textureShadow;
}

void Mesh::setCullingMode(CullingMode cullingMode){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->cullingMode != cullingMode){
            mesh->cullingMode = cullingMode;

            mesh->needReload = true;
        }
    }
}

CullingMode Mesh::getCullingMode() const{
    return getMesh().cullingMode;
}

void Mesh::setWindingOrder(WindingOrder windingOrder){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->windingOrder != windingOrder){
            mesh->windingOrder = windingOrder;

            mesh->needReload = true;
        }
    }
}

WindingOrder Mesh::getWindingOrder() const{
    return getMesh().windingOrder;
}


AABB Mesh::getAABB() const{
    if (ModelComponent* model = findPartsModel()){
        return getPartsAABB(*model, &MeshComponent::aabb);
    }

    return getComponent<MeshComponent>().aabb;
}

AABB Mesh::getVerticesAABB() const{
    if (ModelComponent* model = findPartsModel()){
        return getPartsAABB(*model, &MeshComponent::verticesAABB);
    }

    return getComponent<MeshComponent>().verticesAABB;
}

AABB Mesh::getWorldAABB() const{
    AABB aabb;
    for (MeshComponent* mesh : findMeshes()){
        aabb.merge(mesh->worldAABB);
    }

    return aabb;
}

unsigned int Mesh::getNumSubmeshes() const{
    unsigned int numSubmeshes = 0;
    for (MeshComponent* mesh : findMeshes()){
        numSubmeshes += mesh->numSubmeshes;
    }

    return numSubmeshes;
}

void Mesh::setReceiveLights(bool receiveLights){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->receiveLights != receiveLights){
            mesh->receiveLights = receiveLights;

            mesh->needReload = true;
        }
    }
}

bool Mesh::isReceiveLights() const{
    return getMesh().receiveLights;
}

void Mesh::setReceiveIBL(bool receiveIBL){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->receiveIBL != receiveIBL){
            mesh->receiveIBL = receiveIBL;

            mesh->needReload = true;
        }
    }
}

bool Mesh::isReceiveIBL() const{
    return getMesh().receiveIBL;
}

void Mesh::setCastShadows(bool castShadows){
    for (MeshComponent* mesh : getMeshes()){
        mesh->castShadows = castShadows;
    }
}

bool Mesh::isCastShadows() const{
    return getMesh().castShadows;
}

void Mesh::setLodEnabled(bool lodEnabled){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->lodEnabled != lodEnabled){
            mesh->lodEnabled = lodEnabled;

            mesh->needReload = true;
        }
    }
}

bool Mesh::isLodEnabled() const{
    return getMesh().lodEnabled;
}

void Mesh::setLodBias(float lodBias){
    for (MeshComponent* mesh : getMeshes()){
        mesh->lodBias = lodBias;
    }
}

float Mesh::getLodBias() const{
    return getMesh().lodBias;
}

void Mesh::setReceiveShadows(bool receiveShadows){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->receiveShadows != receiveShadows){
            mesh->receiveShadows = receiveShadows;

            mesh->needReload = true;
        }
    }
}

bool Mesh::isReceiveShadows() const{
    return getMesh().receiveShadows;
}

void Mesh::setRenderInReflectionProbes(bool renderInReflectionProbes){
    bool changed = false;
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->renderInReflectionProbes != renderInReflectionProbes){
            mesh->renderInReflectionProbes = renderInReflectionProbes;
            changed = true;
        }
    }

    if (changed){
        // baked cubemaps keep the old content until the probes capture again
        auto probes = scene->getComponentArray<ReflectionProbeComponent>();
        for (int i = 0; i < probes->size(); i++){
            ReflectionProbeComponent& probe = probes->getComponentFromIndex(i);
            probe.needUpdate = true;
            probe.captureRevision++;
        }
    }
}

bool Mesh::isRenderInReflectionProbes() const{
    return getMesh().renderInReflectionProbes;
}

void Mesh::setShadowsBillboard(bool shadowsBillboard){
    for (MeshComponent* mesh : getMeshes()){
        mesh->shadowsBillboard = shadowsBillboard;
    }
}

bool Mesh::isShadowsBillboard() const{
    return getMesh().shadowsBillboard;
}

void Mesh::setTransparent(bool transparent){
    for (MeshComponent* mesh : getMeshes()){
        mesh->transparent = transparent;
    }
}

bool Mesh::isTransparent() const{
    return getMesh().transparent;
}

void Mesh::setAutoTransparency(bool autoTransparency){
    for (MeshComponent* mesh : getMeshes()){
        mesh->autoTransparency = autoTransparency;
    }
}

bool Mesh::isAutoTransparency() const{
    return getMesh().autoTransparency;
}

void Mesh::setCustomShader(const std::string& path){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->customShader != path){
            mesh->customShader = path;

            mesh->needReload = true;
        }
    }
}

std::string Mesh::getCustomShader() const{
    return getMesh().customShader;
}

void Mesh::setCustomDepthShader(const std::string& path){
    for (MeshComponent* mesh : getMeshes()){
        if (mesh->customDepthShader != path){
            mesh->customDepthShader = path;

            mesh->needReload = true;
        }
    }
}

std::string Mesh::getCustomDepthShader() const{
    return getMesh().customDepthShader;
}

void Mesh::setShaderUniform(const std::string& name, const Vector4& value){
    for (MeshComponent* mesh : getMeshes()){
        if (ShaderUniforms::set(mesh->shaderUniforms, name, value))
            mesh->needUpdateShaderUniforms = true;
    }
}

void Mesh::setShaderUniform(const std::string& name, const Vector3& value){
    setShaderUniform(name, Vector4(value.x, value.y, value.z, 0.0f));
}

void Mesh::setShaderUniform(const std::string& name, const Vector2& value){
    setShaderUniform(name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void Mesh::setShaderUniform(const std::string& name, float value){
    setShaderUniform(name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 Mesh::getShaderUniform(const std::string& name) const{
    return ShaderUniforms::get(getMesh().shaderUniforms, name);
}

bool Mesh::removeShaderUniform(const std::string& name){
    bool removed = false;
    for (MeshComponent* mesh : getMeshes()){
        if (ShaderUniforms::remove(mesh->shaderUniforms, name)){
            mesh->needUpdateShaderUniforms = true;
            removed = true;
        }
    }

    return removed;
}

void Mesh::setAsMirror(){
    // default normal +Z matches a Wall surface; flip for differently-oriented meshes
    setAsMirror(Vector3(0, 0, 1));
}

void Mesh::setAsMirror(Vector3 normal){
    // turn this (flat) mesh into a planar reflection surface. The engine creates and
    // drives the reflection camera internally and feeds it to the base texture.
    MirrorComponent* mirror = scene->findComponent<MirrorComponent>(entity);
    if (mirror){
        mirror->normal = normal;
    }else{
        MirrorComponent mirrorComp;
        mirrorComp.normal = normal;
        addComponent<MirrorComponent>(mirrorComp);
    }
}

void Mesh::removeMirror(){
    if (scene->findComponent<MirrorComponent>(entity)){
        removeComponent<MirrorComponent>();
    }
}

bool Mesh::isMirror() const{
    return scene->findComponent<MirrorComponent>(entity) != NULL;
}


void Mesh::createInstancedMesh(){
    scene->getSystem<MeshSystem>()->createInstancedMesh(entity);
}

void Mesh::removeInstancedMesh(){
    scene->getSystem<MeshSystem>()->removeInstancedMesh(entity);
}

bool Mesh::hasInstancedMesh() const{
    return scene->getSystem<MeshSystem>()->hasInstancedMesh(entity);
}

void Mesh::setInstancedBillboard(bool billboard, bool cylindrical){
    createInstancedMesh();
    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    instmesh.instancedBillboard = billboard;
    instmesh.instancedCylindricalBillboard = cylindrical;

    instmesh.needUpdateInstances = true;
}

void Mesh::setInstancedBillboard(bool billboard){
    createInstancedMesh();
    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    if (instmesh.instancedBillboard != billboard){
        instmesh.instancedBillboard = billboard;

        instmesh.needUpdateInstances = true;
    }
}

bool Mesh::isInstancedBillboard() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.instancedBillboard;
    }

    return false;
}

void Mesh::setInstancedCylindricalBillboard(bool cylindricalBillboard){
    createInstancedMesh();
    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    if (instmesh.instancedCylindricalBillboard != cylindricalBillboard){
        instmesh.instancedCylindricalBillboard = cylindricalBillboard;

        instmesh.needUpdateInstances = true;
    }
}

bool Mesh::isInstancedCylindricalBillboard() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.instancedCylindricalBillboard;
    }

    return false;
}

void Mesh::setMaxInstances(unsigned int maxInstances){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        if (instmesh.maxInstances != maxInstances){
            instmesh.maxInstances = maxInstances;

            if (MeshComponent* mesh = scene->findComponent<MeshComponent>(entity))
                mesh->needReload = true;
        }
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

unsigned int Mesh::getMaxInstances() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.maxInstances;
    }

    Log::error("There is no instanced mesh component in this mesh");
    return 0;
}

void Mesh::setDistanceFade(bool distanceFade){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        if (instmesh.distanceFade != distanceFade){
            instmesh.distanceFade = distanceFade;

            if (MeshComponent* mesh = scene->findComponent<MeshComponent>(entity))
                mesh->needReload = true;
        }
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

bool Mesh::isDistanceFade() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.distanceFade;
    }

    return false;
}

void Mesh::setFadeRange(float fadeStart, float fadeEnd){
    setFadeStart(fadeStart);
    setFadeEnd(fadeEnd);
}

void Mesh::setCullDistance(float distance){
    if (hasInstancedMesh()){
        getComponent<InstancedMeshComponent>().cullDistance = distance;
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

float Mesh::getCullDistance() const{
    return hasInstancedMesh() ? getComponent<InstancedMeshComponent>().cullDistance : 0.0f;
}

void Mesh::setFadeStart(float fadeStart){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        instmesh.fadeStart = fadeStart;
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

float Mesh::getFadeStart() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.fadeStart;
    }

    return 0;
}

void Mesh::setFadeEnd(float fadeEnd){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        instmesh.fadeEnd = fadeEnd;
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

float Mesh::getFadeEnd() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.fadeEnd;
    }

    return 0;
}

void Mesh::setCullInstances(bool cullInstances){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        instmesh.cullInstances = cullInstances;
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

bool Mesh::isCullInstances() const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.cullInstances;
    }

    return false;
}

void Mesh::addInstance(InstanceData instance){
    createInstancedMesh();

    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    instmesh.instances.push_back(instance);

    if (instmesh.maxInstances < instmesh.instances.size()){
        instmesh.maxInstances = instmesh.maxInstances * 2;
        if (MeshComponent* mesh = scene->findComponent<MeshComponent>(entity))
            mesh->needReload = true;
    }

    instmesh.needUpdateInstances = true;
}

void Mesh::addInstance(Vector3 position){
    InstanceData instance = {};

    instance.position = position;

    addInstance(instance);
}

void Mesh::addInstance(float x, float y, float z){
    addInstance(Vector3(x, y, z));
}

void Mesh::addInstance(Vector3 position, Quaternion rotation, Vector3 scale){
    InstanceData instance = {};

    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;

    addInstance(instance);
}

void Mesh::addInstance(Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color){
    InstanceData instance = {};

    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;
    instance.color = color;

    addInstance(instance);
}

void Mesh::addInstance(Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color, Rect textureRect){
    InstanceData instance = {};

    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;
    instance.color = color;
    instance.textureRect = textureRect;

    addInstance(instance);
}

InstanceData& Mesh::getInstance(size_t index){
    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    return instmesh.instances.at(index);
}

void Mesh::updateInstance(size_t index, InstanceData instance){
    createInstancedMesh();

    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    instmesh.instances.at(index) = instance;

    instmesh.needUpdateInstances = true;
}

void Mesh::updateInstance(size_t index, Vector3 position){
    InstanceData& instance = getInstance(index);

    instance.position = position;

    updateInstance(index, instance);
}

void Mesh::updateInstance(size_t index, float x, float y, float z){
    updateInstance(index, Vector3(x, y, z));
}

void Mesh::updateInstance(size_t index, Vector3 position, Quaternion rotation, Vector3 scale){
    InstanceData& instance = getInstance(index);

    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;

    updateInstance(index, instance);
}

void Mesh::updateInstance(size_t index, Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color){
    InstanceData& instance = getInstance(index);

    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;
    instance.color = color;

    updateInstance(index, instance);
}

void Mesh::updateInstance(size_t index, Vector3 position, Quaternion rotation, Vector3 scale, Vector4 color, Rect textureRect){
    InstanceData& instance = getInstance(index);

    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;
    instance.color = color;
    instance.textureRect = textureRect;

    updateInstance(index, instance);
}

void Mesh::removeInstance(size_t index){
    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    instmesh.instances.erase(instmesh.instances.begin() + index);

    instmesh.needUpdateInstances = true;
}

bool Mesh::isInstanceVisible(size_t index){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.instances.at(index).visible;
    }

    return false;
}

void Mesh::setInstanceVisible(size_t index, bool visible) const{
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        if (instmesh.instances.at(index).visible != visible){
            instmesh.instances.at(index).visible = visible;

            instmesh.needUpdateInstances = true;
        }
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

void Mesh::updateInstances(){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        instmesh.needUpdateInstances = true;
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}

size_t Mesh::getNumInstances(){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        return instmesh.instances.size();
    }

    return 0;
}

void Mesh::clearInstances(){
    if (hasInstancedMesh()){
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        instmesh.instances.clear();

        instmesh.needUpdateInstances = true;
    }else{
        Log::error("There is no instanced mesh component in this mesh");
    }
}
