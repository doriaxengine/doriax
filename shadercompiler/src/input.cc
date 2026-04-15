//
// (c) 2021 Eduardo Doria.
//

#include "shadercompiler.h"

#include <iostream>
#include <fstream>
#include <string>

using namespace shadercompiler;

static bool load_string_from_file(std::string& buffer, std::string path){
    std::ifstream ifs (path);

  	if (ifs.is_open()){
        buffer.assign(std::istreambuf_iterator<char>(ifs) ,std::istreambuf_iterator<char>());
    }else{
        fprintf(stderr, "Unable to open file: %s\n", path.c_str());
        return false;
    }

    return true;
}

bool shadercompiler::load_input(std::vector<input_t>& inputs, const args_t& args){

    std::string buffer;

    if (!args.vert_file.empty()){
        if (args.useBuffers){
            if (args.fileBuffers.find(args.vert_file) == args.fileBuffers.end())
                return false;
            buffer = args.fileBuffers.at(args.vert_file);
        }else{
            if (!load_string_from_file(buffer, args.vert_file))
                return false;
        }

        inputs.push_back({STAGE_VERTEX, args.vert_file, buffer});
    }
    
    if (!args.frag_file.empty()){
        if (args.useBuffers){
            if (args.fileBuffers.find(args.frag_file) == args.fileBuffers.end())
                return false;
            buffer = args.fileBuffers.at(args.frag_file);
        }else{
            if (!load_string_from_file(buffer, args.frag_file))
                return false;
        }

        inputs.push_back({STAGE_FRAGMENT, args.frag_file, buffer});
    }

    return true;
}