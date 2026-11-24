// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef FOG_COMPONENT_H
#define FOG_COMPONENT_H

#include "util/Color.h"

namespace doriax{

    enum class FogType{
        LINEAR,
		EXPONENTIAL,
		EXPONENTIALSQUARED
    };

    struct DORIAX_API FogComponent{
        FogType type = FogType::LINEAR;
		Vector3 color = Color::sRGBToLinear(Vector3(0.8f, 0.8f, 0.8f)); // linear color
		float density = 0.01f; // for exponential fog
		float linearStart = 10;
		float linearEnd = 60;
		float skyBlend = 0.0f; // 0 to 1: how much the fog takes the color of the sky behind each point
    };
    
}

#endif //FOG_COMPONENT_H