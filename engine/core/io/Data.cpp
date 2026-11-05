//
// Inspired by work of Jari Komppa in SoLoud audio engine
// https://sol.gfxile.net/soloud/file.html
// Modified by Eduardo Doria.
//

// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Data.h"
#include "ResourcePack.h"
#include "Log.h"

#include <new>
#include <string.h>
#include <vector>

using namespace doriax;

Data::Data(): dataPtr(NULL), dataLength(0), offset(0), dataOwned(false) {

}

// the data stays empty when there is no memory
bool Data::allocate(unsigned int aLength) {
    dataPtr = new (std::nothrow) unsigned char[aLength];
    dataOwned = dataPtr != NULL;
    dataLength = dataOwned ? aLength : 0;
    return dataOwned;
}

Data::Data(unsigned char *aData, unsigned int aDataLength, bool aCopy, bool aTakeOwnership): Data(){
    open(aData, aDataLength, aCopy, aTakeOwnership);
}

Data::Data(const char *aFilename): Data(){
    if (open(aFilename) != FileErrors::FILEDATA_OK)
        Log::error("Can't open file: %s", aFilename ? aFilename : "(nil)");
}

Data::Data(const Data& d): Data(){
    *this = d;
}

Data& Data::operator = (const Data& d){
    if (this == &d)
        return *this;

    // freed after the copy, d can be a view of this buffer
    unsigned char* oldPtr = dataOwned ? dataPtr : NULL;
    dataPtr = NULL;
    dataLength = 0;
    dataOwned = false;
    offset = 0;

    // a copy always owns its buffer
    if (d.dataPtr && allocate(d.dataLength)) {
        memcpy(dataPtr, d.dataPtr, dataLength);
        offset = d.offset;
    }

    delete[] oldPtr;

    return *this;
}

Data::~Data() {
    if (dataOwned)
        delete[] dataPtr;
}

unsigned int Data::read(unsigned char *aDst, unsigned int aBytes) {
    if (aBytes > dataLength - offset)
        aBytes = dataLength - offset;

    if (aBytes > 0)
        memcpy(aDst, dataPtr + offset, aBytes);
    offset += aBytes;

    return aBytes;
}

unsigned int Data::write(unsigned char *aSrc, unsigned int aBytes) {
    if (dataLength > 0 || offset > 0)
        return 0;

    if (dataOwned)
        delete[] dataPtr;
    if (!allocate(aBytes))
        return 0;
    memcpy(dataPtr + offset, aSrc, aBytes);
    offset += aBytes;

    return aBytes;
}

unsigned int Data::length() {
    return dataLength;
}

void Data::seek(int aOffset) {
    if (aOffset >= 0)
        offset = aOffset;
    else
        offset = dataLength + aOffset;
    if (offset > dataLength)
        offset = aOffset >= 0 ? dataLength : 0;
}

unsigned int Data::pos() {
    return offset;
}

unsigned char * Data::getMemPtr() {
    return dataPtr;
}

unsigned int Data::open(unsigned char *aData, unsigned int aDataLength, bool aCopy, bool aTakeOwnership) {
    if (aData == NULL || aDataLength == 0)
        return FileErrors::INVALID_PARAMETER;

    if (dataOwned)
        delete[] dataPtr;
    dataPtr = 0;
    offset = 0;

    if (aCopy)
    {
        if (!allocate(aDataLength))
            return FileErrors::OUT_OF_MEMORY;
        memcpy(dataPtr, aData, aDataLength);
        return FileErrors::FILEDATA_OK;
    }

    dataLength = aDataLength;
    dataPtr = aData;
    dataOwned = aTakeOwnership;
    return FileErrors::FILEDATA_OK;
}

unsigned int Data::open(const char *aFilename) {
    if (!aFilename)
        return FileErrors::INVALID_PARAMETER;
    if (dataOwned)
        delete[] dataPtr;
    dataPtr = 0;
    dataLength = 0;
    dataOwned = false;
    offset = 0;

    std::vector<unsigned char> packedData;
    if (ResourcePack::read(aFilename, packedData)) {
        if (!allocate(static_cast<unsigned int>(packedData.size())))
            return FileErrors::OUT_OF_MEMORY;
        if (dataLength > 0)
            memcpy(dataPtr, packedData.data(), dataLength);
        return FileErrors::FILEDATA_OK;
    }

    File df;
    int res = df.open(aFilename);
    if (res != 0)
        return res;

    return open(&df);
}

unsigned int Data::open(File *aFile) {
    if (!aFile || !aFile->getFilePtr())
        return FileErrors::INVALID_PARAMETER;

    if (dataOwned)
        delete[] dataPtr;
    dataPtr = 0;
    offset = 0;

    if (!allocate(aFile->length()))
        return FileErrors::OUT_OF_MEMORY;
    dataLength = aFile->read(dataPtr, dataLength);
    return FileErrors::FILEDATA_OK;
}

int Data::eof() {
    if (offset >= dataLength)
        return 1;
    return 0;
}
