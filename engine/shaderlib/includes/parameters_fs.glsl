// The fragment stage's declared parameters, as parameters_vs.glsl
// (mesh.frag keeps them in its material block)
#ifndef SHADER_PARAMETERS_FS
#define SHADER_PARAMETERS_FS

#include "shader_params_fs.glsl"

#ifdef HAS_SHADER_PARAMS_FS
    uniform u_fs_params {
        SHADER_PARAMS_FS_MEMBERS
    } paramsFS;

    void loadShaderParameters(){
        SHADER_PARAMS_FS_LOAD(paramsFS)
    }
#else
    void loadShaderParameters(){}
#endif

#endif
