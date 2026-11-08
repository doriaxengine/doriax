uniform texture2D u_blendMap;
uniform texture2D u_blendMap1;
uniform texture2D u_blendMap2;
uniform texture2DArray u_terrainDetail;
uniform sampler u_blendMap_smp;
uniform sampler u_blendMap1_smp;
uniform sampler u_blendMap2_smp;
uniform sampler u_terrainDetail_smp;

in vec2 v_terrainTextureCoords;
in vec2 v_terrainTextureDetailTiled;
in float v_terrainDetailHeight;
in float v_terrainEyeTiles;
#ifdef HAS_NORMALS
    in vec3 v_terrainNormal;
#endif

// A coarser second tiling rate fades in over this many detail tiles of distance
const float TERRAIN_TILE_BREAK_NEAR = 6.0;
const float TERRAIN_TILE_BREAK_FAR = 40.0;
const float TERRAIN_TILE_BREAK_RATE = 0.371;
const float TERRAIN_TILE_BREAK_MIX = 0.5;

// Slope, as 1.0 - abs(normal.y), over which the side projections take over from the flat one
const float TERRAIN_TRIPLANAR_START = 0.15;
const float TERRAIN_TRIPLANAR_END = 0.6;

// How hard layer height biases the blend. 0.0 leaves the plain blend map weights
const float TERRAIN_HEIGHT_CONTRAST = 6.0;

// Each blend map weights three layers of the detail array in its RGB
const int TERRAIN_BLENDMAPS = 3;

#ifdef HAS_TERRAIN_PBR
    // One entry per layer. Every map a layer owns is a slice of the same detail array,
    // so a layer with no map of a kind stores -1 there and falls back to its factors.
    uniform u_fs_terrainLayers {
        vec4 colorFactor[MAX_TERRAIN_LAYERS]; //rgb tint (linear), a = 1.0 on a PBR layer
        vec4 uvTransform[MAX_TERRAIN_LAYERS]; //xy scale of the detail tiling, zw offset
        vec4 surface[MAX_TERRAIN_LAYERS];     //roughness, metallic, normal strength, occlusion strength
        vec4 slices[MAX_TERRAIN_LAYERS];      //color, normal and surface slice
    } terrainLayers;

    #ifdef HAS_NORMALS
        in vec3 v_terrainAxisX;
        in vec3 v_terrainAxisZ;
    #endif

    struct TerrainSurface{
        vec4 color;
        vec3 normal;    //in the space the pass shades in
        float roughness;
        float metallic;
        float occlusion;
    };
#endif

vec2 terrainFarUV;
float terrainFarAmount;
// Only the painted layers are fetched, and a branch around a fetch has no derivatives of
// its own, so the gradients are taken here where every layer still shares them
vec2 terrainFlatDX;
vec2 terrainFlatDY;
#ifdef HAS_NORMALS
    vec2 terrainSideXUV;
    vec2 terrainSideZUV;
    vec2 terrainSideXDX;
    vec2 terrainSideXDY;
    vec2 terrainSideZDX;
    vec2 terrainSideZDY;
    vec2 terrainSideWeight;
    float terrainSideAmount;
    // Which way each side plane faces, so a normal map can be mirrored back with its UV
    float terrainFaceX;
    float terrainFaceZ;
#endif

