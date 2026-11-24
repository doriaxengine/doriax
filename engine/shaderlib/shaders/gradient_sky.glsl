// A sky of three colors meeting at the horizon, with the sun of the scene's light
#pragma shader sky

#pragma stage fragment
#pragma param vec3 zenithColor = 0.2, 0.4, 0.75 color "Straight up"
#pragma param vec3 horizonColor = 0.65, 0.74, 0.84 color "At the horizon"
#pragma param vec3 groundColor = 0.28, 0.25, 0.22 color "Straight down"
#pragma param float skyCurve = 0.5 [0.01, 4] "How soon the horizon color gives way to the zenith one; lower is a thinner band"
#pragma param float groundCurve = 0.1 [0.01, 4] "The same below the horizon"
#pragma param float sunSize = 2 [0, 20] "The sun's disc across, in degrees; 0 hides it"
#pragma param float sunGlow = 0.3 [0, 4] "Strength of the glow around the sun"
#pragma param float sunGlowSize = 30 [1, 180] "How far the glow reaches, in degrees"
#pragma param float energy = 1 [0, 16] "Brightness; above 1 lights the scene more, and blooms with HDR"
#include "effects/gradient_sky.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    vec3 sky = gradientSky(context.direction, context.sunDirection, context.sunColor,
        sRGBToLinear(params.zenithColor), sRGBToLinear(params.horizonColor), sRGBToLinear(params.groundColor),
        params.skyCurve, params.groundCurve, params.sunSize, params.sunGlow, params.sunGlowSize);
    return vec4(sky * params.energy, 1.0) * context.tint;
}
