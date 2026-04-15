//
// (c) 2026 Eduardo Doria.
//

#ifndef SBSREADER_H
#define SBSREADER_H

#include <string>
#include "ShaderData.h"
#include "io/FileData.h"

namespace doriax {

    class DORIAX_API SBSReader {
    private:
        ShaderData shaderData;

        bool read(FileData& file);

    public:
        SBSReader();
        virtual ~SBSReader();

        bool read(const std::string& filepath);
        bool read(std::vector<unsigned char> datashader);

        ShaderData& getShaderData();
    };

}


#endif //SBSREADER_H
