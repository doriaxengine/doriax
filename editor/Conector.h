#pragma once

#include <filesystem>
#include <string>
#include "Scene.h"

namespace fs = std::filesystem;

namespace doriax::editor{

    class Conector{
    private:
        void* libHandle;

        bool fileExists(const fs::path& path);
        void* loadSharedLibrary(const std::string& libPath);
        void unloadSharedLibrary(void* libHandle);

    public:
        Conector();
        virtual ~Conector();

        bool connect(const fs::path& buildPath, std::string libName);
        void disconnect();

        void init(Scene* sceneProject);
        void cleanup(Scene* sceneProject);

        bool isLibraryConnected() const;
    };

}