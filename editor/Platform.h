#ifndef EDITORPLATFORM_H
#define EDITORPLATFORM_H

#include "System.h"
#include "Project.h"
#include <cstdarg>

namespace doriax::editor{

    class Platform : public System{
    private:
        Project* project;

        static int width;
        static int height;

    public:
        Platform(Project* project);

        static bool setSizes(int width, int height);

        int getScreenWidth() override;
        int getScreenHeight() override;

        std::string getAssetPath() override;

        void platformLog(const int type, const char *fmt, va_list args) override;
    };

}

#endif /* EDITORPLATFORM_H */