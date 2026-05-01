#pragma once

#include "command/Command.h"
#include "PropertyCmd.h"
#include <vector>
#include <memory>
#include <utility>

namespace doriax::editor {

    class MultiPropertyCmd : public Command {
    private:
        std::vector<std::unique_ptr<Command>> commands;

    public:
        MultiPropertyCmd() {
        }

        template<typename T>
        void addPropertyCmd(Project* project, uint32_t sceneId, Entity entity, ComponentType type, 
                           std::string propertyName, T newValue, std::function<void()> onValueChanged = nullptr) {
            commands.push_back(std::make_unique<PropertyCmd<T>>(
                project, sceneId, entity, type, propertyName, newValue, onValueChanged));
        }

        void addCommand(std::unique_ptr<Command> command) {
            commands.push_back(std::move(command));
        }

        bool execute() override {
            std::vector<Command*> executedCommands;
            for (auto& cmd : commands) {
                if (!cmd->execute()){
                    for (auto it = executedCommands.rbegin(); it != executedCommands.rend(); ++it) {
                        (*it)->undo();
                    }
                    return false;
                }
                executedCommands.push_back(cmd.get());
            }
            return true;
        }

        void undo() override {
            // Undo in reverse order
            for (auto it = commands.rbegin(); it != commands.rend(); ++it) {
                (*it)->undo();
            }
        }

        bool mergeWith(Command* otherCommand) override {
            MultiPropertyCmd* otherMultiCmd = dynamic_cast<MultiPropertyCmd*>(otherCommand);
            if (otherMultiCmd) {
                bool anyMerged = false;
                
                for (size_t i = 0; i < otherMultiCmd->commands.size(); i++) {
                    bool merged = false;
                    
                    for (size_t j = 0; j < commands.size(); j++) {
                        if (commands[j]->mergeWith(otherMultiCmd->commands[i].get())) {
                            merged = true;
                            anyMerged = true;
                            break;
                        }
                    }

                    if (!merged) {
                        commands.push_back(std::move(otherMultiCmd->commands[i]));
                    }
                }
                
                return anyMerged;
            }
            
            return false;
        }

    };

}