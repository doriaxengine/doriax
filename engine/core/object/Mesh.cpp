//
// (c) 2026 Eduardo Doria.
//

#include "Mesh.h"
#include "render/ObjectRender.h"
#include "util/Color.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/MeshSystem.h"

using namespace doriax;

Mesh::Mesh(Scene* scene): Object(scene){
    addComponent<MeshComponent>();
}

Mesh::Mesh(Scene* scene, Entity entity): Object(scene, entity){
}

Mesh::~Mesh(){
}

bool Mesh::load(){
    MeshComponent& mesh = getComponent<MeshComponent>();

    TerrainComponent* terrain = scene->findComponent<TerrainComponent>(entity);
    InstancedMeshComponent* instmesh = scene->findComponent<InstancedMeshComponent>(entity);

    //TODO: check mesh pipelines before call
    return scene->getSystem<RenderSystem>()->loadMesh(entity, mesh, PIP_DEFAULT | PIP_RTT, instmesh, terrain);
}

void Mesh::setTexture(const std::string& path){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.submeshes[0].material.baseColorTexture.setPath(path);

    mesh.submeshes[0].needUpdateTexture = true;
}

void Mesh::setTexture(const std::string& id, TextureData data){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.submeshes[0].material.baseColorTexture.setData(id, data);

    mesh.submeshes[0].needUpdateTexture = true;
}

void Mesh::setTexture(Framebuffer* framebuffer){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.submeshes[0].material.baseColorTexture.setFramebuffer(framebuffer);

    mesh.submeshes[0].needUpdateTexture = true;
}

void Mesh::setColor(Vector4 color){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.submeshes[0].material.baseColorFactor = Color::sRGBToLinear(color);
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
    MeshComponent& mesh = getComponent<MeshComponent>();

    return Color::linearTosRGB(mesh.submeshes[0].material.baseColorFactor);
}

float Mesh::getAlpha() const{
    return getColor().w;
}

void Mesh::setMaterial(const Material& material){
    setMaterial(0, material);
}

Material Mesh::getMaterial() const{
    return getMaterial(0);
}

void Mesh::setMaterial(unsigned int submesh, const Material& material){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.submeshes[submesh].material != material){
        mesh.submeshes[submesh].material = material;
        mesh.submeshes[submesh].needUpdateTexture = true;
    }
}

Material Mesh::getMaterial(unsigned int submesh) const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.submeshes[submesh].material;
}

void Mesh::setPrimitiveType(PrimitiveType primitiveType){
    setPrimitiveType(0, primitiveType);
}

PrimitiveType Mesh::getPrimitiveType() const{
    return getPrimitiveType(0);
}

void Mesh::setPrimitiveType(unsigned int submesh, PrimitiveType primitiveType){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.submeshes[submesh].primitiveType != primitiveType){
        mesh.submeshes[submesh].primitiveType = primitiveType;

        mesh.needReload = true;
    }
}

PrimitiveType Mesh::getPrimitiveType(unsigned int submesh) const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.submeshes[submesh].primitiveType;
}

void Mesh::setFaceCulling(bool faceCulling){
    setFaceCulling(0, faceCulling);
}

bool Mesh::isFaceCulling() const{
    return isFaceCulling(0);
}

void Mesh::setFaceCulling(unsigned int submesh, bool faceCulling){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.submeshes[submesh].faceCulling != faceCulling){
        mesh.submeshes[submesh].faceCulling = faceCulling;

        mesh.needReload = true;
    }
}

bool Mesh::isFaceCulling(unsigned int submesh) const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.submeshes[submesh].faceCulling;
}


void Mesh::setCastShadowsWithTexture(bool castShadowsWithTexture){
    setCastShadowsWithTexture(0, castShadowsWithTexture);
}

bool Mesh::isCastShadowsWithTexture() const{
    return isCastShadowsWithTexture(0);
}

void Mesh::setCastShadowsWithTexture(unsigned int submesh, bool castShadowsWithTexture){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.submeshes[submesh].textureShadow = castShadowsWithTexture;

    mesh.needReload = true;
}

bool Mesh::isCastShadowsWithTexture(unsigned int submesh) const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.submeshes[submesh].textureShadow;
}

void Mesh::setCullingMode(CullingMode cullingMode){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.cullingMode != cullingMode){
        mesh.cullingMode = cullingMode;

        mesh.needReload = true;
    }
}

CullingMode Mesh::getCullingMode() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.cullingMode;
}

void Mesh::setWindingOrder(WindingOrder windingOrder){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.windingOrder != windingOrder){
        mesh.windingOrder = windingOrder;

        mesh.needReload = true;
    }
}

WindingOrder Mesh::getWindingOrder() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.windingOrder;
}


AABB Mesh::getAABB() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.aabb;
}

AABB Mesh::getVerticesAABB() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.verticesAABB;
}

AABB Mesh::getWorldAABB() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.worldAABB;
}

unsigned int Mesh::getNumSubmeshes() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.numSubmeshes;
}

void Mesh::setReceiveLights(bool receiveLights){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.receiveLights != receiveLights){
        mesh.receiveLights = receiveLights;

        mesh.needReload = true;
    }
}

bool Mesh::isReceiveLights() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.receiveLights;
}

void Mesh::setCastShadows(bool castShadows){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.castShadows = castShadows;
}

bool Mesh::isCastShadows() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.castShadows;
}

void Mesh::setReceiveShadows(bool receiveShadows){
    MeshComponent& mesh = getComponent<MeshComponent>();

    if (mesh.receiveShadows != receiveShadows){
        mesh.receiveShadows = receiveShadows;

        mesh.needReload = true;
    }
}

bool Mesh::isReceiveShadows() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.receiveShadows;
}

void Mesh::setShadowsBillboard(bool shadowsBillboard){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.shadowsBillboard = shadowsBillboard;
}

bool Mesh::isShadowsBillboard() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.shadowsBillboard;
}

void Mesh::setTransparent(bool transparent){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.transparent = transparent;
}

bool Mesh::isTransparent() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.transparent;
}

void Mesh::setAutoTransparency(bool autoTransparency){
    MeshComponent& mesh = getComponent<MeshComponent>();

    mesh.autoTransparency = autoTransparency;
}

bool Mesh::isAutoTransparency() const{
    MeshComponent& mesh = getComponent<MeshComponent>();

    return mesh.autoTransparency;
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
        MeshComponent& mesh = getComponent<MeshComponent>();
        InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

        if (instmesh.maxInstances != maxInstances){
            instmesh.maxInstances = maxInstances;

            mesh.needReload = true;
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

void Mesh::addInstance(InstanceData instance){
    createInstancedMesh();

    MeshComponent& mesh = getComponent<MeshComponent>();
    InstancedMeshComponent& instmesh = getComponent<InstancedMeshComponent>();

    instmesh.instances.push_back(instance);

    if (instmesh.maxInstances < instmesh.instances.size()){
        instmesh.maxInstances = instmesh.maxInstances * 2;
        mesh.needReload = true;
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

    MeshComponent& mesh = getComponent<MeshComponent>();
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