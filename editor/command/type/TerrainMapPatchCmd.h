// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include "command/Command.h"
#include "Project.h"
#include "util/TerrainMapUtils.h"

#include <cstdint>
#include <vector>

namespace doriax::editor{

    // Stroke undo: restores one rect of a terrain map, so the history holds a patch instead of
    // a copy of the whole map. The file is still rewritten in full on every apply, since PNG
    // has no partial write.
    class TerrainMapPatchCmd: public Command{

    private:
        Project* project;
        uint32_t sceneId;
        Entity entity;
        TerrainMapRef ref;
        TerrainMapPatch patch;

        bool wasModified = false;

        void apply(const std::vector<unsigned char>& regionPixels, bool restoreModifiedState);

    public:
        TerrainMapPatchCmd(Project* project, uint32_t sceneId, Entity entity, const TerrainMapRef& ref, TerrainMapPatch patch);

        bool execute() override;
        void undo() override;

        bool mergeWith(Command* otherCommand) override;
    };

}
