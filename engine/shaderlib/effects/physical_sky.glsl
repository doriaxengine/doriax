// Sunlight scattered by the air, after Hillaire's atmosphere model with the sun's transmittance in closed form
// lengths in km, scattering and absorption per km
const float ATMOSPHERE_GROUND = 6360.0;
const float ATMOSPHERE_TOP = 6460.0;
const float ATMOSPHERE_RAYLEIGH_HEIGHT = 8.0;
const float ATMOSPHERE_MIE_HEIGHT = 1.2;
// the ozone layer's density is a tent: its peak height and half width
const float ATMOSPHERE_OZONE_HEIGHT = 25.0;
const float ATMOSPHERE_OZONE_WIDTH = 15.0;
// the sun's disc: its angular radius and brightness over the sunlight's
const float ATMOSPHERE_SUN_RADIUS = 0.0047;
const float ATMOSPHERE_SUN_BRIGHTNESS = 10.0;

struct Atmosphere {
    vec3 rayleigh;       // air scattering at the ground
    float mie;           // haze scattering at the ground
    float mieExtinction; // and what it scatters or absorbs
    float mieGlow;       // how much the haze scatters forward
    vec3 ozone;          // ozone absorption at its densest
    vec3 ground;         // the planet's albedo
};

// the Earth's air with its parts scaled; airColor tints what the air scatters
Atmosphere earthAtmosphere(vec3 airColor, float airDensity, float haze, float hazeGlow, float ozone, vec3 groundColor){
    Atmosphere air;
    air.rayleigh = 33.1e-3 * airColor * airDensity;
    air.mie = 3.996e-3 * haze;
    air.mieExtinction = 8.396e-3 * haze;
    air.mieGlow = hazeGlow;
    air.ozone = vec3(0.65e-3, 1.881e-3, 0.085e-3) * ozone;
    air.ground = groundColor;
    return air;
}

// Schüler's approximation of the Chapman function: the air on a ray to space over the air straight up
float atmosphereChapman(float x, float mu){
    float c = sqrt(1.5707963 * x);
    return c / ((c - 1.0) * mu + 1.0);
}

// the air of a layer of scale height h on the ray from radius r at zenith cosine mu, in km at ground density
float atmosphereDepth(float r, float mu, float h){
    float upward = h * atmosphereChapman(r / h, abs(mu)) * exp((ATMOSPHERE_GROUND - r) / h);
    if (mu >= 0.0)
        return upward;
    // down to the ray's lowest point and up again
    float lowest = r * sqrt(1.0 - mu * mu);
    return 2.0 * h * atmosphereChapman(lowest / h, 0.0) * exp((ATMOSPHERE_GROUND - lowest) / h) - upward;
}

// the ozone layer as a shell as thick as its tent, crossed on the slant
float atmosphereOzoneDepth(float r, float mu){
    float s = r * sqrt(1.0 - mu * mu) / (ATMOSPHERE_GROUND + ATMOSPHERE_OZONE_HEIGHT);
    return ATMOSPHERE_OZONE_WIDTH / sqrt(max(1.0 - s * s, 0.01));
}

// the sunlight reaching radius r from zenith cosine mu, through the air and past the planet
vec3 atmosphereTransmittance(Atmosphere air, float r, float mu){
    float lowest = mu < 0.0 ? r * sqrt(1.0 - mu * mu) : r;
    if (lowest < ATMOSPHERE_GROUND - 1.0)
        return vec3(0.0);
    vec3 depth = air.rayleigh * atmosphereDepth(r, mu, ATMOSPHERE_RAYLEIGH_HEIGHT) +
        air.mieExtinction * atmosphereDepth(r, mu, ATMOSPHERE_MIE_HEIGHT) + air.ozone * atmosphereOzoneDepth(r, mu);
    // the planet's shadow, soft as the sun's disc
    return exp(-depth) * smoothstep(-1.0, 1.0, lowest - ATMOSPHERE_GROUND);
}

float atmosphereMiePhase(float nu, float g){
    float g2 = g * g;
    return 0.1193662 * (1.0 - g2) * (1.0 + nu * nu) / ((2.0 + g2) * pow(1.0 + g2 - 2.0 * g * nu, 1.5));
}

// the eye's radius, from altitude km up
float atmosphereEye(float altitude){
    return ATMOSPHERE_GROUND + clamp(altitude, 0.001, 99.0);
}

