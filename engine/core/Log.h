//
// (c) 2026 Eduardo Doria.
//

#ifndef log_h
#define log_h

#include "Export.h"

namespace doriax {

    class DORIAX_API Log {
    public:
        static void print(const char* text, ...);

        static void verbose(const char* text, ...);
        static void debug(const char* text, ...);
        static void warn(const char* text, ...);
        static void error(const char* text, ...);
    };

}

#endif