// A layer costs four fetches, so the projections are resolved once for all of them
void setupTerrainDetail(){
    terrainFarUV = v_terrainTextureDetailTiled * TERRAIN_TILE_BREAK_RATE;
    terrainFarAmount = smoothstep(TERRAIN_TILE_BREAK_NEAR, TERRAIN_TILE_BREAK_FAR, v_terrainEyeTiles) * TERRAIN_TILE_BREAK_MIX;
    terrainFlatDX = dFdx(v_terrainTextureDetailTiled);
    terrainFlatDY = dFdy(v_terrainTextureDetailTiled);

    #ifdef HAS_NORMALS
        vec3 normal = normalize(v_terrainNormal);
        terrainSideAmount = smoothstep(TERRAIN_TRIPLANAR_START, TERRAIN_TRIPLANAR_END, 1.0 - abs(normal.y));
        terrainSideWeight = vec2(abs(normal.x), abs(normal.z));
        terrainSideWeight = terrainSideWeight / max(terrainSideWeight.x + terrainSideWeight.y, 0.0001);

        // The side U axis follows the face direction, or opposite faces come out mirrored
        terrainFaceX = (normal.x < 0.0) ? -1.0 : 1.0;
        terrainFaceZ = (normal.z < 0.0) ? 1.0 : -1.0;
        terrainSideXUV = vec2(v_terrainTextureDetailTiled.y * terrainFaceX, v_terrainDetailHeight);
        terrainSideZUV = vec2(v_terrainTextureDetailTiled.x * terrainFaceZ, v_terrainDetailHeight);

        float heightDX = dFdx(v_terrainDetailHeight);
        float heightDY = dFdy(v_terrainDetailHeight);
        terrainSideXDX = vec2(terrainFlatDX.y * terrainFaceX, heightDX);
        terrainSideXDY = vec2(terrainFlatDY.y * terrainFaceX, heightDY);
        terrainSideZDX = vec2(terrainFlatDX.x * terrainFaceZ, heightDX);
        terrainSideZDY = vec2(terrainFlatDY.x * terrainFaceZ, heightDY);
    #endif
}

vec4 sampleTerrainSlice(float slice, vec2 uv, vec2 dx, vec2 dy){
    return textureGrad(sampler2DArray(u_terrainDetail, u_terrainDetail_smp), vec3(uv, slice), dx, dy);
}

// A flat projection stretches over a cliff, so steep ground moves to the side planes.
// The layer transform rides on every projection, so all of its maps stay in step.
// The far tiling and the side planes are only fetched where they show.
vec4 getTerrainSlice(float slice, vec2 scale, vec2 offset){
    vec2 dx = terrainFlatDX * scale;
    vec2 dy = terrainFlatDY * scale;

    vec4 color = sampleTerrainSlice(slice, v_terrainTextureDetailTiled * scale + offset, dx, dy);
    if (terrainFarAmount > 0.0){
        color = mix(color,
            sampleTerrainSlice(slice, terrainFarUV * scale + offset, dx * TERRAIN_TILE_BREAK_RATE, dy * TERRAIN_TILE_BREAK_RATE),
            terrainFarAmount);
    }

    #ifdef HAS_NORMALS
        if (terrainSideAmount > 0.0){
            vec4 side = sampleTerrainSlice(slice, terrainSideXUV * scale + offset, terrainSideXDX * scale, terrainSideXDY * scale) * terrainSideWeight.x +
                        sampleTerrainSlice(slice, terrainSideZUV * scale + offset, terrainSideZDX * scale, terrainSideZDY * scale) * terrainSideWeight.y;
            color = mix(color, side, terrainSideAmount);
        }
    #endif

    return color;
}

vec4 getTerrainLayer(float layer){
    return getTerrainSlice(layer, vec2(1.0), vec2(0.0));
}

// Detail alpha carries layer height and biases the blend toward the taller layer, so it takes
// the contact zone. An opaque detail has no height and blends on its map weight alone.
vec4 getTerrainColor(vec4 color){
    setupTerrainDetail();

    vec3 blend[TERRAIN_BLENDMAPS];
    blend[0] = texture(sampler2D(u_blendMap, u_blendMap_smp), v_terrainTextureCoords).rgb;
    blend[1] = texture(sampler2D(u_blendMap1, u_blendMap1_smp), v_terrainTextureCoords).rgb;
    blend[2] = texture(sampler2D(u_blendMap2, u_blendMap2_smp), v_terrainTextureCoords).rgb;

    // The base holds whatever weight the layers leave unclaimed, and stands as solid as an
    // opaque detail
    float weightBase = 1.0;
    vec4 sum = vec4(0.0);
    float total = 0.0;

    for (int m = 0; m < TERRAIN_BLENDMAPS; m++){
        for (int c = 0; c < 3; c++){
            float mapWeight = blend[m][c];
            weightBase -= mapWeight;
            if (mapWeight > 0.0){
                vec4 layer = getTerrainLayer(float(m * 3 + c));
                float weight = mapWeight * pow(layer.a, TERRAIN_HEIGHT_CONTRAST);
                sum += layer * weight;
                total += weight;
            }
        }
    }

    weightBase = max(weightBase, 0.0);
    sum += color * weightBase;
    total += weightBase;

    vec4 result = sum / max(total, 0.0001);

    // Height belongs to the blend, not the surface: the material keeps its own alpha
    result.a = color.a;

    return result;
}

