//
// Inspired by work of Jari Komppa in SoLoud audio engine
// https://sol.gfxile.net/soloud/file.html
// Modified by Eduardo Doria.
//

//
// (c) 2026 Eduardo Doria.
//

#include "File.h"

#include "System.h"

using namespace doriax;

File::File(){
    fileHandle = NULL;
}

File::File(FILE *fp) {
    this->fileHandle = fp;
}

File::File(const char *aFilename, bool write){
    open(aFilename, write);
}

File::File(const File& f){
    this->fileHandle = f.fileHandle;
}

File& File::operator = ( const File& f ){
    this->fileHandle = f.fileHandle;

    return *this;
}

File::~File() {
    if (fileHandle)
        close();
}

int File::eof() {
    return feof(fileHandle);
}

unsigned int File::read(unsigned char *aDst, unsigned int aBytes) {
    return (unsigned int)fread(aDst, 1, aBytes, fileHandle);
}

unsigned int File::write(unsigned char *aSrc, unsigned int aBytes){
    unsigned int r = (unsigned int)fwrite(aSrc, 1, aBytes, fileHandle);
    System::instance().syncFileSystem();
    return r;
}

unsigned int File::length() {
    int pos = ftell(fileHandle);
    fseek(fileHandle, 0, SEEK_END);
    int len = ftell(fileHandle);
    fseek(fileHandle, pos, SEEK_SET);
    return len;
}

void File::seek(int aOffset) {
    fseek(fileHandle, aOffset, SEEK_SET);
}

unsigned int File::pos() {
    return ftell(fileHandle);
}

FILE *File::getFilePtr() {
    return fileHandle;
}

unsigned int File::open(const char *aFilename, bool write){
    if (!aFilename)
        return FileErrors::INVALID_PARAMETER;
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
    fflush(fileHandle);
}

void File::close(){
    fclose(fileHandle);
}
