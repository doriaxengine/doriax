#include "CommandHistory.h"
#include <stdio.h>

using namespace doriax;

editor::CommandHistory::~CommandHistory(){
    for (int i = 0; i < list.size(); i++){
        delete list[i];
    }
}

void editor::CommandHistory::addCommand(editor::Command* cmd){
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
    }else{
        delete cmd;
    }
}

void editor::CommandHistory::addCommandNoMerge(Command* cmd){
    cmd->setNoMerge();
    addCommand(cmd);
}

void editor::CommandHistory::undo(){
    if (index > 0){
        list[index-1]->undo();
        index--;

        #ifdef _DEBUG
        printf("DEBUG: undo (%zu from %zu)\n", index, list.size());
        #endif
    }
}

void editor::CommandHistory::redo(){
    if (index < list.size()){
        index++;
        list[index-1]->execute();

        #ifdef _DEBUG
        printf("DEBUG: redo (%zu from %zu)\n", index, list.size());
        #endif
    }
}

bool editor::CommandHistory::canUndo() const{
    return index > 0;
}

bool editor::CommandHistory::canRedo() const{
    return index < list.size();
}

void editor::CommandHistory::clear(){
    for (int i = 0; i < list.size(); i++){
        delete list[i];
    }
    list.clear();
    index = 0;
}