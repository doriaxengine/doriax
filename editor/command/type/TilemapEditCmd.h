// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "command/Command.h"
#include "Project.h"
#include "component/TilemapComponent.h"

#include <cstdint>
#include <vector>

namespace doriax::editor{

    struct TileRectCollisionChange{
        int rectIndex = -1;
        bool before = true;
        bool after = true;
    };

    class TilemapTilesCmd : public Command{
    private:
        Project* project;
        uint32_t sceneId;
        Entity entity;
        std::vector<TileData> beforeTiles;
        std::vector<TileData> afterTiles;
        bool wasModified;

        void apply(const std::vector<TileData>& targetTiles, bool restoreModifiedState);

    public:
        TilemapTilesCmd(Project* project, uint32_t sceneId, Entity entity,
                        std::vector<TileData> beforeTiles,
                        std::vector<TileData> afterTiles,
                        bool wasModifiedBefore);

        bool execute() override;
        void undo() override;
        bool affectsStructure() const override { return false; }
        bool mergeWith(Command* otherCommand) override;
    };

    class TilemapRectCollisionCmd : public Command{
    private:
        Project* project;
        uint32_t sceneId;
        Entity entity;
        std::vector<TileRectCollisionChange> changes;
        bool wasModified;

        void apply(bool useAfter, bool restoreModifiedState);

    public:
        TilemapRectCollisionCmd(Project* project, uint32_t sceneId, Entity entity,
                                std::vector<TileRectCollisionChange> changes,
                                bool wasModifiedBefore);

        bool execute() override;
        void undo() override;
        bool affectsStructure() const override { return false; }
        bool mergeWith(Command* otherCommand) override;
    };

}
