// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Doriax.h"

#include <cassert>
#include <cmath>
#include <vector>

using namespace doriax;

namespace {

bool closeEnough(float a, float b){
    return std::fabs(a - b) < 0.0001f;
}

}

int main(){
    Scene scene;

    Entity nullBounds = scene.createEntity();
    scene.addComponent<MeshComponent>(nullBounds);

    Entity farBounds = scene.createEntity();
    scene.addComponent<MeshComponent>(farBounds);
    scene.getComponent<MeshComponent>(farBounds).worldAABB = AABB(6.0f, -1.0f, -1.0f, 7.0f, 1.0f, 1.0f);

    Entity nearBounds = scene.createEntity();
    scene.addComponent<MeshComponent>(nearBounds);
    scene.getComponent<MeshComponent>(nearBounds).worldAABB = AABB(2.0f, -1.0f, -1.0f, 3.0f, 1.0f, 1.0f);

    Ray ray(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f));

    RayReturn closest = ray.intersects(&scene, RayFilter::BOUNDS);
    assert(closest.hit);
    assert(closest.body == nearBounds);
    assert(closeEnough(closest.distance, 0.2f));
    assert(closest.shapeIndex == 0);

    RayReturn ignoredEntity = ray.intersects(&scene, RayFilter::BOUNDS, nearBounds);
    assert(ignoredEntity.hit);
    assert(ignoredEntity.body == farBounds);

    const std::vector<Entity> ignoredEntities{nearBounds, farBounds};
    assert(!ray.intersects(&scene, RayFilter::BOUNDS, ignoredEntities));

    const AABB overlappingA(1.0f, 1.0f, 1.0f, 3.0f, 3.0f, 3.0f);
    const AABB overlappingB(2.0f, 2.0f, 2.0f, 4.0f, 4.0f, 4.0f);
    assert(overlappingA.intersects(overlappingB));
    assert(ray.intersects(AABB(1.0f, -1.0f, -1.0f, 2.0f, 1.0f, 1.0f)));
    assert(ray.intersects(OBB(Vector3(1.5f, 0.0f, 0.0f), Vector3(0.5f, 1.0f, 1.0f))));

    return 0;
}
