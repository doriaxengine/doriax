// Bends the mesh away from a point, such as grass a character walks through. A script
// writes the point into the scene's Interaction parameters every frame.
#pragma shader mesh

#pragma stage vertex
#pragma param float pushStrength = 0.5 [0, 5] "How far the top of the mesh moves away, in world units"
#pragma param float pushHeight = 1 [0, 50] "Model height of the full push; 0 moves the whole mesh"
#include "effects/interaction.glsl"

vec3 getVertexOffset(VertexContext context){
    return interactionPush(context.localPosition, context.worldPosition, params.pushStrength, params.pushHeight);
}
