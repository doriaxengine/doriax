//
// (c) 2026 Eduardo Doria.
//

#include "Lines.h"
#include "subsystem/RenderSystem.h"

using namespace doriax;

Lines::Lines(Scene* scene): Object(scene){
    addComponent<LinesComponent>();
}

Lines::Lines(Scene* scene, Entity entity): Object(scene, entity){
}

Lines::~Lines(){

}

bool Lines::load(){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    return scene->getSystem<RenderSystem>()->loadLines(entity, linescomp, PIP_DEFAULT | PIP_RTT);
}

void Lines::setMaxLines(unsigned int maxLines){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    if (linescomp.maxLines != maxLines){
        linescomp.maxLines = maxLines;

        linescomp.needReload = true;
    }
}

unsigned int Lines::getMaxLines() const{
    LinesComponent& linescomp = getComponent<LinesComponent>();

    return linescomp.maxLines;
}

void Lines::addLine(LineData line){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    linescomp.lines.push_back(line);

    if (linescomp.maxLines < linescomp.lines.size()){
        linescomp.maxLines = linescomp.maxLines * 2;
        linescomp.needReload = true;
    }else{
        linescomp.needUpdateBuffer = true;
    }
}

void Lines::addLine(Vector3 pointA, Vector3 pointB){
    LineData line = {};

    line.pointA = pointA;
    line.pointB = pointB;

    addLine(line);
}

void Lines::addLine(Vector3 pointA, Vector3 pointB, Vector3 color){
    LineData line = {};

    line.pointA = pointA;
    line.pointB = pointB;
    line.colorA = Vector4(color, line.colorA.z);
    line.colorB = Vector4(color, line.colorB.z);

    addLine(line);
}

void Lines::addLine(Vector3 pointA, Vector3 pointB, Vector4 color){
    LineData line = {};

    line.pointA = pointA;
    line.pointB = pointB;
    line.colorA = color;
    line.colorB = color;

    addLine(line);
}

void Lines::addLine(Vector3 pointA, Vector3 pointB, Vector4 colorA, Vector4 colorB){
    LineData line = {};

    line.pointA = pointA;
    line.pointB = pointB;
    line.colorA = colorA;
    line.colorB = colorB;

    addLine(line);
}

LineData& Lines::getLine(size_t index){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    return linescomp.lines.at(index);
}

void Lines::updateLine(size_t index, LineData line){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    if (linescomp.lines.at(index) != line){
        linescomp.lines.at(index) = line;

        linescomp.needUpdateBuffer = true;
    }
}

void Lines::updateLine(size_t index, Vector3 pointA, Vector3 pointB){
    LineData line = getLine(index);

    line.pointA = pointA;
    line.pointB = pointB;

    updateLine(index, line);
}

void Lines::updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector3 color){
    LineData line = getLine(index);

    line.pointA = pointA;
    line.pointB = pointB;
    line.colorA = Vector4(color, line.colorA.z);
    line.colorB = Vector4(color, line.colorB.z);

    updateLine(index, line);
}

void Lines::updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector4 color){
    LineData line = getLine(index);

    line.pointA = pointA;
    line.pointB = pointB;
    line.colorA = color;
    line.colorB = color;

    updateLine(index, line);
}

void Lines::updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector4 colorA, Vector4 colorB){
    LineData line = getLine(index);

    line.pointA = pointA;
    line.pointB = pointB;
    line.colorA = colorA;
    line.colorB = colorB;

    updateLine(index, line);
}

void Lines::updateLine(size_t index, Vector3 color){
    LineData line = getLine(index);

    line.colorA = Vector4(color, line.colorA.z);
    line.colorB = Vector4(color, line.colorB.z);

    updateLine(index, line);
}

void Lines::updateLine(size_t index, Vector4 color){
    LineData line = getLine(index);

    line.colorA = color;
    line.colorB = color;

    updateLine(index, line);
}

void Lines::updateLine(size_t index, Vector4 colorA, Vector4 colorB){
    LineData line = getLine(index);

    line.colorA = colorA;
    line.colorB = colorB;

    updateLine(index, line);
}

void Lines::removeLine(size_t index){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    linescomp.lines.erase(linescomp.lines.begin() + index);

    linescomp.needUpdateBuffer = true;
}

void Lines::updateLines(){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    linescomp.needUpdateBuffer = true;
}

size_t Lines::getNumLines(){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    return linescomp.lines.size();
}

void Lines::clearLines(){
    LinesComponent& linescomp = getComponent<LinesComponent>();

    if (linescomp.lines.size() > 0){
        linescomp.lines.clear();

        linescomp.needReload = true;
    }
}