// the air and the ground from direction for each unit of sunColor, with the sun toward sunDirection, seen from altitude km up
vec3 physicalSky(Atmosphere air, vec3 direction, vec3 sunDirection, float altitude, int samples){
    vec3 d = normalize(direction);
    float r = atmosphereEye(altitude);
    float mu = d.y;
    float muSun = sunDirection.y;
    float nu = dot(d, sunDirection);

    // to the ground when the view meets it, else out of the air
    float groundHit = r * r * (mu * mu - 1.0) + ATMOSPHERE_GROUND * ATMOSPHERE_GROUND;
    bool ground = mu < 0.0 && groundHit >= 0.0;
    float far = ground ? -r * mu - sqrt(groundHit) : -r * mu + sqrt(r * r * (mu * mu - 1.0) + ATMOSPHERE_TOP * ATMOSPHERE_TOP);

    float rayleighPhase = 0.0596831 * (1.0 + nu * nu);
    float miePhase = atmosphereMiePhase(nu, air.mieGlow);
    // light scattered many times comes from all around: the share a column of air scatters, and the lit ground below
    vec3 column = 1.0 - exp(-(air.rayleigh * ATMOSPHERE_RAYLEIGH_HEIGHT + air.mie * ATMOSPHERE_MIE_HEIGHT));
    vec3 isotropic = 0.0795775 * (column + 2.0 * air.ground * max(muSun, 0.0));

    vec3 light = vec3(0.0);
    vec3 depth = vec3(0.0);
    float n = float(samples);
    for (int i = 0; i < samples; i++){
        // closer together near the eye, where the air is
        float t0 = far * (float(i) / n) * (float(i) / n);
        float t1 = far * (float(i + 1) / n) * (float(i + 1) / n);
        float t = 0.5 * (t0 + t1);
        float dt = t1 - t0;
        float radius = sqrt(r * r + t * t + 2.0 * r * mu * t);
        float height = radius - ATMOSPHERE_GROUND;

        vec3 rayleigh = air.rayleigh * exp(-height / ATMOSPHERE_RAYLEIGH_HEIGHT);
        float mieDensity = exp(-height / ATMOSPHERE_MIE_HEIGHT);
        float mie = air.mie * mieDensity;
        float ozoneDensity = max(1.0 - abs(height - ATMOSPHERE_OZONE_HEIGHT) / ATMOSPHERE_OZONE_WIDTH, 0.0);
        vec3 extinction = rayleigh + air.mieExtinction * mieDensity + air.ozone * ozoneDensity;

        vec3 sun = atmosphereTransmittance(air, radius, (r * muSun + t * nu) / radius);
        vec3 scattered = (rayleigh * rayleighPhase + mie * miePhase + (rayleigh + mie) * isotropic) * sun;
        // seen through the air before it, half this step included
        light += exp(-(depth + 0.5 * extinction * dt)) * scattered * dt;
        depth += extinction * dt;
    }

    if (ground){
        // the planet, lit by the sun and the sky
        float groundSun = (r * muSun + far * nu) / ATMOSPHERE_GROUND;
        vec3 sun = atmosphereTransmittance(air, ATMOSPHERE_GROUND, groundSun);
        light += exp(-depth) * air.ground * 0.3183099 * sun * (max(groundSun, 0.0) + column);
    }
    return light;
}

// the sun's disc through the air, darker toward its limb, for each unit of sunColor; sunSize 1 is its real size
vec3 physicalSun(Atmosphere air, vec3 direction, vec3 sunDirection, float sunSize, float altitude){
    if (sunSize <= 0.0)
        return vec3(0.0);
    vec3 d = normalize(direction);
    float angle = atan(length(cross(d, sunDirection)), dot(d, sunDirection));
    float radius = ATMOSPHERE_SUN_RADIUS * sunSize;
    float edge = fwidth(angle);
    float disc = 1.0 - smoothstep(radius - edge, radius + edge, angle);
    float limb = sqrt(max(1.0 - angle * angle / (radius * radius), 0.0));
    // the planet hides it below the horizon
    return atmosphereTransmittance(air, atmosphereEye(altitude), d.y) * disc * (0.4 + 0.6 * limb) * ATMOSPHERE_SUN_BRIGHTNESS;
}
