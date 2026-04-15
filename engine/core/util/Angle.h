//
// (c) 2026 Eduardo Doria.
//

#ifndef angle_h
#define angle_h

#include "Export.h"

namespace doriax {

    class DORIAX_API Angle {
    public:
        static float radToDefault(float radians);
        static float degToDefault(float degrees);
        static float defaultToRad(float angle);
        static float defaultToDeg(float angle);
        static float radToDeg(float radians);
        static float degToRad(float degrees);
    };
    
}

#endif
