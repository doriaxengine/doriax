// Fragment hooks of water: getColor on the surface, getUnderwaterColor on the fade a camera
// below it sees. One shader file fills both, so both programs know both contexts.

struct FragmentContext {
    vec3 worldPosition;
    vec3 normal;          // the rippled surface, world space
    vec3 view;            // toward the eye
    vec2 pixel;
};

struct UnderwaterContext {
    vec2 uv;              // of the view, 0 at the top left
    vec4 sceneColor;      // what the camera sees before the fade, linear
    float pathLength;     // water the view crosses, in world units
    vec2 pixel;
};

// the fragment stage of the shader file, mapped in by the shader builder
#include "shader_hooks_fs.glsl"
