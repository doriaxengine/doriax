#ifdef HAS_MORPHTARGET
    in vec3 a_morphTarget0;
    in vec3 a_morphTarget1;
    #if (defined(HAS_MORPHNORMAL) && !defined(HAS_MORPHTANGENT)) || (!defined(HAS_MORPHNORMAL) && defined(HAS_MORPHTANGENT))
        in vec3 a_morphTarget2;
        in vec3 a_morphTarget3;
    #endif
    #if !defined(HAS_MORPHNORMAL) && !defined(HAS_MORPHTANGENT)
        in vec3 a_morphTarget4;
        in vec3 a_morphTarget5;
        in vec3 a_morphTarget6;
        in vec3 a_morphTarget7;
    #endif

    #ifndef DEPTH_SHADER
        #if defined(HAS_NORMALS) && defined(HAS_MORPHNORMAL)
            in vec3 a_morphNormal0;
            in vec3 a_morphNormal1;
            #if !defined(HAS_MORPHTANGENT)
                in vec3 a_morphNormal2;
                in vec3 a_morphNormal3;
            #endif
        #endif

        #if defined(HAS_TANGENTS) && defined(HAS_MORPHTANGENT)
            in vec3 a_morphTangent0;
            in vec3 a_morphTangent1;
        #endif
    #endif

    uniform u_vs_morphtarget {
        #if !defined(HAS_MORPHNORMAL) && !defined(HAS_MORPHTANGENT)
            vec4 morphWeights[2];
        #else
            vec4 morphWeights[1];
        #endif
    };
#endif

vec3 getMorphPosition(vec3 pos){
    #ifdef HAS_MORPHTARGET
        pos += (morphWeights[0].x * a_morphTarget0);
        pos += (morphWeights[0].y * a_morphTarget1);
        #if (defined(HAS_MORPHNORMAL) && !defined(HAS_MORPHTANGENT)) || (!defined(HAS_MORPHNORMAL) && defined(HAS_MORPHTANGENT))
            pos += (morphWeights[0].z * a_morphTarget2);
            pos += (morphWeights[0].w * a_morphTarget3);
        #endif
        #if !defined(HAS_MORPHNORMAL) && !defined(HAS_MORPHTANGENT)
            pos += (morphWeights[1].x * a_morphTarget4);
            pos += (morphWeights[1].y * a_morphTarget5);
            pos += (morphWeights[1].z * a_morphTarget6);
            pos += (morphWeights[1].w * a_morphTarget7);
        #endif
    #endif

    return pos;
}

#ifndef DEPTH_SHADER
    vec3 getMorphNormal(vec3 normal){
        #if defined(HAS_NORMALS) && defined(HAS_MORPHNORMAL)
            normal += (morphWeights[0].x * a_morphNormal0);
            normal += (morphWeights[0].y * a_morphNormal1);
            #if !defined(HAS_MORPHTANGENT)
                normal += (morphWeights[0].z * a_morphNormal2);
                normal += (morphWeights[0].w * a_morphNormal3);
            #endif
        #endif

        return normal;
    }

    vec3 getMorphTangent(vec3 tangent){
        #if defined(HAS_TANGENTS) && defined(HAS_MORPHTANGENT)
            tangent += (morphWeights[0].x * a_morphTangent0);
            tangent += (morphWeights[0].y * a_morphTangent1);
        #endif

        return tangent;
    }
#endif