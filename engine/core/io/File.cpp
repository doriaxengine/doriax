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

#include <climits>

using namespace doriax;

File::File(){
    fileHandle = NULL;
    modified = false;
    writing = false;
}

File::File(FILE *fp): File() {
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
    setWriting(false);
    // peeks a byte, a failed peek can't leave its flags on the stream
    int c = fgetc(fileHandle);
    if (c == EOF) {
        clearerr(fileHandle);
        return 1;
    }
    ungetc(c, fileHandle);
    return 0;
}

unsigned int File::read(unsigned char *aDst, unsigned int aBytes) {
    if (!fileHandle)
        return 0;
    setWriting(false);
    return (unsigned int)fread(aDst, 1, aBytes, fileHandle);
}

unsigned int File::write(unsigned char *aSrc, unsigned int aBytes){
    if (!fileHandle)
        return 0;
    setWriting(true);
    unsigned int r = (unsigned int)fwrite(aSrc, 1, aBytes, fileHandle);
    modified = true;
    System::instance().syncFileSystem();
    return r;
}

unsigned int File::length() {
    if (!fileHandle)
        return 0;
    long pos = ftell(fileHandle);
    if (pos < 0)
        return 0;
    fseek(fileHandle, 0, SEEK_END);
    long len = ftell(fileHandle);
    fseek(fileHandle, pos, SEEK_SET);
    // also a directory, which can report the largest long
    if (len < 0 || len == LONG_MAX || (unsigned long)len > UINT_MAX)
        return 0;
    return (unsigned int)len;
}

void File::seek(int aOffset) {
    if (!fileHandle)
        return;
    // a negative offset counts back from the end and stops at the start
    if (aOffset >= 0 || fseek(fileHandle, aOffset, SEEK_END) != 0)
        fseek(fileHandle, aOffset >= 0 ? aOffset : 0, SEEK_SET);
}

unsigned int File::pos() {
    if (!fileHandle)
        return 0;
    long pos = ftell(fileHandle);
    return pos < 0 ? 0 : (unsigned int)pos;
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
    // "w+b" already truncated it
    modified = write;
    return FileErrors::FILEDATA_OK;
}

void File::flush(){
    if (fileHandle) {
        fflush(fileHandle);
        syncWritten();
    }
}

void File::close(){
    if (fileHandle) {
        fclose(fileHandle);
        fileHandle = NULL;
        writing = false;
        syncWritten();
    }
}

void File::setWriting(bool value){
    // C needs a seek between reading and writing
    if (writing != value) {
        fseek(fileHandle, 0, SEEK_CUR);
        writing = value;
    }
}

void File::syncWritten(){
    // a write can leave bytes buffered until flush or close
    if (modified) {
        modified = false;
        System::instance().syncFileSystem();
    }
}
