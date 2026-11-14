// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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

    auto renderSystem = scene->getSystem<RenderSystem>();
    return renderSystem->loadLines(entity, linescomp, renderSystem->getScenePipelines());
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

void Lines::setShader(const std::string& path){
    LinesComponent& lines = getComponent<LinesComponent>();

    if (lines.shader != path){
        lines.shader = path;

        lines.needReload = true;
    }
}

std::string Lines::getShader() const{
    LinesComponent& lines = getComponent<LinesComponent>();

    return lines.shader;
}

void Lines::setShaderUniform(const std::string& name, const Vector4& value){
    LinesComponent& lines = getComponent<LinesComponent>();

    ShaderUniforms::set(lines.shaderUniforms, name, value);
    lines.needUpdateShaderUniforms = true;
}

void Lines::setShaderUniform(const std::string& name, const Vector3& value){
    setShaderUniform(name, Vector4(value.x, value.y, value.z, 0.0f));
}

void Lines::setShaderUniform(const std::string& name, const Vector2& value){
    setShaderUniform(name, Vector4(value.x, value.y, 0.0f, 0.0f));
}

void Lines::setShaderUniform(const std::string& name, float value){
    setShaderUniform(name, Vector4(value, 0.0f, 0.0f, 0.0f));
}

Vector4 Lines::getShaderUniform(const std::string& name) const{
    LinesComponent& lines = getComponent<LinesComponent>();

    return ShaderUniforms::get(lines.shaderUniforms, name);
}

bool Lines::removeShaderUniform(const std::string& name){
    LinesComponent& lines = getComponent<LinesComponent>();

    if (!ShaderUniforms::remove(lines.shaderUniforms, name))
        return false;

    lines.needUpdateShaderUniforms = true;
    return true;
}

void Lines::setShaderTexture(const std::string& name, const Texture& texture){
    LinesComponent& lines = getComponent<LinesComponent>();

    const Texture* current = ShaderTextures::find(lines.shaderTextures, name);
    if (current && *current == texture)
        return;

    ShaderTextures::set(lines.shaderTextures, name, texture);
    lines.needReload = true;
}

Texture Lines::getShaderTexture(const std::string& name) const{
    LinesComponent& lines = getComponent<LinesComponent>();

    return ShaderTextures::get(lines.shaderTextures, name);
}