// Dithers the mesh away where it hides a target from the camera, such as a player behind a
// wall. A script tracks the target into the camera's SeeThrough view parameters.
// @view vec3 SeeThrough.target "World position kept in view"
// @view float SeeThrough.radius = 0 [0, 20] "Radius of the opening around the target; 0 turns it off"
// @view float SeeThrough.opacity = 0.3 [0, 1] "Share of the mesh left inside the opening"
// @view float SeeThrough.margin = 0.5 [0, 10] "Distance before the target where the opening starts"
// @coverage_shadows keep

float getCoverage(CoverageContext context){
    return coverageViewOpening(context.worldPosition, context.eye, context.forward, context.perspective,
        SeeThrough.target, SeeThrough.radius, SeeThrough.opacity, SeeThrough.margin);
}
