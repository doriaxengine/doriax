// Lines along silhouettes and creases of 3D scenes, and along color changes if asked
#pragma shader postprocess

#pragma stage fragment
#pragma param vec3 lineColor = 0, 0, 0 color "Color of the lines"
#pragma param float thickness = 2 [2, 8] "Line width, in pixels of a 1080-line view"
#pragma param float depthSensitivity = 0.5 [0, 1] "How readily silhouettes and creases get lines; 0 turns them off"
#pragma param float colorSensitivity = 0 [0, 1] "How readily color changes get lines, as in 2D; 0 turns them off"
#pragma param float fadeDistance = 0 [0, 1000] "Distance where the lines are gone, in world units; 0 keeps them at any distance"
#include "effects/outline.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    return outline(context.uv, color, params.lineColor, params.thickness, params.depthSensitivity, params.colorSensitivity, params.fadeDistance);
}
