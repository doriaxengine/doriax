#ifndef INSTANCE_MATRIX
#define INSTANCE_MATRIX

// Instances are affine, so the bottom row carries the custom value
mat4 getInstanceMatrix(){
    return mat4(vec4(i_matrix_col1.xyz, 0.0), vec4(i_matrix_col2.xyz, 0.0), vec4(i_matrix_col3.xyz, 0.0), vec4(i_matrix_col4.xyz, 1.0));
}

vec4 getInstanceCustom(){
    return vec4(i_matrix_col1.w, i_matrix_col2.w, i_matrix_col3.w, i_matrix_col4.w);
}

#endif
