// The vertex stage's declared parameters; loadShaderParameters() copies them to params.name and Collection.name
#ifndef SHADER_PARAMETERS_VS
#define SHADER_PARAMETERS_VS

#include "shader_params_vs.glsl"

#ifdef HAS_SHADER_PARAMS_VS
    uniform u_vs_params {
        SHADER_PARAMS_VS_MEMBERS
    } paramsVS;

    void loadShaderParameters(){
        SHADER_PARAMS_VS_LOAD(paramsVS)
    }
#else
    void loadShaderParameters(){}
#endif

#endif
