#include "ViewportGizmo.h"
#include "math/Ray.h"
#include "math/Sphere.h"
#include "math/AABB.h"
#include <limits>

using namespace doriax;

editor::ViewportGizmo::ViewportGizmo(){
    scene = new Scene(EntityPool::System);
    camera = new Camera(scene);
    mainObject = new Object(scene);
    cube = new Shape(scene);
    xaxis = new Shape(scene);
    yaxis = new Shape(scene);
    zaxis = new Shape(scene);
    xarrow = new Shape(scene);
    yarrow = new Shape(scene);
    zarrow = new Shape(scene);

    scene->setBackgroundColor(0.0, 0.0, 0.0, 0.0);
    scene->setCamera(camera);

    cube->createBox(0.6,0.6,0.6);
    cube->setColor(0.5, 0.5, 0.5, 1.0);

    xaxis->createCylinder(0.12, 2);
    yaxis->createCylinder(0.12, 2);
    zaxis->createCylinder(0.12, 2);

    xaxis->setColor(0.7, 0.2, 0.2, 1.0);
    yaxis->setColor(0.2, 0.7, 0.2, 1.0);
    zaxis->setColor(0.2, 0.2, 0.7, 1.0);

    xaxis->setRotation(0,0,90);
    zaxis->setRotation(90,0,0);

    xarrow->createCylinder(0.3, 0.0, 0.6);
    yarrow->createCylinder(0.3, 0.0, 0.6);
    zarrow->createCylinder(0.3, 0.0, 0.6);

    xarrow->setPosition(1.2, 0, 0);
    yarrow->setPosition(0, 1.2, 0);
    zarrow->setPosition(0, 0, 1.2);

    xarrow->setRotation(0,0,-90);
    zarrow->setRotation(90,0,0);

    xarrow->setColor(0.7, 0.2, 0.2, 1.0);
    yarrow->setColor(0.2, 0.7, 0.2, 1.0);
    zarrow->setColor(0.2, 0.2, 0.7, 1.0);

    mainObject->addChild(cube);
    mainObject->addChild(xaxis);
    mainObject->addChild(yaxis);
    mainObject->addChild(zaxis);
    mainObject->addChild(xarrow);
    mainObject->addChild(yarrow);
    mainObject->addChild(zarrow);

    camera->setPosition(0, 0, 5);
    camera->setTarget(0, 0, 0);
    camera->setFramebufferSize(128, 128);
    camera->setRenderToTexture(true);
}

editor::ViewportGizmo::~ViewportGizmo(){
    delete camera;
    delete mainObject;
    delete cube;
    delete xaxis;
    delete yaxis;
    delete zaxis;
    delete xarrow;
    delete yarrow;
    delete zarrow;

    delete scene;
}

void editor::ViewportGizmo::applyRotation(Camera* sceneCam){
    Vector3 direction = sceneCam->getWorldDirection();
    Vector3 right = sceneCam->getWorldRight();
    Vector3 up = direction.crossProduct(right);

    mainObject->setRotation(Quaternion(right, up, direction).inverse());
}

Framebuffer* editor::ViewportGizmo::getFramebuffer(){
    return camera->getFramebuffer();
}

TextureRender& editor::ViewportGizmo::getTexture(){
    return getFramebuffer()->getRender().getColorTexture();
}

Scene* editor::ViewportGizmo::getScene(){
    return scene;
}

Object* editor::ViewportGizmo::getObject(){
    return mainObject;
}

