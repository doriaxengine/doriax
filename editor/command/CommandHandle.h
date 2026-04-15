#ifndef COMMANDHANDLE_H
#define COMMANDHANDLE_H

#include "CommandHistory.h"
#include <map>

namespace doriax::editor{

    class CommandHandle{

    private:
        static std::map<size_t, CommandHistory*> historys;

    public:
        static CommandHistory* get(size_t sceneId);

        static void remove(size_t sceneId);
    };

}

#endif /* COMMANDHANDLE_H */