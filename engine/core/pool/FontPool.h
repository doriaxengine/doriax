//
// (c) 2026 Eduardo Doria.
//

#ifndef FONTPOOL_H
#define FONTPOOL_H

#include "util/STBText.h"
#include <map>
#include <memory>

namespace doriax{

    typedef std::map< std::string, std::shared_ptr<STBText> > fonts_t;

    class DORIAX_API FontPool{
    private:
        static fonts_t& getMap();

    public:
        static std::shared_ptr<STBText> get(const std::string& id);
        static std::shared_ptr<STBText> get(const std::string& id, const std::string& fontpath, unsigned int fontSize);
        static void remove(const std::string& id);

        // necessary for engine shutdown
        static void clear();

    };
}

#endif /* FONTPOOL_H */
