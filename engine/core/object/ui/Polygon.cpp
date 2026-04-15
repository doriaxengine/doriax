//
// (c) 2026 Eduardo Doria.
//


#include "Polygon.h"

#include "util/Color.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/UISystem.h"

using namespace doriax;

Polygon::Polygon(Scene* scene): UILayout(scene){
    addComponent<UIComponent>();
    addComponent<PolygonComponent>();
}

Polygon::Polygon(Scene* scene, Entity entity): UILayout(scene, entity){
}

Polygon::~Polygon(){
}

bool Polygon::createPolygon(){
    PolygonComponent& pcomp = getComponent<PolygonComponent>();
    UIComponent& ui = getComponent<UIComponent>();
    UILayoutComponent& layout = getComponent<UILayoutComponent>();

    return scene->getSystem<UISystem>()->createOrUpdatePolygon(pcomp, ui, layout);
}

bool Polygon::load(){
    UIComponent& ui = getComponent<UIComponent>();

    return scene->getSystem<RenderSystem>()->loadUI(entity, ui, PIP_DEFAULT | PIP_RTT, false);
}

void Polygon::addVertex(Vector3 vertex){
    PolygonComponent& pcomp = getComponent<PolygonComponent>();

    pcomp.points.push_back({vertex, Vector4(1.0f, 1.0f, 1.0f, 1.0f)});

    pcomp.needUpdatePolygon = true;
}

void Polygon::addVertex(float x, float y){
   addVertex(Vector3(x, y, 0));
}

void Polygon::clearVertices(){
    PolygonComponent& pcomp = getComponent<PolygonComponent>();

    pcomp.points.clear();

    pcomp.needUpdatePolygon = true;
}

void Polygon::setTexture(const std::string& path){
    UIComponent& ui = getComponent<UIComponent>();

    ui.texture.setPath(path);

    ui.needUpdateTexture = true;
}

void Polygon::setTexture(const std::string& id, TextureData data){
    UIComponent& ui = getComponent<UIComponent>();

    ui.texture.setData(id, data);

    ui.needUpdateTexture = true;
}

void Polygon::setTexture(Framebuffer* framebuffer){
    UIComponent& ui = getComponent<UIComponent>();

    ui.texture.setFramebuffer(framebuffer);

    ui.needUpdateTexture = true;
}

void Polygon::setColor(Vector4 color){
    UIComponent& ui = getComponent<UIComponent>();

    ui.color = Color::sRGBToLinear(color);
}

void Polygon::setColor(const float red, const float green, const float blue, const float alpha){
    setColor(Vector4(red, green, blue, alpha));
}

void Polygon::setColor(const float red, const float green, const float blue){
    setColor(Vector4(red, green, blue, getColor().w));
}

void Polygon::setAlpha(const float alpha){
    Vector4 color = getColor();
    setColor(Vector4(color.x, color.y, color.z, alpha));
}

Vector4 Polygon::getColor() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.color;
}

float Polygon::getAlpha() const{
    return getColor().w;
}

void Polygon::setFlipY(bool flipY){
    UIComponent& ui = getComponent<UIComponent>();
    PolygonComponent& pcomp = getComponent<PolygonComponent>();

    ui.automaticFlipY = false;
    if (ui.flipY != flipY){
        ui.flipY = flipY;

        pcomp.needUpdatePolygon = true;
    }
}

bool Polygon::isFlipY() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.flipY;
}

AABB Polygon::getAABB() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.aabb;
}

AABB Polygon::getWorldAABB() const{
    UIComponent& ui = getComponent<UIComponent>();

    return ui.worldAABB;
}