editor::ViewportGizmoAxis editor::ViewportGizmo::hitTest(float normalizedX, float normalizedY) const {
    // Build a ray from the click position through the gizmo's 3D scene
    Matrix4 invVP = camera->getViewProjectionMatrix().inverse();

    Vector4 nearNDC(normalizedX, normalizedY, -1.0f, 1.0f);
    Vector4 farNDC(normalizedX, normalizedY, 1.0f, 1.0f);

    Vector4 nearWorld = invVP * nearNDC;
    Vector4 farWorld = invVP * farNDC;
    nearWorld.divideByW();
    farWorld.divideByW();

    Vector3 rayOrigin(nearWorld.x, nearWorld.y, nearWorld.z);
    Vector3 rayEnd(farWorld.x, farWorld.y, farWorld.z);
    Vector3 rayDir = rayEnd - rayOrigin;

    Ray ray(rayOrigin, rayDir);

    Quaternion objRotation = mainObject->getRotation();

    // Axis arrow tip positions and hit sphere radius
    float tipDist = 1.2f;
    float hitRadius = 0.45f;

    struct AxisInfo {
        Vector3 localPos;
        ViewportGizmoAxis axis;
    };

    AxisInfo axes[] = {
        { Vector3( tipDist, 0, 0), ViewportGizmoAxis::POSITIVE_X },
        { Vector3(-tipDist, 0, 0), ViewportGizmoAxis::NEGATIVE_X },
        { Vector3(0,  tipDist, 0), ViewportGizmoAxis::POSITIVE_Y },
        { Vector3(0, -tipDist, 0), ViewportGizmoAxis::NEGATIVE_Y },
        { Vector3(0, 0,  tipDist), ViewportGizmoAxis::POSITIVE_Z },
        { Vector3(0, 0, -tipDist), ViewportGizmoAxis::NEGATIVE_Z },
    };

    float closestT = std::numeric_limits<float>::max();
    ViewportGizmoAxis closestAxis = ViewportGizmoAxis::NONE;

    // Test axis tip spheres
    for (const auto& a : axes) {
        Vector3 worldPos = objRotation * a.localPos;
        Sphere sphere(worldPos, hitRadius);
        RayReturn result = ray.intersects(sphere);
        if (result && result.distance >= 0 && result.distance < closestT) {
            closestT = result.distance;
            closestAxis = a.axis;
        }
    }

    // Test central cube (0.6x0.6x0.6 centered at origin, rotated)
    // Use an AABB-approximating sphere for the cube center
    if (closestAxis == ViewportGizmoAxis::NONE) {
        float cubeHalf = 0.35f;
        Vector3 cubeMin = Vector3(-cubeHalf, -cubeHalf, -cubeHalf);
        Vector3 cubeMax = Vector3( cubeHalf,  cubeHalf,  cubeHalf);

        // Transform ray into local space of mainObject to test against the cube AABB
        Quaternion invRotation = objRotation.inverse();
        Vector3 localOrigin = invRotation * rayOrigin;
        Vector3 localDir = invRotation * rayDir;
        Ray localRay(localOrigin, localDir);

        AABB cubeAABB(cubeMin, cubeMax);
        RayReturn result = localRay.intersects(cubeAABB);
        if (result) {
            // Determine which face was hit from the normal or hit point
            Vector3 n = result.normal;
            if (n == Vector3::ZERO) {
                // Fallback: determine face from hit point's dominant axis
                Vector3 p = result.point;
                float ax = std::abs(p.x);
                float ay = std::abs(p.y);
                float az = std::abs(p.z);
                if (ax >= ay && ax >= az) n = Vector3(p.x > 0 ? 1.0f : -1.0f, 0, 0);
                else if (ay >= ax && ay >= az) n = Vector3(0, p.y > 0 ? 1.0f : -1.0f, 0);
                else n = Vector3(0, 0, p.z > 0 ? 1.0f : -1.0f);
            }
            if (n.x > 0.5f)       closestAxis = ViewportGizmoAxis::POSITIVE_X;
            else if (n.x < -0.5f) closestAxis = ViewportGizmoAxis::NEGATIVE_X;
            else if (n.y > 0.5f)  closestAxis = ViewportGizmoAxis::POSITIVE_Y;
            else if (n.y < -0.5f) closestAxis = ViewportGizmoAxis::NEGATIVE_Y;
            else if (n.z > 0.5f)  closestAxis = ViewportGizmoAxis::POSITIVE_Z;
            else if (n.z < -0.5f) closestAxis = ViewportGizmoAxis::NEGATIVE_Z;
        }
    }

    return closestAxis;
}