#ifdef HAS_TERRAIN_PBR

#ifdef HAS_NORMALS
    // Terrain object space into whichever space the pass shades in
    vec3 terrainShadingNormal(vec3 objectNormal){
        vec3 axisY = cross(v_terrainAxisZ, v_terrainAxisX);
        return objectNormal.x * v_terrainAxisX + objectNormal.y * axisY + objectNormal.z * v_terrainAxisZ;
    }

    vec3 sampleTerrainTangentNormal(float slice, vec2 uv, vec2 dx, vec2 dy, float strength){
        vec3 tangentNormal = sampleTerrainSlice(slice, uv, dx, dy).xyz * 2.0 - 1.0;
        tangentNormal.xy *= strength;
        return tangentNormal;
    }

    // Whiteout blending: a tangent normal is added to the geometric normal on its own
    // projection plane, never mixed as raw RGB across planes that face different ways.
    vec3 getTerrainLayerNormal(float slice, vec2 scale, vec2 offset, float strength){
        vec3 geom = normalize(v_terrainNormal);
        vec2 dx = terrainFlatDX * scale;
        vec2 dy = terrainFlatDY * scale;

        vec3 flatNormal = sampleTerrainTangentNormal(slice, v_terrainTextureDetailTiled * scale + offset, dx, dy, strength);
        if (terrainFarAmount > 0.0){
            flatNormal = mix(flatNormal,
                sampleTerrainTangentNormal(slice, terrainFarUV * scale + offset, dx * TERRAIN_TILE_BREAK_RATE, dy * TERRAIN_TILE_BREAK_RATE, strength),
                terrainFarAmount);
        }
        vec3 normal = vec3(flatNormal.xy + geom.xz, abs(flatNormal.z) * geom.y).xzy;
        if (terrainSideAmount <= 0.0){
            return normalize(normal);
        }

        vec3 sideX = sampleTerrainTangentNormal(slice, terrainSideXUV * scale + offset, terrainSideXDX * scale, terrainSideXDY * scale, strength);
        vec3 sideZ = sampleTerrainTangentNormal(slice, terrainSideZUV * scale + offset, terrainSideZDX * scale, terrainSideZDY * scale, strength);
        // Undo the mirrored U of each side plane, or opposite slopes tilt the wrong way
        sideX.x *= terrainFaceX;
        sideZ.x *= terrainFaceZ;
        vec3 sideNormal = vec3(sideX.xy + geom.zy, abs(sideX.z) * geom.x).zyx * terrainSideWeight.x +
                          vec3(sideZ.xy + geom.xy, abs(sideZ.z) * geom.z).xyz * terrainSideWeight.y;

        return normalize(mix(normal, sideNormal, terrainSideAmount));
    }
#endif

