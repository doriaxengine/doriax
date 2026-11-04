//
// Inspired by work of Jari Komppa in SoLoud audio engine
// https://sol.gfxile.net/soloud/file.html
// Modified by Eduardo Doria.
//

// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef FILE_H
#define FILE_H

#include "FileData.h"

namespace doriax {

    class DORIAX_API File: public FileData {
    protected:
        FILE *fileHandle;
        bool modified;
        bool writing;

        void syncWritten();
        void setWriting(bool value);

    public:

        File();
        File(FILE *fp);
        File(const char *aFilename, bool write = false);

        // owns the handle
        File(const File& f) = delete;
        File& operator = ( const File& f ) = delete;

        virtual ~File();

        virtual int eof();
        virtual unsigned int read(unsigned char *aDst, unsigned int aBytes);
        virtual unsigned int write(unsigned char *aSrc, unsigned int aBytes);
        virtual unsigned int length();
        virtual void seek(int aOffset);
        virtual unsigned int pos();
        virtual FILE * getFilePtr();

        unsigned int open(const char *aFilename, bool write = false);
        void flush();
        void close();
    };
    
}


#endif
