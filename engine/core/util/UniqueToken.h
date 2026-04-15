//
// (c) 2026 Eduardo Doria.
//

#ifndef UNIQUETOKEN_H
#define UNIQUETOKEN_H

#include "Export.h"
#include <string>

namespace doriax {

    class DORIAX_API UniqueToken {
    private:
        static int id;

        static std::string randString(const int len);

    public:
        static std::string get();

    };

}

#endif //UNIQUETOKEN_H
