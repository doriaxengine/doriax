// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "GraphEditCmd.h"

using namespace doriax;

editor::GraphEditCmd::GraphEditCmd(std::function<void(const std::string&)> apply, std::string before, std::string after, std::string key){
    this->apply = std::move(apply);
    this->before = std::move(before);
    this->after = std::move(after);
    this->key = std::move(key);
    this->firstRun = true;
}

bool editor::GraphEditCmd::execute(){
    if (!firstRun){
        apply(after);
    }
    firstRun = false;
    return true;
}

void editor::GraphEditCmd::undo(){
    apply(before);
}

bool editor::GraphEditCmd::mergeWith(editor::Command*){
    return false;
}

bool editor::GraphEditCmd::extend(const std::string& key, const std::string& after){
    if (!canMerge() || key.empty() || key != this->key){
        return false;
    }
    this->after = after;
    return true;
}
