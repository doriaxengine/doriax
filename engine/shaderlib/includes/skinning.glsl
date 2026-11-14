#ifdef HAS_SKINNING
    in vec4 a_boneWeights;
    in vec4 a_boneIds;

    #ifdef SKINNING_TEXTURE
        #pragma image_sample_type u_bonesTexture unfilterable_float
        uniform texture2D u_bonesTexture;
        #pragma sampler_type u_bones_smp nonfiltering
        uniform sampler u_bones_smp;
    #else
        readonly buffer sbo_skinning {
            mat4 bonesMatrix[];
        };
    #endif

    uniform u_vs_skinning {
        vec4 normAdjust; // joint scale, weight scale
    };
#endif

#ifdef HAS_SKINNING
mat4 getBoneMatrix(int index){
    #ifdef SKINNING_TEXTURE
        return mat4(
            texelFetch(sampler2D(u_bonesTexture, u_bones_smp), ivec2(0, index), 0),
            texelFetch(sampler2D(u_bonesTexture, u_bones_smp), ivec2(1, index), 0),
            texelFetch(sampler2D(u_bonesTexture, u_bones_smp), ivec2(2, index), 0),
            texelFetch(sampler2D(u_bonesTexture, u_bones_smp), ivec2(3, index), 0));
    #else
        return bonesMatrix[index];
    #endif
}
#endif

mat4 getBoneTransform(){
    mat4 boneTransform = mat4(0.0);
    #ifdef HAS_SKINNING
        boneTransform += getBoneMatrix(int(a_boneIds[0] * normAdjust.x)) * (a_boneWeights[0] * normAdjust.y);
        boneTransform += getBoneMatrix(int(a_boneIds[1] * normAdjust.x)) * (a_boneWeights[1] * normAdjust.y);
        boneTransform += getBoneMatrix(int(a_boneIds[2] * normAdjust.x)) * (a_boneWeights[2] * normAdjust.y);
        boneTransform += getBoneMatrix(int(a_boneIds[3] * normAdjust.x)) * (a_boneWeights[3] * normAdjust.y);
    #endif

    return boneTransform;
}

vec3 getSkinPosition(vec3 pos, mat4 boneTransform){
    #ifdef HAS_SKINNING
        vec4 skinVertex = vec4(pos, 1.0);
        skinVertex = boneTransform * skinVertex;
        pos = vec3(skinVertex) / skinVertex.w;
    #endif

    return pos;
}

vec3 getSkinNormal(vec3 normal, mat4 boneTransform){
    #ifdef HAS_SKINNING
        // direction: no translation (callers normalize)
        normal = mat3(boneTransform) * normal;
    #endif

    return normal;
}

vec3 getSkinTangent(vec3 tangent, mat4 boneTransform){
    #ifdef HAS_SKINNING
        tangent = mat3(boneTransform) * tangent;
    #endif

    return tangent;
}
