// Bends the mesh away from a point, such as grass a character walks through. A script
// writes the point into the scene's Interaction parameters every frame.
// @scene vec3 Interaction.position "World position the meshes bend away from"
// @scene float Interaction.radius = 0 [0, 20] "Reach of the push; 0 turns it off"
// @param float pushStrength = 0.5 [0, 5] "How far the top of the mesh moves away, in world units"
// @param float pushHeight = 1 [0, 50] "Model height of the full push; 0 moves the whole mesh"

vec3 getVertexOffset(vec3 localPosition, vec3 worldPosition){
    vec3 away = worldPosition - Interaction.position;
    away.y = 0.0;
    float distanceAway = length(away);
    if (Interaction.radius <= 0.0 || distanceAway >= Interaction.radius)
        return vec3(0.0);

    float push = 1.0 - distanceAway / Interaction.radius;
    push *= push;
    float bend = 1.0;
    if (params.pushHeight > 0.0){
        bend = clamp(localPosition.y / params.pushHeight, 0.0, 1.0);
        bend *= bend;
    }

    vec3 direction = distanceAway > 0.0001 ? away / distanceAway : vec3(1.0, 0.0, 0.0);
    float amount = params.pushStrength * push * bend;
    // a pushed tip also drops, so the mesh keeps about its length
    return direction * amount - vec3(0.0, amount * 0.5, 0.0);
}
