#include "shadercompiler.h"

using namespace shadercompiler;

args_t shadercompiler::initialize_args(){
    args_t args;
    args.useBuffers = false;
    args.fileBuffers.clear();
    args.vert_file = "";
    args.frag_file = "";
    args.lang = LANG_GLSL;
    args.version = 0;
    args.es = false;
    args.platform = SHADER_DEFAULT;
    args.output_basename = "";
    args.output_dir = "";
    args.output_type = OUTPUT_JSON;
    args.include_dir = "";
    args.defines.clear();
    args.list_includes = false;
    args.optimization = true;

    return args;
}