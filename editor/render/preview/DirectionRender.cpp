#include "DirectionRender.h"

#include "Configs.h"

using namespace doriax;

editor::DirectionRender::DirectionRender(){
    scene = new Scene(EntityPool::System);
    camera = new Camera(scene);
    light = new Light(scene);
    
    arrowObject = new Object(scene);
    arrowShaft = new Shape(scene);
    arrowHead = new Shape(scene);

    scene->setBackgroundColor(0.0, 0.0, 0.0, 0.0);
    scene->setCamera(camera);

    // Setup lighting
    light->setDirection(-0.4, -0.5, -0.5);
    light->setIntensity(6.0);
    light->setType(LightType::DIRECTIONAL);

    // Create arrow shaft (cylinder)
    arrowShaft->createCylinder(0.15, 1.5);
    arrowShaft->setColor(0.2, 0.6, 0.9, 1.0);

    arrowShaft->setPosition(0, -0.1, 0);

    // Create arrow head (cone-shaped cylinder)
    arrowHead->createCylinder(0.4, 0.0, 0.4);
    arrowHead->setColor(0.2, 0.6, 0.9, 1.0);
    
    // Position arrow head at the tip
    arrowHead->setPosition(0, 0.75, 0);

    // Add shapes to main object
    arrowObject->addChild(arrowShaft);
    arrowObject->addChild(arrowHead);

    // Setup camera
    camera->setPosition(0, 0, 3);
    camera->setTarget(0, 0, 0);
    camera->setType(CameraType::CAMERA_PERSPECTIVE);
    camera->setFramebufferSize(THUMBNAIL_SIZE, THUMBNAIL_SIZE);
    camera->setRenderToTexture(true);

    // Set default direction (pointing up)
    setDirection(0, 1, 0);
}

editor::DirectionRender::~DirectionRender(){
    delete camera;
    delete light;
    delete arrowObject;
    delete arrowShaft;
    delete arrowHead;
    delete scene;
}

void editor::DirectionRender::setDirection(Vector3 direction){
    this->direction = direction;

    if (direction.length() == 0.0f) {
        arrowObject->setVisible(false);
        return;
    }

    arrowObject->setVisible(true);
    direction.normalize();
    
    // Default arrow direction (pointing up along Y axis)
    Vector3 up = Vector3(0, 1, 0);
    
    // Check if direction is parallel to up vector (avoid division by zero)
    float dot = up.dotProduct(direction);
    
    if (abs(dot) > 0.999f) {
        // Direction is parallel to up vector
        if (dot > 0) {
            // Already pointing up, use identity quaternion
            arrowObject->setRotation(Quaternion::IDENTITY);
        } else {
            // Pointing down, rotate 180 degrees around X axis
            arrowObject->setRotation(Quaternion(180.0f, 0.0f, 0.0f));
        }
    } else {
        // Calculate rotation axis (perpendicular to both up and direction)
        Vector3 rotationAxis = up.crossProduct(direction);
        rotationAxis.normalize();
        
        // Calculate rotation angle
        float angle = acos(dot) * 180.0f / M_PI;
        
        // Create quaternion from axis-angle
        Quaternion rotation(angle, rotationAxis);
        arrowObject->setRotation(rotation);
    }
}

void editor::DirectionRender::setDirection(float x, float y, float z){
    setDirection(Vector3(x, y, z));
}

void editor::DirectionRender::setColor(Vector4 color){
    arrowShaft->setColor(color.x, color.y, color.z, color.w);
    arrowHead->setColor(color.x, color.y, color.z, color.w);
}

void editor::DirectionRender::setColor(float r, float g, float b, float a){
    arrowShaft->setColor(r, g, b, a);
    arrowHead->setColor(r, g, b, a);
}

Vector3 editor::DirectionRender::getDirection(){
    return direction;
}

void editor::DirectionRender::setBackground(Vector4 color){
    scene->setBackgroundColor(color);
}

Framebuffer* editor::DirectionRender::getFramebuffer(){
    return camera->getFramebuffer();
}

Texture editor::DirectionRender::getTexture(){
    return Texture(getFramebuffer());
}

Scene* editor::DirectionRender::getScene(){
    return scene;
}

Object* editor::DirectionRender::getObject(){
    return arrowObject;
}