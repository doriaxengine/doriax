//
// (c) 2026 Eduardo Doria.
//

#include "Points.h"
#include "util/Angle.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

Points::Points(Scene* scene): Object(scene){
    addComponent<PointsComponent>();
}

Points::Points(Scene* scene, Entity entity): Object(scene, entity){
}

Points::~Points(){
}

bool Points::load(){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    return scene->getSystem<RenderSystem>()->loadPoints(entity, pointscomp, PIP_DEFAULT | PIP_RTT);
}

void Points::setMaxPoints(unsigned int maxPoints){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    if (pointscomp.maxPoints != maxPoints){
        pointscomp.maxPoints = maxPoints;

        pointscomp.needReload = true;
    }
}

unsigned int Points::getMaxPoints() const{
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    return pointscomp.maxPoints;
}

void Points::addPoint(PointData point){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    pointscomp.points.push_back(point);

    if (pointscomp.maxPoints < pointscomp.points.size()){
        pointscomp.maxPoints = pointscomp.maxPoints * 2;
        pointscomp.needReload = true;
    }

    if (point.textureRect != Rect(0,0,1,1) && !pointscomp.hasTextureRect){
        pointscomp.hasTextureRect = true;
        pointscomp.needReload = true;
    }

    pointscomp.needUpdate = true;
}

void Points::addPoint(Vector3 position){
    PointData point = {};

    point.position = position;

    addPoint(point);
}

void Points::addPoint(float x, float y, float z){
   addPoint(Vector3(x, y, z));
}

void Points::addPoint(Vector3 position, Vector4 color){
    PointData point = {};

    point.position = position;
    point.color = color;

    addPoint(point);
}

void Points::addPoint(Vector3 position, Vector4 color, float size){
    PointData point = {};

    point.position = position;
    point.color = color;
    point.size = size;

    addPoint(point);
}

void Points::addPoint(Vector3 position, Vector4 color, float size, float rotation){
    PointData point = {};

    point.position = position;
    point.color = color;
    point.size = size;
    point.rotation = rotation;

    addPoint(point);
}

void Points::addPoint(Vector3 position, Vector4 color, float size, float rotation, Rect textureRect){
    PointData point = {};

    point.position = position;
    point.color = color;
    point.size = size;
    point.rotation = rotation;
    point.textureRect = textureRect;

    addPoint(point);
}

PointData& Points::getPoint(size_t index){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    return pointscomp.points.at(index);
}

void Points::updatePoint(size_t index, PointData point){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    pointscomp.points.at(index) = point;

    if (point.textureRect != Rect(0,0,1,1) && !pointscomp.hasTextureRect){
        pointscomp.hasTextureRect = true;
        pointscomp.needReload = true;
    }

    pointscomp.needUpdate = true;
}

void Points::updatePoint(size_t index, Vector3 position){
    PointData point = getPoint(index);

    point.position = position;

    updatePoint(index, point);
}

void Points::updatePoint(size_t index, float x, float y, float z){
    updatePoint(index, Vector3(x, y, z));
}

void Points::updatePoint(size_t index, Vector3 position, Vector4 color){
    PointData point = getPoint(index);

    point.position = position;
    point.color = color;

    updatePoint(index, point);
}

void Points::updatePoint(size_t index, Vector3 position, Vector4 color, float size){
    PointData point = getPoint(index);

    point.position = position;
    point.color = color;
    point.size = size;

    updatePoint(index, point);
}

void Points::updatePoint(size_t index, Vector3 position, Vector4 color, float size, float rotation){
    PointData point = getPoint(index);

    point.position = position;
    point.color = color;
    point.size = size;
    point.rotation = rotation;

    updatePoint(index, point);
}

void Points::updatePoint(size_t index, Vector3 position, Vector4 color, float size, float rotation, Rect textureRect){
    PointData point = getPoint(index);

    point.position = position;
    point.color = color;
    point.size = size;
    point.rotation = rotation;
    point.textureRect = textureRect;

    updatePoint(index, point);
}

void Points::removePoint(size_t index){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.points.erase(pointscomp.points.begin() + index);

    pointscomp.needUpdate = true;
}

bool Points::isPointVisible(size_t index){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    return pointscomp.points.at(index).visible;
}

void Points::setPointVisible(size_t index, bool visible) const{
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    if (pointscomp.points.at(index).visible != visible){
        pointscomp.points.at(index).visible = visible;

        pointscomp.needUpdate = true;
    }
}

void Points::updatePoints(){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.needUpdate = true;
}

size_t Points::getNumPoints(){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    return pointscomp.points.size();
}

void Points::clearPoints(){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    pointscomp.points.clear();

    pointscomp.needUpdate = true;
}

void Points::setTexture(const std::string& path){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.texture.setPath(path);

    pointscomp.needUpdateTexture = true;
}

void Points::setTexture(const std::string& id, TextureData data){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.texture.setData(id, data);

    pointscomp.needUpdateTexture = true;
}

void Points::setTexture(Framebuffer* framebuffer){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.texture.setFramebuffer(framebuffer);

    pointscomp.needUpdateTexture = true;
}

void Points::setTransparent(bool transparent){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.transparent = transparent;
}

bool Points::isTransparent() const{
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    return pointscomp.transparent;
}

void Points::setAutoTransparency(bool autoTransparency){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    pointscomp.autoTransparency = autoTransparency;
}

bool Points::isAutoTransparency() const{
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    return pointscomp.autoTransparency;
}

void Points::addSpriteFrame(int id, const std::string& name, Rect rect){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    if (pointscomp.framesRect.validIndex(id)){
        pointscomp.framesRect[id] = {name, rect};
        if ((unsigned int)(id + 1) > pointscomp.numFramesRect){
            pointscomp.numFramesRect = id + 1;
        }
        if (!pointscomp.hasTextureRect){
            pointscomp.hasTextureRect = true;
            pointscomp.needReload = true;
        }
    }else{
        Log::error("Cannot set frame id %s less than 0 or greater than %i", name.c_str(), pointscomp.framesRect.size());
    }
}

void Points::addSpriteFrame(const std::string& name, float x, float y, float width, float height){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    int id = (int)pointscomp.numFramesRect;

    if (pointscomp.framesRect.validIndex(id)){
        addSpriteFrame(id, name, Rect(x, y, width, height));
    }else{
        Log::error("Cannot set frame %s. Sprite frames reached limit of %i", name.c_str(), pointscomp.framesRect.size());
    }
}

void Points::addSpriteFrame(float x, float y, float width, float height){
    addSpriteFrame("", x, y, width, height);
}

void Points::addSpriteFrame(Rect rect){
    addSpriteFrame(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight());
}

void Points::removeSpriteFrame(int id){
    PointsComponent& pointscomp = getComponent<PointsComponent>();
    if (id >= 0 && (unsigned int)id < pointscomp.numFramesRect){
        for (unsigned int i = id; i + 1 < pointscomp.numFramesRect; i++){
            pointscomp.framesRect[i] = pointscomp.framesRect[i + 1];
        }
        pointscomp.numFramesRect--;
        pointscomp.framesRect[pointscomp.numFramesRect] = SpriteFrameData{};

        if (pointscomp.numFramesRect == 0 && pointscomp.hasTextureRect){
            pointscomp.hasTextureRect = false;
            pointscomp.needReload = true;
        }
    }
}

void Points::removeSpriteFrame(const std::string& name){
    PointsComponent& pointscomp = getComponent<PointsComponent>();

    for (int id = (int)pointscomp.numFramesRect - 1; id >= 0; id--){
        if (pointscomp.framesRect[id].name == name){
            removeSpriteFrame(id);
        }
    }
}
