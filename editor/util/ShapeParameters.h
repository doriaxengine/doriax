#pragma once

namespace doriax::editor{
    
    struct ShapeParameters{
        int geometryType = 0;
        float planeWidth = 10.0f, planeDepth = 10.0f;
        unsigned int planeTiles = 1;
        float boxWidth = 1.0f, boxHeight = 1.0f, boxDepth = 1.0f;
        unsigned int boxTiles = 1;
        float sphereRadius = 1.0f;
        unsigned int sphereSlices = 36, sphereStacks = 18;
        float cylinderBaseRadius = 1.0f, cylinderTopRadius = 1.0f, cylinderHeight = 2.0f;
        unsigned int cylinderSlices = 36, cylinderStacks = 18;
        float capsuleBaseRadius = 1.0f, capsuleTopRadius = 1.0f, capsuleHeight = 2.0f;
        unsigned int capsuleSlices = 36, capsuleStacks = 18;
        float torusRadius = 1.0f, torusRingRadius = 0.5f;
        unsigned int torusSides = 36, torusRings = 16;
    };

}