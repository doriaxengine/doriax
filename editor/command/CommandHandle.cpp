#include "CommandHandle.h"

using namespace doriax;

std::map<size_t, editor::CommandHistory*>  editor::CommandHandle::historys;


editor::CommandHistory* editor::CommandHandle::get(size_t sceneId){
    if (!historys.count(sceneId)){
        historys[sceneId] = new CommandHistory();
    }
    return historys[sceneId];
}

void editor::CommandHandle::remove(size_t sceneId){
    if (historys.count(sceneId)){
        delete historys[sceneId];
        historys.erase(sceneId);
    }
}