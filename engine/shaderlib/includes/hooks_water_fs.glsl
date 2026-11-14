// Fragment hooks of water: getColor and getUnderwaterColor

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

// the shader file's fragment stage
#include "shader_hooks_fs.glsl"