// Everything the painted layers cover, evaluated once and shared by the color pass and
// the G-buffer. Whatever weight the layers leave unclaimed keeps the values handed in.
TerrainSurface getTerrainSurface(vec4 baseColor, vec3 baseNormal, float baseRoughness, float baseMetallic){
    setupTerrainDetail();

    vec3 blend[TERRAIN_BLENDMAPS];
    blend[0] = texture(sampler2D(u_blendMap, u_blendMap_smp), v_terrainTextureCoords).rgb;
    blend[1] = texture(sampler2D(u_blendMap1, u_blendMap1_smp), v_terrainTextureCoords).rgb;
    blend[2] = texture(sampler2D(u_blendMap2, u_blendMap2_smp), v_terrainTextureCoords).rgb;

    float weightBase = 1.0;
    vec4 sumColor = vec4(0.0);
    vec3 sumNormal = vec3(0.0);
    float sumRoughness = 0.0;
    float sumMetallic = 0.0;
    float sumOcclusion = 0.0;
    float total = 0.0;

    for (int m = 0; m < TERRAIN_BLENDMAPS; m++){
        for (int c = 0; c < 3; c++){
            int index = m * 3 + c;
            float mapWeight = blend[m][c];
            weightBase -= mapWeight;
            if (mapWeight <= 0.0){
                continue;
            }

            vec2 scale = terrainLayers.uvTransform[index].xy;
            vec2 offset = terrainLayers.uvTransform[index].zw;

            vec4 color = getTerrainSlice(terrainLayers.slices[index].x, scale, offset);
            float height = color.a;
            float roughness = baseRoughness;
            float metallic = baseMetallic;
            float occlusion = 1.0;
            vec3 normal = baseNormal;

            if (terrainLayers.colorFactor[index].a > 0.5){
                // sRGB and tinted, with its alpha out of the blend. A color layer beside it
                // stays raw, as on the cheap path, so old layers keep their look.
                color = vec4(sRGBToLinear(color.rgb) * terrainLayers.colorFactor[index].rgb, baseColor.a);
                height = 1.0;
                roughness = terrainLayers.surface[index].x;
                metallic = terrainLayers.surface[index].y;

                float surfaceSlice = terrainLayers.slices[index].z;
                if (surfaceSlice >= 0.0){
                    vec4 orm = getTerrainSlice(surfaceSlice, scale, offset);
                    occlusion = mix(1.0, orm.r, terrainLayers.surface[index].w);
                    roughness *= orm.g;
                    metallic *= orm.b;
                    height = orm.a;
                }

                #ifdef HAS_NORMALS
                    float normalSlice = terrainLayers.slices[index].y;
                    if (normalSlice >= 0.0){
                        normal = terrainShadingNormal(getTerrainLayerNormal(normalSlice, scale, offset, terrainLayers.surface[index].z));
                    }
                #endif
            }

            float weight = mapWeight * pow(height, TERRAIN_HEIGHT_CONTRAST);
            sumColor += color * weight;
            sumNormal += normal * weight;
            sumRoughness += roughness * weight;
            sumMetallic += metallic * weight;
            sumOcclusion += occlusion * weight;
            total += weight;
        }
    }

    weightBase = max(weightBase, 0.0);
    sumColor += baseColor * weightBase;
    sumNormal += baseNormal * weightBase;
    sumRoughness += baseRoughness * weightBase;
    sumMetallic += baseMetallic * weightBase;
    sumOcclusion += weightBase;
    total += weightBase;

    TerrainSurface surface;
    // Painted over by layers with no height where they meet: the base stands, not black
    if (total <= 0.0){
        surface.color = baseColor;
        surface.normal = baseNormal;
        surface.roughness = baseRoughness;
        surface.metallic = baseMetallic;
        surface.occlusion = 1.0;
        return surface;
    }

    surface.color = sumColor / total;
    // Height belongs to the blend, not the surface: the material keeps its own alpha
    surface.color.a = baseColor.a;
    surface.roughness = clamp(sumRoughness / total, 0.0, 1.0);
    surface.metallic = clamp(sumMetallic / total, 0.0, 1.0);
    surface.occlusion = clamp(sumOcclusion / total, 0.0, 1.0);

    float normalLength = length(sumNormal);
    surface.normal = (normalLength > 0.0001) ? sumNormal / normalLength : baseNormal;

    return surface;
}

#ifdef TERRAIN_MATERIAL_INFO
    // The painted surface replaces what the material contributed, reflectance included
    MaterialInfo applyTerrainSurface(MaterialInfo info, TerrainSurface surface, float f0_ior){
        info.perceptualRoughness = surface.roughness;
        info.metallic = surface.metallic;

        vec3 f0 = vec3(f0_ior);
        info.albedoColor = mix(info.baseColor.rgb * (vec3(1.0) - f0), vec3(0.0), info.metallic);
        info.f0 = mix(f0, info.baseColor.rgb, info.metallic);

        return info;
    }
#endif

#endif
