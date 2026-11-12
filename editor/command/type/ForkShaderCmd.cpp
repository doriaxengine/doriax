// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ForkShaderCmd.h"

#include "command/type/PropertyCmd.h"
#include "command/type/ScenePropertyCmd.h"

#include <filesystem>

using namespace doriax;

editor::ForkShaderCmd::ForkShaderCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType cpType,
                                     ShaderType shaderType, const std::filesystem::path& targetDirRel,
                                     const std::string& baseName, bool forkIncludes) {
    this->project = project;
    this->plan = ProjectUtils::prepareShaderFork(project, shaderType, targetDirRel, baseName, forkIncludes);

    if (plan.valid) {
        // a mesh keeps its depth fork in a property of its own, and a water its underwater fork
        const char* propertyName = "customShader";
        if (shaderType == ShaderType::DEPTH)
            propertyName = "customDepthShader";
        propertyCmd = std::make_unique<PropertyCmd<std::string>>(
            project, sceneId, entity, cpType, propertyName, plan.base);
    }
}

editor::ForkShaderCmd::ForkShaderCmd(Project* project, SceneProject* sceneProject, ShaderType shaderType,
                                     const std::string& scenePropertyName,
                                     const std::filesystem::path& targetDirRel,
                                     const std::string& baseName, bool forkIncludes) {
    this->project = project;
    this->plan = ProjectUtils::prepareShaderFork(project, shaderType, targetDirRel, baseName, forkIncludes);

    if (!sceneProject) {
        plan.valid = false;
        if (plan.error.empty())
            plan.error = "Scene is unavailable.";
    } else if (plan.valid) {
        propertyCmd = std::make_unique<ScenePropertyCmd<std::string>>(
            project, sceneProject->id, scenePropertyName, plan.base);
    }
}

bool editor::ForkShaderCmd::execute() {
    executed = false;
    if (!plan.valid)
        return false;

    if (!ProjectUtils::writeShaderFork(plan))
        return false;

    if (propertyCmd && !propertyCmd->execute()) {
        ProjectUtils::removeShaderFork(plan);
        return false;
    }

    project->invalidateCustomShaders();
    executed = true;
    return true;
}

void editor::ForkShaderCmd::undo() {
    if (!executed)
        return;

    if (propertyCmd)
        propertyCmd->undo();

    ProjectUtils::removeShaderFork(plan);
    project->invalidateCustomShaders();
    executed = false;
}

bool editor::ForkShaderCmd::mergeWith(Command* otherCommand) {
    return false;
}
