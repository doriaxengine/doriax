//
// (c) 2026 Eduardo Doria.
//

#include "Camera.h"

#include "util/Angle.h"
#include "Engine.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

Camera::Camera(Scene* scene): Object(scene){
    addComponent<CameraComponent>();

    CameraComponent& cameraComponent = getComponent<CameraComponent>();
    applyPerspectiveDefaults(cameraComponent);
    // default CameraComponent is CAMERA_UI
    cameraComponent.type = CameraType::CAMERA_PERSPECTIVE;

    Transform& transform = getComponent<Transform>();
    transform.position = Vector3(0.0, 0.0, 1.0);
}

Camera::Camera(Scene* scene, Entity entity): Object(scene, entity){
}

Camera::~Camera(){
}

void Camera::applyOrthoDefaults(CameraComponent& cameraComponent){
    cameraComponent.leftClip = 0;
    cameraComponent.rightClip = Engine::getCanvasWidth();
    cameraComponent.bottomClip = 0;
    cameraComponent.topClip = Engine::getCanvasHeight();
    cameraComponent.nearClip = DEFAULT_ORTHO_NEAR;
    cameraComponent.farClip = DEFAULT_ORTHO_FAR;
}

void Camera::applyPerspectiveDefaults(CameraComponent& cameraComponent){
    cameraComponent.yfov = 0.75;

    if (Engine::getCanvasWidth() != 0 && Engine::getCanvasHeight() != 0) {
        cameraComponent.aspect = (float) Engine::getCanvasWidth() / (float) Engine::getCanvasHeight();
    }else{
        cameraComponent.aspect = 1.0;
    }

    cameraComponent.nearClip = DEFAULT_PERSPECTIVE_NEAR;
    cameraComponent.farClip = DEFAULT_PERSPECTIVE_FAR;
}

void Camera::activate(){
    scene->setCamera(entity);
}

void Camera::setOrtho(float left, float right, float bottom, float top, float nearValue, float farValue){
    CameraComponent& camera = getComponent<CameraComponent>();

    camera.type = CameraType::CAMERA_ORTHO;

    camera.leftClip = left;
    camera.rightClip = right;
    camera.bottomClip = bottom;
    camera.topClip = top;
    camera.nearClip = nearValue;
    camera.farClip = farValue;
    
    camera.autoResize = false;

    camera.needUpdate = true;
}

void Camera::setPerspective(float yfov, float aspect, float nearValue, float farValue){
    CameraComponent& camera = getComponent<CameraComponent>();

    camera.type = CameraType::CAMERA_PERSPECTIVE;

    camera.yfov = Angle::defaultToRad(yfov);
    camera.aspect = aspect;
    camera.nearClip = nearValue;
    camera.farClip = farValue;
    
    camera.autoResize = false;

    camera.needUpdate = true;
}

void Camera::setAutoResize(bool autoResize){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.autoResize != autoResize){
        camera.autoResize = autoResize;

        camera.needUpdate = true;
    }
}

bool Camera::isAutoResize() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.autoResize;
}

void Camera::setNearClip(float nearValue){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.nearClip != nearValue){
        camera.nearClip = nearValue;

        camera.needUpdate = true;
    }
}

float Camera::getNearClip() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.nearClip;
}

void Camera::setFarClip(float farValue){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.farClip != farValue){
        camera.farClip = farValue;

        camera.needUpdate = true;
    }
}

float Camera::getFarClip() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.farClip;
}

void Camera::setLeftClip(float left){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.leftClip != left){
        camera.leftClip = left;

        camera.autoResize = false;

        camera.needUpdate = true;
    }
}

float Camera::getLeftClip() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.leftClip;
}

void Camera::setRightClip(float right){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.rightClip != right){
        camera.rightClip = right;

        camera.autoResize = false;

        camera.needUpdate = true;
    }
}

float Camera::getRightClip() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.rightClip;
}

void Camera::setBottomClip(float bottom){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.bottomClip != bottom){
        camera.bottomClip = bottom;

        camera.autoResize = false;

        camera.needUpdate = true;
    }
}

float Camera::getBottomClip() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.bottomClip;
}

void Camera::setTopClip(float top){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.topClip != top){
        camera.topClip = top;

        camera.autoResize = false;

        camera.needUpdate = true;
    }
}

float Camera::getTopClip() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.topClip;
}

void Camera::setAspect(float aspect){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.aspect != aspect){
        camera.aspect = aspect;

        camera.autoResize = false;

        camera.needUpdate = true;
    }
}

float Camera::getAspect() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.aspect;
}

void Camera::setYFov(float yfov){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.yfov != Angle::defaultToRad(yfov)){
        camera.yfov = Angle::defaultToRad(yfov);

        camera.needUpdate = true;
    }
}

float Camera::getYFov() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return Angle::radToDefault(camera.yfov);
}

