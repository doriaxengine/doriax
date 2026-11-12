// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "command/Command.h"
#include "util/ProjectUtils.h"
#include "ecs/Entity.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace doriax::editor {

    // Forks a built-in shader and points a mesh's customShader (customDepthShader
    // for a DEPTH fork, customUnderwaterShader for an UNDERWATER fork, or a scene's default
    // shader property) at the new fork as a single
    // undoable step: execute() writes the .vert/.frag (and optional private include) files
    // and sets the property; undo() restores the property and deletes the forked files.
    class ForkShaderCmd : public Command {
    private:
        Project* project;
        ProjectUtils::ShaderForkPlan plan;
        std::unique_ptr<Command> propertyCmd;  // sets customShader (or scene property) to plan.base
        bool executed = false;

    public:
        ForkShaderCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType cpType,
                      ShaderType shaderType, const std::filesystem::path& targetDirRel,
                      const std::string& baseName, bool forkIncludes = false);

        // scene-level variant: sets a scene default shader property (e.g. "default_mesh_shader")
        ForkShaderCmd(Project* project, SceneProject* sceneProject, ShaderType shaderType,
                      const std::string& scenePropertyName,
                      const std::filesystem::path& targetDirRel,
                      const std::string& baseName, bool forkIncludes = false);

        // generic variant: the caller builds the command that points a property at the
        // fork, given its base path (the post-process shader lives inside a list value)
        ForkShaderCmd(Project* project, ShaderType shaderType,
                      const std::function<std::unique_ptr<Command>(const std::string&)>& makePropertyCmd,
                      const std::filesystem::path& targetDirRel,
                      const std::string& baseName, bool forkIncludes = false);

        bool isValid() const { return plan.valid; }
        const std::string& getBase() const { return plan.base; }
        const std::string& getError() const { return plan.error; }

        bool execute() override;
        void undo() override;
        bool mergeWith(Command* otherCommand) override;
    };

}
