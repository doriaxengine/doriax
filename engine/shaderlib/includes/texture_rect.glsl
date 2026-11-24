// the part of the texture a sprite frame or an instance shows, the same in every pass
#ifdef HAS_TEXTURERECT
    uniform u_vs_spriteParams {
        vec4 textureRect;
    } spriteParams;
#endif
#ifdef HAS_INSTANCING
    in vec4 i_textureRect;
#endif

vec2 getTextureRectUV(vec2 uv){
    #ifdef HAS_TEXTURERECT
        uv = uv * spriteParams.textureRect.zw + spriteParams.textureRect.xy;
    #endif
    #ifdef HAS_INSTANCING
        uv = uv * i_textureRect.zw + i_textureRect.xy;
    #endif
    return uv;
}
