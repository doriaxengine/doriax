#include "Conector.h"

#include "editor/Out.h"
#include "Project.h"

#include <iostream>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using namespace doriax;

editor::Conector::Conector(){
    libHandle = nullptr;
}

editor::Conector::~Conector(){
    disconnect();
}

bool editor::Conector::fileExists(const fs::path& path) {
    return std::filesystem::exists(path);
}

void* editor::Conector::loadSharedLibrary(const std::string& libPath) {
    #ifdef _WIN32
        HMODULE libHandle = LoadLibrary(libPath.c_str());
        if (!libHandle) {
            Out::error("Failed to load library: %s (Error code: %lu)", libPath.c_str(), GetLastError());
            return nullptr;
        }
        return libHandle;
    #else
        void* libHandle = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!libHandle) {
            Out::error("Failed to load library: %s (Error: %s)", libPath.c_str(), dlerror());
            return nullptr;
        }
        return libHandle;
    #endif
}

void editor::Conector::unloadSharedLibrary(void* libHandle) {
    #ifdef _WIN32
        if (libHandle) {
            FreeLibrary(static_cast<HMODULE>(libHandle));
            std::cout << "Library unloaded successfully.\n";
        }
    #else
        if (libHandle) {
            dlclose(libHandle);
            std::cout << "Library unloaded successfully.\n";
        }
    #endif
}

bool editor::Conector::connect(const fs::path& buildPath, std::string libName){
    // Disconnect any existing connection first
    disconnect();

    std::string libPath = libName;
    #ifdef _WIN32
        libPath = libPath + ".dll";
    #else
        libPath = libPath + ".so";
    #endif

    fs::path fullLibPath = buildPath / fs::path(libPath);
    if (fileExists(fullLibPath)) {
        libHandle = loadSharedLibrary(fullLibPath.string());
        if (libHandle) {
            Out::info("Successfully connected to library");
            return true;
        }
    } else {
        Out::error("Library file not found: %s", libPath.c_str());
    }

    return false;
}

void editor::Conector::disconnect(){
    if (libHandle) {
        unloadSharedLibrary(libHandle);
        libHandle = nullptr;

        Out::info("Disconnected from library successfully");
    }
}

void editor::Conector::init(Scene* scene){
    if (!libHandle) {
        Out::error("Cannot execute: Not connected to library");
        return;
    }

    using InitScriptsFunc = void (*)(Scene*);
    #ifdef _WIN32
        InitScriptsFunc initScriptsFn = reinterpret_cast<InitScriptsFunc>(GetProcAddress(static_cast<HMODULE>(libHandle), "initScripts"));
        if (!initScriptsFn) {
            Out::error("Failed to find function 'initScripts' in the library (Error code: %lu)", GetLastError());
        }
    #else
        dlerror(); // clear any existing error
        InitScriptsFunc initScriptsFn = reinterpret_cast<InitScriptsFunc>(dlsym(libHandle, "initScripts"));
        const char* err = dlerror();
        if (err) {
            Out::error("Failed to find function 'initScripts' in the library (Error: %s)", err);
            initScriptsFn = nullptr;
        }
    #endif

    if (initScriptsFn) {
        try {
            initScriptsFn(scene);
        } catch (const std::exception& e) {
            Out::error("Exception in initScripts(): %s", e.what());
        } catch (...) {
            Out::error("Unknown exception in initScripts()");
        }
    }
}

void editor::Conector::cleanup(Scene* scene){
    if (!libHandle) {
        Out::error("Cannot cleanup: Not connected to library");
        return;
    }

    using CleanupScriptsFunc = void (*)(Scene*);
    #ifdef _WIN32
        CleanupScriptsFunc cleanupScriptsFn = reinterpret_cast<CleanupScriptsFunc>(GetProcAddress(static_cast<HMODULE>(libHandle), "cleanupScripts"));
        if (!cleanupScriptsFn) {
            Out::error("Failed to find function 'cleanupScripts' in the library (Error code: %lu)", GetLastError());
        }
    #else
        dlerror(); // clear any existing error
        CleanupScriptsFunc cleanupScriptsFn = reinterpret_cast<CleanupScriptsFunc>(dlsym(libHandle, "cleanupScripts"));
        const char* err = dlerror();
        if (err) {
            Out::error("Failed to find function 'cleanupScripts' in the library (Error: %s)", err);
            cleanupScriptsFn = nullptr;
        }
    #endif

    if (cleanupScriptsFn) {
        try {
            cleanupScriptsFn(scene);
        } catch (const std::exception& e) {
            Out::error("Exception in cleanupScripts(): %s", e.what());
        } catch (...) {
            Out::error("Unknown exception in cleanupScripts()");
        }
    } else {
        Out::warning("Cleanup function not found in library");
    }
}

bool editor::Conector::isLibraryConnected() const {
    return libHandle != nullptr;
}