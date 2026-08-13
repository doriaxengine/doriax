//
// (c) 2026 Eduardo Doria.
//

#ifndef DORIAX_INPUT_ACTION_MAP_H
#define DORIAX_INPUT_ACTION_MAP_H

#include "Export.h"
#include "input/InputAction.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

namespace doriax {

    // InputActionMap organizes InputActions by context (gameplay, menu, cutscene, etc.)
    // and manages activation/deactivation of action sets.
    //
    // Usage:
    //   auto gameplayMap = InputActionMap::create("gameplay");
    //   gameplayMap->addAction("move", InputActionType::Analog2D);
    //   gameplayMap->addAction("jump", InputActionType::Digital);
    //   gameplayMap->enable();

    class DORIAX_API InputActionMap {
    public:
        using Ptr = std::shared_ptr<InputActionMap>;

        static Ptr create(const std::string& name);

        explicit InputActionMap(const std::string& name);

        const std::string& name() const;

        // Add or retrieve an action in this map
        std::shared_ptr<InputAction> addAction(
            const std::string& actionName,
            InputActionType type = InputActionType::Digital
        );

        std::shared_ptr<InputAction> getAction(const std::string& actionName);
        const std::shared_ptr<InputAction> getAction(const std::string& actionName) const;

        bool hasAction(const std::string& actionName) const;
        std::vector<std::string> listActions() const;

        // Enable/disable this action map
        void enable();
        void disable();
        bool isEnabled() const;

        // Process input frame (clear per-frame state)
        void processFrame();

        // Remove an action
        void removeAction(const std::string& actionName);

        // Clear all actions
        void clear();

    private:
        std::string name_;
        bool enabled_ = false;
        std::unordered_map<std::string, std::shared_ptr<InputAction>> actions_;
    };

    // InputActionMapRegistry - global registry of action maps
    class DORIAX_API InputActionMapRegistry {
    public:
        static InputActionMapRegistry& get();

        // Create and register an action map
        InputActionMap::Ptr createMap(const std::string& name);

        // Get a registered action map
        InputActionMap::Ptr getMap(const std::string& name);

        // Activate an action map (activates only this map, deactivates others)
        void activateMap(const std::string& name);

        // Get the currently active action map
        InputActionMap::Ptr getActiveMap();

        // List all registered map names
        std::vector<std::string> listMaps() const;

        // Remove a map
        void removeMap(const std::string& name);

        // Clear all maps
        void clear();

        // Process frame for active map
        void processFrame();

    private:
        InputActionMapRegistry() = default;
        std::unordered_map<std::string, InputActionMap::Ptr> maps_;
        std::string activeMapName_;
    };

}

#endif // DORIAX_INPUT_ACTION_MAP_H
