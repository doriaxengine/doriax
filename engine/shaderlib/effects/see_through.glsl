// Coverage of a fragment that may hide the camera's see-through target
#pragma view vec3 SeeThrough.target "World position kept in view"
#pragma view float SeeThrough.radius = 0 [0, 20] "Radius of the opening around the target; 0 turns it off"
#pragma view float SeeThrough.opacity = 0.3 [0, 1] "Share of the mesh left inside the opening"
#pragma view float SeeThrough.margin = 0.5 [0, 10] "Distance before the target where the opening starts"
float seeThrough(FragmentContext context){
    return coverageViewOpening(context.worldPosition, context.eye, context.forward, context.perspective,
        SeeThrough.target, SeeThrough.radius, SeeThrough.opacity, SeeThrough.margin);
}
