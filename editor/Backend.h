#pragma once

#include "App.h"

namespace doriax::editor{
    class Backend{
    private:
        static App app;
        static std::string title;

    public:
        static int init(int argc, char **argv);

        static App& getApp();

        static void disableMouseCursor();
        static void enableMouseCursor();
        static void closeWindow();

        static void updateWindowTitle(const std::string& projectName);

        static void* getNFDWindowHandle();
    };

}