void Camera::setType(CameraType type){
    CameraComponent& camera = getComponent<CameraComponent>();
    
    if (camera.type != type){
        camera.type = type;

        if (camera.type == CameraType::CAMERA_PERSPECTIVE){
            applyPerspectiveDefaults(camera);
        }else{
            applyOrthoDefaults(camera);
        }

        camera.needUpdate = true;
    }
}

CameraType Camera::getType() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    
    return camera.type;
}

void Camera::setTarget(Vector3 target){
    CameraComponent& camera = getComponent<CameraComponent>();

    if (camera.target != target){
        camera.target = target;

        camera.needUpdate = true;
    }
    camera.useTarget = true;
}

void Camera::setTarget(const float x, const float y, const float z){
    setTarget(Vector3(x,y,z));
}

Vector3 Camera::getTarget() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.target;
}

void Camera::disableTarget(){
    CameraComponent& camera = getComponent<CameraComponent>();
    camera.useTarget = false;
    camera.needUpdate = true;
}

bool Camera::isUsingTarget() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.useTarget;
}

void Camera::setUp(Vector3 up){
    CameraComponent& camera = getComponent<CameraComponent>();
    
    if (camera.up != up){
        camera.up = up;

        camera.needUpdate = true;
    }
}

void Camera::setUp(const float x, const float y, const float z){
    setUp(Vector3(x,y,z));
}

Vector3 Camera::getUp() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.up;
}

Vector3 Camera::getDirection() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.direction;
}

Vector3 Camera::getRight() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.right;
}

Vector3 Camera::getWorldTarget() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.worldTarget;
}

Vector3 Camera::getWorldDirection() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.worldDirection;
}

Vector3 Camera::getWorldUp() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.worldUp;
}

Vector3 Camera::getWorldRight() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.worldRight;
}

Matrix4 Camera::getViewMatrix() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.viewMatrix;
}

Matrix4 Camera::getProjectionMatrix() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.projectionMatrix;
}

Matrix4 Camera::getViewProjectionMatrix() const{
    CameraComponent& camera = getComponent<CameraComponent>();
    return camera.viewProjectionMatrix;
}

void Camera::rotateView(float angle){
    if (angle != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        if (camera.useTarget){
            Vector3 viewCenter = camera.target - transf.position;

            Matrix4 rotation;
            rotation = Matrix4::rotateMatrix(angle, camera.up);
            viewCenter = rotation * viewCenter;

            camera.target = viewCenter + transf.position;
        }else{
            Quaternion rotation;
            rotation.fromAngleAxis(angle, camera.up);
            transf.rotation = rotation * transf.rotation;
            transf.needUpdate = true;
        }

        camera.needUpdate = true;
    }
}

void Camera::rotatePosition(float angle){
    if (angle != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        Vector3 positionCenter = transf.position - camera.target;

        Matrix4 rotation;
        rotation = Matrix4::rotateMatrix(angle, camera.up);
        positionCenter = rotation * positionCenter;

        transf.position = positionCenter + camera.target;

        transf.needUpdate = true;
        camera.needUpdate = true;
    }
}

void Camera::elevateView(float angle){
    if (angle != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        if (camera.useTarget){
            Vector3 viewCenter = camera.target - transf.position;

            Matrix4 rotation;
            rotation = Matrix4::rotateMatrix(angle, viewCenter.crossProduct(camera.up));
            viewCenter = rotation * viewCenter;

            camera.target = viewCenter + transf.position;
        }else{
            Quaternion rotation;
            rotation.fromAngleAxis(angle, camera.right);
            transf.rotation = rotation * transf.rotation;
            transf.needUpdate = true;
        }

        camera.needUpdate = true;
    }
}

void Camera::elevatePosition(float angle){
    if (angle != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        Vector3 positionCenter = transf.position - camera.target;
        Vector3 dirNorm = (-positionCenter).normalize();

        // Clamp elevation to prevent flipping over the poles (~89 degrees)
        float dotUp = dirNorm.dotProduct(camera.up);
        const float limit = 0.99f;
        if (dotUp < -limit && angle > 0) return;
        if (dotUp >  limit && angle < 0) return;

        Matrix4 rotation;
        rotation = Matrix4::rotateMatrix(angle, positionCenter.crossProduct(camera.up));
        positionCenter = rotation * positionCenter;

        transf.position = positionCenter + camera.target;

        transf.needUpdate = true;
        camera.needUpdate = true;
    }
}

void Camera::walkForward(float distance){
    if (distance != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        Vector3 walkVector = camera.up.crossProduct(camera.right);
        walkVector.normalize();

        camera.target = camera.target + (walkVector * distance);
        transf.position = transf.position + (walkVector * distance);

        transf.needUpdate = true;
        camera.needUpdate = true;
    }
}

