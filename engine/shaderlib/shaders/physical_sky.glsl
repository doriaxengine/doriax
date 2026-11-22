// Sunlight scattered by the air: blue days, red sunsets and twilight, from the sun of the scene's light
#pragma shader sky

#pragma stage fragment
#pragma param float intensity = 2 [0, 16] "Brightness for the light's intensity, which also lights the scene"
#pragma param vec3 airColor = 0.36, 0.6, 1 color "What the air scatters: the blue of the day, and the red it leaves at sunset"
#pragma param float airDensity = 1 [0, 10] "Amount of air; more is deeper blue and redder sunsets"
#pragma param float haze = 1 [0, 50] "Dust and water in the air: a paler sky and a glow around the sun"
#pragma param float hazeGlow = 0.8 [0, 0.99] "How tight the glow around the sun is"
#pragma param float ozone = 1 [0, 10] "The ozone layer, which keeps twilight blue"
#pragma param vec3 groundColor = 0.45, 0.42, 0.38 color "The planet below the horizon, which also brightens the sky"
#pragma param float sunSize = 1 [0, 20] "The sun's disc, 1 its real size; 0 hides it"
#pragma param float altitude = 0.1 [0, 50] "Height of the eye above the ground, in km"
#pragma param int samples = 16 [4, 64] "Steps along each view of the capture; more is smoother near the horizon and slower"
#include "effects/physical_sky.glsl"

vec4 getColor(FragmentContext context, vec4 color){
    Atmosphere air = earthAtmosphere(sRGBToLinear(params.airColor), params.airDensity, params.haze, params.hazeGlow,
        params.ozone, sRGBToLinear(params.groundColor));
    vec3 light = context.sunColor * params.intensity;
    // the air is marched only into the capture that lights the scene; the view reads it back and adds the sun
    if (context.capturing)
        return vec4(physicalSky(air, context.direction, context.sunDirection, params.altitude, params.samples) * light, 1.0) * context.tint;
    vec3 sun = physicalSun(air, context.direction, context.sunDirection, params.sunSize, params.altitude);
    return skyCapture(context.direction) + vec4(sun * light, 0.0) * context.tint;
}
