//
// Inspired by work of Jari Komppa in SoLoud audio engine
// https://sol.gfxile.net/soloud/file.html
// Modified by Eduardo Doria.
//

// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef DATA_H
#define DATA_H

#include "Export.h"
#include "FileData.h"
#include "File.h"

namespace doriax {

    class DORIAX_API Data: public FileData {

    protected:
        unsigned char *dataPtr;
        unsigned int dataLength;
        unsigned int offset;
        bool dataOwned;

        bool allocate(unsigned int aLength);

    public:
        Data();
        Data(unsigned char *aData, unsigned int aDataLength, bool aCopy=false, bool aTakeOwnership=true);
        Data(const char *aFilename);

        Data(const Data& d);
        Data& operator = (const Data& d);

        virtual ~Data();

        virtual int eof() override;
        virtual unsigned int read(unsigned char *aDst, unsigned int aBytes) override;
        virtual unsigned int write(unsigned char *aSrc, unsigned int aBytes) override;
        virtual unsigned int length() override;
        virtual void seek(int aOffset) override;
        virtual unsigned int pos() override;
        virtual unsigned char * getMemPtr();

        unsigned int open(unsigned char *aData, unsigned int aDataLength, bool aCopy=false, bool aTakeOwnership=true);
        unsigned int open(const char *aFilename);
        unsigned int open(File *aFile);
    };
    
}


#endif
