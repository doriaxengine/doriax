//
// Inspired by work of Jari Komppa in SoLoud audio engine
// https://sol.gfxile.net/soloud/file.html
// Modified by Eduardo Doria.
//

// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "File.h"

#include "System.h"
#include "Log.h"

using namespace doriax;

File::File(){
    fileHandle = NULL;
}

File::File(FILE *fp) {
    this->fileHandle = fp;
}

File::File(const char *aFilename, bool write): File(){
    if (open(aFilename, write) != FileErrors::FILEDATA_OK)
        Log::error("Can't open file: %s", aFilename ? aFilename : "(nil)");
}

File::~File() {
    close();
}

int File::eof() {
    if (!fileHandle)
        return 1;
    return feof(fileHandle);
}

unsigned int File::read(unsigned char *aDst, unsigned int aBytes) {
    if (!fileHandle)
        return 0;
    return (unsigned int)fread(aDst, 1, aBytes, fileHandle);
}

unsigned int File::write(unsigned char *aSrc, unsigned int aBytes){
    if (!fileHandle)
        return 0;
    unsigned int r = (unsigned int)fwrite(aSrc, 1, aBytes, fileHandle);
    System::instance().syncFileSystem();
    return r;
}

unsigned int File::length() {
    if (!fileHandle)
        return 0;
    int pos = ftell(fileHandle);
    fseek(fileHandle, 0, SEEK_END);
    int len = ftell(fileHandle);
    fseek(fileHandle, pos, SEEK_SET);
    return len;
}

void File::seek(int aOffset) {
    if (fileHandle)
        fseek(fileHandle, aOffset, SEEK_SET);
}

unsigned int File::pos() {
    if (!fileHandle)
        return 0;
    return ftell(fileHandle);
}

FILE *File::getFilePtr() {
    return fileHandle;
}

unsigned int File::open(const char *aFilename, bool write){
    if (!aFilename)
        return FileErrors::INVALID_PARAMETER;
    close();
    std::string systemPath = FileData::getSystemPath(aFilename);
    if (!write)
        fileHandle = System::instance().platformFopen(systemPath.c_str(), "rb");
    else{
        fileHandle = System::instance().platformFopen(systemPath.c_str(), "w+b");
    }
    if (!fileHandle)
        return FileErrors::FILE_NOT_FOUND;
    return FileErrors::FILEDATA_OK;
}

void File::flush(){
    if (fileHandle)
        fflush(fileHandle);
}

void File::close(){
    if (fileHandle) {
        fclose(fileHandle);
        fileHandle = NULL;
    }
}
