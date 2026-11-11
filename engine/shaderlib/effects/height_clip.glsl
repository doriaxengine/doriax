// Coverage that ends above the scene's clip height
#pragma scene float Clip.height = 1000 "World height above which meshes are cut away"
#pragma scene float Clip.softness = 0 [0, 10] "Width of a dithered band above the cut"
float heightClip(vec3 worldPosition){
    return coverageBelowHeight(worldPosition, Clip.height, Clip.softness);
}
