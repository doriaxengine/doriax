#ifndef COMMANDHISTORY_H
#define COMMANDHISTORY_H

#include "Command.h"
#include <vector>
#include <cstddef>

namespace doriax::editor{

    class CommandHistory{

    private:
        std::vector<Command*> list;
        size_t index = 0; // real index is (index-1)

    public:
        virtual ~CommandHistory();

        void addCommand(Command* cmd);
        void addCommandNoMerge(Command* cmd);

        void undo();
        void redo();

        bool canUndo() const;
        bool canRedo() const;

        void clear();
    };

}

#endif /* COMMANDHISTORY_H */