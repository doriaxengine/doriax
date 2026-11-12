// Fragment hooks of UI, points and lines, on their color before it is written
#include "includes/parameters_fs.glsl"

struct FragmentContext {
    vec3 worldPosition;
    vec2 uv1;             // a point's coordinate after its rotation and rect; 0 on lines
    vec4 color;           // vertex color, 1 without
    vec4 textureColor;    // the texture or font sample, linear; 1 without a texture
    vec4 tint;            // the UI color, 1 on points and lines
    vec2 pixel;
};

// the fragment stage of the shader file, mapped in by the shader builder
#include "shader_hooks_fs.glsl"

// always 1; keeps unused inputs alive so the stages pair
float keepHookInputs(FragmentContext context){
    return (context.worldPosition.x < -3.0e38 || context.uv1.x < -3.0e38) ? 0.0 : 1.0;
}
