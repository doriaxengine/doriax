//
// (c) 2026 Eduardo Doria.
//


#include "MeshPolygon.h"

#include "Engine.h"
#include "component/MeshPolygonComponent.h"
#include "subsystem/MeshSystem.h"

using namespace doriax;

MeshPolygon::MeshPolygon(Scene* scene): Mesh(scene){
    addComponent<MeshPolygonComponent>();

    MeshComponent& mesh = getComponent<MeshComponent>();
    mesh.submeshes[0].primitiveType = PrimitiveType::TRIANGLE_STRIP;
}

MeshPolygon::MeshPolygon(Scene* scene, Entity entity): Mesh(scene, entity){
}

MeshPolygon::~MeshPolygon(){
}

bool MeshPolygon::createPolygon(){
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();
    MeshComponent& mesh = getComponent<MeshComponent>();

    return scene->getSystem<MeshSystem>()->createOrUpdateMeshPolygon(pcomp, mesh);
}

void MeshPolygon::addVertex(Vector3 vertex){
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();

    pcomp.points.push_back({vertex, Vector4(1.0f, 1.0f, 1.0f, 1.0f)});

    pcomp.needUpdatePolygon = true;
}

void MeshPolygon::addVertex(float x, float y){
   addVertex(Vector3(x, y, 0));
}

void MeshPolygon::clearVertices(){
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();

    pcomp.points.clear();

    pcomp.needUpdatePolygon = true;
}

unsigned int MeshPolygon::getWidth(){
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();

    return pcomp.width;
}

unsigned int MeshPolygon::getHeight(){
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();

    return pcomp.height;
}

void MeshPolygon::setFlipY(bool flipY){
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();

    pcomp.automaticFlipY = false;
    if (pcomp.flipY != flipY){
        pcomp.flipY = flipY;

        pcomp.needUpdatePolygon = true;
    }
}

bool MeshPolygon::isFlipY() const{
    MeshPolygonComponent& pcomp = getComponent<MeshPolygonComponent>();

    return pcomp.flipY;
}