// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "CommandHistory.h"
#include <stdio.h>

using namespace doriax;

std::function<void(size_t, bool)> editor::CommandHistory::onSceneModified = nullptr;

editor::CommandHistory::CommandHistory(size_t sceneId): sceneId(sceneId){
}

editor::CommandHistory::~CommandHistory(){
    for (int i = 0; i < list.size(); i++){
        delete list[i];
    }
}

bool editor::CommandHistory::addCommand(editor::Command* cmd){
    if (cmd->execute()){
        if (index < list.size()){
            for (auto it = list.begin() + index; it != list.end(); ++it) {
                delete *it;
            }

            list.erase(list.begin() + index, list.end());
        }

        if (list.size() > 0 && list.back()->canMerge() && cmd->canMerge()){
            if (cmd->mergeWith(list.back())){
                list.pop_back();
            }
        }

        list.push_back(cmd);
        index = list.size();

        if (onSceneModified){
            onSceneModified(sceneId, cmd->affectsStructure());
        }
        return true;
    }

    delete cmd;
    return false;
}

bool editor::CommandHistory::addCommandNoMerge(Command* cmd){
    cmd->setNoMerge();
    return addCommand(cmd);
}

bool editor::CommandHistory::undo(){
    if (index > 0 && list[index-1]->canUndo()){
        const bool structural = list[index-1]->affectsStructure();
        list[index-1]->undo();
        index--;

        if (onSceneModified){
            onSceneModified(sceneId, structural);
        }

        #ifdef _DEBUG
        printf("DEBUG: undo (%zu from %zu)\n", index, list.size());
        #endif
        return true;
    }
    return false;
}

bool editor::CommandHistory::redo(){
    if (index < list.size() && list[index]->canRedo()){
        index++;
        list[index-1]->execute();

        if (onSceneModified){
            onSceneModified(sceneId, list[index-1]->affectsStructure());
        }

        #ifdef _DEBUG
        printf("DEBUG: redo (%zu from %zu)\n", index, list.size());
        #endif
        return true;
    }
    return false;
}

bool editor::CommandHistory::canUndo() const{
    return index > 0;
}

bool editor::CommandHistory::canRedo() const{
    return index < list.size();
}

void editor::CommandHistory::forEach(const std::function<void(Command*)>& function) const{
    for (Command* command : list){
        function(command);
    }
}

void editor::CommandHistory::clear(){
    for (int i = 0; i < list.size(); i++){
        delete list[i];
    }
    list.clear();
    index = 0;
}