void Camera::zoom(float distance){
    if (distance != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        transf.position = transf.position - (camera.direction * distance);

        transf.needUpdate = true;
    }
}

void Camera::slide(float distance){
    if (distance != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        camera.target = camera.target + (camera.right * distance);
        transf.position = transf.position + (camera.right * distance);

        transf.needUpdate = true;
        camera.needUpdate = true;
    }
}

void Camera::slideForward(float distance){
    if (distance != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        camera.target = camera.target - (camera.direction * distance);
        transf.position = transf.position - (camera.direction * distance);

        transf.needUpdate = true;
        camera.needUpdate = true;
    }
}

void Camera::slideUp(float distance){
    if (distance != 0){
        CameraComponent& camera = getComponent<CameraComponent>();
        Transform& transf = getComponent<Transform>();

        Vector3 slideVector = camera.direction.crossProduct(camera.right);
        slideVector.normalize();

        camera.target = camera.target + (slideVector * distance);
        transf.position = transf.position + (slideVector * distance);

        transf.needUpdate = true;
        camera.needUpdate = true;
    }
}

void Camera::setRenderToTexture(bool renderToTexture){
    CameraComponent& camera = getComponent<CameraComponent>();

	camera.renderToTexture = renderToTexture;
}

bool Camera::isRenderToTexture() const{
    CameraComponent& camera = getComponent<CameraComponent>();

	return camera.renderToTexture;
}

Framebuffer* Camera::getFramebuffer(){
    CameraComponent& camera = getComponent<CameraComponent>();

	return camera.framebuffer;
}

void Camera::setFramebufferSize(int width, int height){
    CameraComponent& camera = getComponent<CameraComponent>();

    float oldWidth = camera.framebuffer->getWidth();
    float oldHeight = camera.framebuffer->getHeight();

    if ((oldWidth != width) || (oldHeight != height)){
        camera.framebuffer->setWidth(width);
        camera.framebuffer->setHeight(height);

        if (camera.renderToTexture){
            scene->getSystem<RenderSystem>()->updateFramebuffer(camera);
            scene->getSystem<RenderSystem>()->updateCameraSize(entity);
        }
    }
}

void Camera::setFramebufferFilter(TextureFilter filter){
    CameraComponent& camera = getComponent<CameraComponent>();

	camera.framebuffer->setMinFilter(filter);
	camera.framebuffer->setMagFilter(filter);

	if (camera.renderToTexture){
		scene->getSystem<RenderSystem>()->updateFramebuffer(camera);
	}
}

void Camera::setTransparentSort(bool transparentSort){
    CameraComponent& camera = getComponent<CameraComponent>();

    camera.transparentSort = transparentSort;
}

bool Camera::isTransparentSort() const{
    CameraComponent& camera = getComponent<CameraComponent>();

    return camera.transparentSort;
}

Ray Camera::screenToRay(float x, float y){
    CameraComponent& camera = getComponent<CameraComponent>();

    // Calculate normalized screen coordinates [-1, 1]
    float normalized_x = ((2.0f * x) / Engine::getCanvasWidth()) - 1.0f;
    float normalized_y = -(((2.0f * y) / Engine::getCanvasHeight()) - 1.0f);

    // Points in NDC space
    Vector4 near_point_ndc = {normalized_x, normalized_y, -1.0f, 1.0f};
    Vector4 far_point_ndc = {normalized_x, normalized_y, 1.0f, 1.0f};

    // Transform from NDC to world space using inverse view-projection matrix
    Matrix4 inverseViewProjection = camera.viewProjectionMatrix.inverse();
    Vector4 near_point_world = inverseViewProjection * near_point_ndc;
    Vector4 far_point_world = inverseViewProjection * far_point_ndc;

    // Perspective division
    near_point_world.divideByW();
    far_point_world.divideByW();

    Vector3 ray_origin = {near_point_world[0], near_point_world[1], near_point_world[2]};
    Vector3 ray_end = {far_point_world[0], far_point_world[1], far_point_world[2]};

    Vector3 ray_direction = ray_end - ray_origin;

    // For orthographic cameras, ensure the ray length is appropriate
    if (camera.type == CameraType::CAMERA_ORTHO) {
        // Scale the direction to match the far-near distance
        float length = camera.farClip - camera.nearClip;
        ray_direction = ray_direction.normalize() * length;
    }

    return Ray(ray_origin, ray_direction);
}

float Camera::getDistanceFromTarget() const {
    Transform& transform = getComponent<Transform>();
    CameraComponent& camera = getComponent<CameraComponent>();

    return (transform.position - camera.target).length();
}

void Camera::updateCamera(){
    Transform& transform = getComponent<Transform>();
    CameraComponent& camera = getComponent<CameraComponent>();

    scene->getSystem<RenderSystem>()->updateCamera(camera, transform);
}