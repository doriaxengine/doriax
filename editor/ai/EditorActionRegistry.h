// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "AiTypes.h"

#include <string>
#include <vector>

namespace doriax::editor::ai {

struct ValidationResult {
    bool ok = false;
    std::string error;
};

class EditorActionRegistry {
public:
    static std::vector<ToolDefinition> tools();
    static bool hasTool(const std::string& name);
    static bool isReadOnly(const std::string& name);
    static ToolDefinition getTool(const std::string& name);
    static ValidationResult validate(const std::string& name, const Json& arguments);
    static std::string describe(const std::string& name, const Json& arguments);
    // Engine rules for any model using the tools: the chat's prompt and get_editor_guidance
    static std::string guidance();
    // The start of guidance(), short enough for the MCP instructions
    static std::string essentialGuidance();
};

} // namespace doriax::editor::ai
