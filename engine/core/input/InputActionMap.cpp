//
// (c) 2026 Eduardo Doria.
//

#include "input/InputActionMap.h"
#include "Log.h"

namespace doriax {

    // InputActionMap Implementation

    InputActionMap::Ptr InputActionMap::create(const std::string& name) {
        return std::make_shared<InputActionMap>(name);
    }

    InputActionMap::InputActionMap(const std::string& name) : name_(name) {
    }

    const std::string& InputActionMap::name() const {
        return name_;
    }

    std::shared_ptr<InputAction> InputActionMap::addAction(
        const std::string& actionName,
        InputActionType type
    ) {
        auto action = std::make_shared<InputAction>(actionName, type);
        actions_[actionName] = action;
        return action;
    }

    std::shared_ptr<InputAction> InputActionMap::getAction(const std::string& actionName) {
        auto it = actions_.find(actionName);
        return it != actions_.end() ? it->second : nullptr;
    }

    const std::shared_ptr<InputAction> InputActionMap::getAction(const std::string& actionName) const {
        auto it = actions_.find(actionName);
        return it != actions_.end() ? it->second : nullptr;
    }

    bool InputActionMap::hasAction(const std::string& actionName) const {
        return actions_.find(actionName) != actions_.end();
    }

    std::vector<std::string> InputActionMap::listActions() const {
        std::vector<std::string> result;
        result.reserve(actions_.size());
        for (const auto& [name, action] : actions_) {
            (void)action;  // Suppress unused warning
            result.push_back(name);
        }
        return result;
    }

    void InputActionMap::enable() {
        enabled_ = true;
    }

    void InputActionMap::disable() {
        enabled_ = false;
    }

    bool InputActionMap::isEnabled() const {
        return enabled_;
    }

    void InputActionMap::processFrame() {
        if (!enabled_) return;

        for (auto& [name, action] : actions_) {
            (void)name;  // Suppress unused warning
            action->clearFrame();
        }
    }

    void InputActionMap::removeAction(const std::string& actionName) {
        actions_.erase(actionName);
    }

    void InputActionMap::clear() {
        actions_.clear();
    }

    // InputActionMapRegistry Implementation

    InputActionMapRegistry& InputActionMapRegistry::get() {
        static InputActionMapRegistry registry;
        return registry;
    }

    InputActionMap::Ptr InputActionMapRegistry::createMap(const std::string& name) {
        auto map = InputActionMap::create(name);
        maps_[name] = map;
        Log::info("InputActionMapRegistry: created map '%s'", name.c_str());
        return map;
    }

    InputActionMap::Ptr InputActionMapRegistry::getMap(const std::string& name) {
        auto it = maps_.find(name);
        return it != maps_.end() ? it->second : nullptr;
    }

    void InputActionMapRegistry::activateMap(const std::string& name) {
        // Disable currently active map
        if (!activeMapName_.empty()) {
            auto active = getMap(activeMapName_);
            if (active) {
                active->disable();
            }
        }

        // Enable new map
        auto newMap = getMap(name);
        if (newMap) {
            newMap->enable();
            activeMapName_ = name;
            Log::info("InputActionMapRegistry: activated map '%s'", name.c_str());
        } else {
            Log::error("InputActionMapRegistry: map '%s' not found", name.c_str());
        }
    }

    InputActionMap::Ptr InputActionMapRegistry::getActiveMap() {
        return activeMapName_.empty() ? nullptr : getMap(activeMapName_);
    }

    std::vector<std::string> InputActionMapRegistry::listMaps() const {
        std::vector<std::string> result;
        result.reserve(maps_.size());
        for (const auto& [name, map] : maps_) {
            (void)map;  // Suppress unused warning
            result.push_back(name);
        }
        return result;
    }

    void InputActionMapRegistry::removeMap(const std::string& name) {
        maps_.erase(name);
    }

    void InputActionMapRegistry::clear() {
        maps_.clear();
        activeMapName_.clear();
    }

    void InputActionMapRegistry::processFrame() {
        auto active = getActiveMap();
        if (active) {
            active->processFrame();
        }
    }

}
