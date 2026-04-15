//
// Inspired by work of Jari Komppa in SoLoud audio engine
// https://sol.gfxile.net/soloud/file.html
// Modified by (c) 2020 Eduardo Doria.
//

#ifndef FILEDATA_H
#define FILEDATA_H

#include "Export.h"
#include <stdio.h>
#include <string>

namespace doriax {

    enum FileErrors{
        FILEDATA_OK       = 0,
        INVALID_PARAMETER = 1,
        FILE_NOT_FOUND    = 2,
        OUT_OF_MEMORY    = 3
    };

    class DORIAX_API FileData {

    protected:
        static bool beginWith(const std::string& path, const std::string& prefix);
        static std::string simplifyPath(const std::string& path);

    public:
        virtual ~FileData();

        static FileData* newFile(bool useHandle = false);
        static FileData* newFile(const char *aFilename, bool useHandle = false);

        static std::string getBaseDir(const std::string& filepath);
        static std::string getFilePathExtension(const std::string &filepath);
        static std::string getSystemPath(std::string path);

        unsigned int read8();
        unsigned int read16();
        unsigned int read32();

        virtual int eof() = 0;
        virtual unsigned int read(unsigned char *aDst, unsigned int aBytes) = 0;
        virtual unsigned int write(unsigned char *aSrc, unsigned int aBytes) = 0;
        virtual unsigned int length() = 0;
        virtual void seek(int aOffset) = 0;
        virtual unsigned int pos() = 0;

        std::string readString();
        std::string readString(unsigned int stringlen);
        unsigned int writeString(const std::string& s);
    };
    
}


#endif
