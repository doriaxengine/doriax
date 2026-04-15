#include "UILayer.h"

using namespace doriax;

editor::UILayer::UILayer(bool enableViewGizmo){
    Vector3 rectColor = Vector3(0.3, 0.1, 0.2);

    scene = new Scene(EntityPool::System);
    camera = new Camera(scene);

    selectionRect = new Object(scene);
    upRect = new Polygon(scene);
    bottomRect = new Polygon(scene);
    leftRect = new Polygon(scene);
    rightRect = new Polygon(scene);
    centralRect = new Polygon(scene);

    upRect->setColor(Vector4(rectColor, 1.0));
    bottomRect->setColor(Vector4(rectColor, 1.0));
    leftRect->setColor(Vector4(rectColor, 1.0));
    rightRect->setColor(Vector4(rectColor, 1.0));
    centralRect->setColor(Vector4(rectColor, 0.2));

    selectionRect->setVisible(false);
    selectionRect->addChild(upRect);
    selectionRect->addChild(bottomRect);
    selectionRect->addChild(leftRect);
    selectionRect->addChild(rightRect);
    selectionRect->addChild(centralRect);

    // to avoid try to load every frame (without vertices)
    updateRect(Vector2::ZERO, Vector2::ZERO);
    
    camera->setType(CameraType::CAMERA_UI);

    if (enableViewGizmo){
        viewGizmoImage = new Image(scene);

        viewGizmoImage->setAnchorPreset(AnchorPreset::TOP_RIGHT);
        viewGizmoImage->setSize(100, 100);
    }else{
        viewGizmoImage = nullptr;
    }

    scene->setCamera(camera);
}

editor::UILayer::~UILayer(){
    delete camera;
    delete selectionRect;
    delete upRect;
    delete bottomRect;
    delete leftRect;
    delete rightRect;
    delete centralRect;
    if (viewGizmoImage){
        delete viewGizmoImage;
    }

    delete scene;
}

void editor::UILayer::setViewportGizmoTexture(Framebuffer* framebuffer){
    viewGizmoImage->setTexture(framebuffer);
}

void editor::UILayer::setViewGizmoImageVisible(bool visible){
    if (viewGizmoImage){
        viewGizmoImage->setVisible(visible);
    }
}

void editor::UILayer::setSelectionBoxVisible(bool visible){
    selectionRect->setVisible(visible);
}

void editor::UILayer::updateRect(Vector2 position, Vector2 size){
    float thickness = 2;

    if (size.x < 0 && size.y < 0){
        position = position + size - Vector2(thickness);
        size = -size + Vector2(thickness*2);
    }

    selectionRect->setPosition(Vector3(position, 0));

    upRect->clearVertices();
    leftRect->clearVertices();
    rightRect->clearVertices();
    bottomRect->clearVertices();
    centralRect->clearVertices();

    upRect->addVertex(0, 0);
    upRect->addVertex(0, thickness);
    upRect->addVertex(size.x, 0);
    upRect->addVertex(size.x, thickness);

    bottomRect->addVertex(0, size.y-thickness);
    bottomRect->addVertex(0, size.y);
    bottomRect->addVertex(size.x, size.y-thickness);
    bottomRect->addVertex(size.x, size.y);

    leftRect->addVertex(0, 0);
    leftRect->addVertex(0, size.y);
    leftRect->addVertex(thickness, 0);
    leftRect->addVertex(thickness, size.y);

    rightRect->addVertex(size.x-thickness, 0);
    rightRect->addVertex(size.x-thickness, size.y);
    rightRect->addVertex(size.x, 0);
    rightRect->addVertex(size.x, size.y);

    centralRect->addVertex(0, 0);
    centralRect->addVertex(0, size.y);
    centralRect->addVertex(size.x, 0);
    centralRect->addVertex(size.x, size.y);
}

Framebuffer* editor::UILayer::getFramebuffer(){
    return camera->getFramebuffer();
}

TextureRender& editor::UILayer::getTexture(){
    return getFramebuffer()->getRender().getColorTexture();
}

Camera* editor::UILayer::getCamera(){
    return camera;
}

Scene* editor::UILayer::getScene(){
    return scene;
}