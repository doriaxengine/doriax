// Cuts away everything above a height, such as the upper floors of a building
// @scene float Clip.height = 1000 "World height above which meshes are cut away"
// @scene float Clip.softness = 0 [0, 10] "Width of a dithered band above the cut"
// @coverage_shadows apply

float getCoverage(CoverageContext context){
    return coverageBelowHeight(context.worldPosition, Clip.height, Clip.softness);
}
