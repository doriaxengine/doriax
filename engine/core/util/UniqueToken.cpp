//
// (c) 2026 Eduardo Doria.
//

#include "UniqueToken.h"
#include <cstdlib>

using namespace doriax;

int UniqueToken::id;

std::string UniqueToken::randString(const int len){
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::string tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return tmp_s;
}

std::string UniqueToken::get(){

    return randString(10) + std::to_string(id++);
}
