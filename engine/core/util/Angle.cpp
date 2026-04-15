//
// (c) 2026 Eduardo Doria.
//

#include "Angle.h"

#include "Engine.h"
#define _USE_MATH_DEFINES
#include <cmath>
#include <math.h>

using namespace doriax;

float Angle::radToDefault(float radians){
    if (Engine::isUseDegrees()){
        return radians * (180.0 / M_PI);
    }else{
        return radians;
    }
}

float Angle::degToDefault(float degrees){
    if (!Engine::isUseDegrees()){
        return degrees * (M_PI / 180.0);
    }else{
        return degrees;
    }
}

float Angle::defaultToRad(float angle){
    if (Engine::isUseDegrees()){
        return angle * (M_PI / 180.0);
    }else{
        return angle;
    }
}

float Angle::defaultToDeg(float angle){
  if (!Engine::isUseDegrees()){
      return angle * (180.0 / M_PI);
  }else{
      return angle;
  }
}

float Angle::radToDeg(float radians){
    return radians * (180.0 / M_PI);
}

float Angle::degToRad(float degrees){
    return degrees * (M_PI / 180.0);
}
