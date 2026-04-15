#ifdef HAS_SKINNING
    in vec4 a_boneWeights;
    in vec4 a_boneIds;

    uniform u_vs_skinning {
        mat4 bonesMatrix[MAX_BONES];
        vec4 normAdjust; //needed "expand" the normalized values - Joints (x), weights (y)
    };
#endif

mat4 getBoneTransform(){
    mat4 boneTransform = mat4(0.0);
    #ifdef HAS_SKINNING
        boneTransform += bonesMatrix[int(a_boneIds[0] * normAdjust.x)] * (a_boneWeights[0] * normAdjust.y);
        boneTransform += bonesMatrix[int(a_boneIds[1] * normAdjust.x)] * (a_boneWeights[1] * normAdjust.y);
        boneTransform += bonesMatrix[int(a_boneIds[2] * normAdjust.x)] * (a_boneWeights[2] * normAdjust.y);
        boneTransform += bonesMatrix[int(a_boneIds[3] * normAdjust.x)] * (a_boneWeights[3] * normAdjust.y);
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
        vec4 skinNormal = vec4(normal, 1.0);
        skinNormal = boneTransform * skinNormal;
        normal = vec3(skinNormal) / skinNormal.w;
    #endif

    return normal;
}

vec3 getSkinTangent(vec3 tangent, mat4 boneTransform){
    #ifdef HAS_SKINNING
        vec4 skinTangent = vec4(tangent, 1.0);
        skinTangent = boneTransform * skinTangent;
        tangent = vec3(skinTangent) / skinTangent.w;
    #endif

    return tangent;
}