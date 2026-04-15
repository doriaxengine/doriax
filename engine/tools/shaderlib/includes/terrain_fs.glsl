uniform texture2D u_blendMap;
uniform texture2D u_terrainDetailR;
uniform texture2D u_terrainDetailG;
uniform texture2D u_terrainDetailB;
uniform sampler u_blendMap_smp;
uniform sampler u_terrainDetailR_smp;
uniform sampler u_terrainDetailG_smp;
uniform sampler u_terrainDetailB_smp;

in vec2 v_terrainTextureCoords;
in vec2 v_terrainTextureDetailTiled;

vec4 getTerrainColor(vec4 color){
    vec4 blendMapColor = texture(sampler2D(u_blendMap, u_blendMap_smp), v_terrainTextureCoords);
    float backTextureAmount = 1.0 - (blendMapColor.r + blendMapColor.g + blendMapColor.b);
    color = color * backTextureAmount;

    color = color + texture(sampler2D(u_terrainDetailR, u_terrainDetailR_smp), v_terrainTextureDetailTiled) * blendMapColor.r;
    color = color + texture(sampler2D(u_terrainDetailG, u_terrainDetailG_smp), v_terrainTextureDetailTiled) * blendMapColor.g;
    color = color + texture(sampler2D(u_terrainDetailB, u_terrainDetailB_smp), v_terrainTextureDetailTiled) * blendMapColor.b;

    return color;
}