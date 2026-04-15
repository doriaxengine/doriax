#pragma once

#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include "imgui.h"
#include "Catalog.h"

namespace doriax {
namespace editor {

struct ComponentEntry {
    ComponentType type;
    const char* icon;
    const char* name;
    const char* description;
    ImVec4 color;
};

struct ComponentCategory {
    const char* name;
    const char* icon;
    ImVec4 color;
    std::vector<ComponentEntry> entries;
};

class ComponentAddDialog {
private:
    bool m_isOpen = false;
    char m_searchBuffer[128] = "";
    int m_hoveredIndex = -1;
    bool m_justOpened = false;
    
    std::vector<ComponentCategory> m_categories;
    std::function<void(ComponentType)> m_onAdd;
    std::function<void()> m_onCancel;
    std::function<bool(ComponentType)> m_canAddComponent;

    void initializeCategories();

public:
    ComponentAddDialog();
    ~ComponentAddDialog() = default;

    void open(
        std::function<void(ComponentType)> onAdd,
        std::function<bool(ComponentType)> canAddComponent,
        std::function<void()> onCancel = nullptr
    );

    void show();
    bool isOpen() const { return m_isOpen; }
    void close() { m_isOpen = false; }
};

} // namespace editor
} // namespace doriax