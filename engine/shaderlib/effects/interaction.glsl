// The push of a vertex away from the interaction point
#pragma scene vec3 Interaction.position "World position the meshes bend away from"
#pragma scene float Interaction.radius = 0 [0, 20] "Reach of the push; 0 turns it off"
vec3 interactionPush(vec3 localPosition, vec3 worldPosition, float strength, float height){
    vec3 away = worldPosition - Interaction.position;
    away.y = 0.0;
    float distanceAway = length(away);
    if (Interaction.radius <= 0.0 || distanceAway >= Interaction.radius)
        return vec3(0.0);

    float push = 1.0 - distanceAway / Interaction.radius;
    push *= push;
    float bend = 1.0;
    if (height > 0.0){
        bend = clamp(localPosition.y / height, 0.0, 1.0);
        bend *= bend;
    }

    vec3 direction = distanceAway > 0.0001 ? away / distanceAway : vec3(1.0, 0.0, 0.0);
    float amount = strength * push * bend;
    // a pushed tip also drops, so the mesh keeps about its length
    return direction * amount - vec3(0.0, amount * 0.5, 0.0);
}
