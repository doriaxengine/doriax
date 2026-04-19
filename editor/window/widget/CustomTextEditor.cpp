#include "CustomTextEditor.h"
#include "SemanticSuggestions.h"
#include "engine_api_suggestions.h"
#include "external/IconsFontAwesome6.h"
#include "util/UIUtils.h"
#include "App.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <sstream>
#include <cstring>
#include <regex>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace doriax::editor {

CustomTextEditor::CustomTextEditor()
    : primaryCursor(0)
    , undoIndex(0)
    , language(SyntaxLanguage::None)
    , readOnly(false)
    , tabSize(4)
    , showLineNumbers(true)
    , showWhitespace(false)
    , autoIndent(true)
    , highlightCurrentLine(true)
    , matchBrackets(true)
    , showMinimap(false)
    , autoComplete(true)
    , isDragging(false)
    , isDraggingText(false)
    , mayDragText(false)
    , isDoubleClick(false)
    , isTripleClick(false)
    , clickCount(0)
    , scrollX(0)
    , scrollY(0)
    , showAutoComplete(false)
    , autoCompleteIndex(0)
    , suggestionIndex(0)
    , suggestionsHovered(false)
    , showParamHint(false)
    , paramHintActiveParam(0)
    , paramHintOverloadIndex(0)
    , showTooltipFlag(false)
    , currentSearchResult(-1)
    , showFindDialog(false)
    , showReplaceInput(false)
    , findCaseSensitive(false)
    , findWholeWord(false)
    , charWidth(0)
    , lineHeight(0)
    , lineNumberWidth(0)
    , leftMargin(10)
    , textStartX(0)
    , suggestions(std::make_unique<SemanticSuggestions>())
    , scrollToSuggestion(false)
{
    lines.push_back("");
    cursors.push_back(Cursor());
    memset(findInputBuffer, 0, sizeof(findInputBuffer));
    memset(replaceInputBuffer, 0, sizeof(replaceInputBuffer));
    initializePalette();
    initializeLanguage();
    initializeSuggestions();
}

CustomTextEditor::~CustomTextEditor() {
}

void CustomTextEditor::initializePalette() {
    // VS Code Dark+ inspired color scheme
    backgroundColor = ImVec4(0.118f, 0.118f, 0.118f, 1.0f);
    lineNumberColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    currentLineColor = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    selectionColor = ImVec4(0.26f, 0.40f, 0.60f, 0.5f);
    cursorColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    matchingBracketColor = ImVec4(0.5f, 0.5f, 0.5f, 0.4f);
    searchHighlightColor = ImVec4(0.9f, 0.8f, 0.3f, 0.3f);

    palette[static_cast<int>(TokenType::Default)] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
    palette[static_cast<int>(TokenType::Keyword)] = ImVec4(0.57f, 0.44f, 0.86f, 1.0f);      // Purple
    palette[static_cast<int>(TokenType::Type)] = ImVec4(0.31f, 0.69f, 0.78f, 1.0f);         // Cyan
    palette[static_cast<int>(TokenType::Number)] = ImVec4(0.71f, 0.82f, 0.53f, 1.0f);       // Light green
    palette[static_cast<int>(TokenType::String)] = ImVec4(0.81f, 0.55f, 0.42f, 1.0f);       // Orange
    palette[static_cast<int>(TokenType::Comment)] = ImVec4(0.42f, 0.55f, 0.35f, 1.0f);      // Green
    palette[static_cast<int>(TokenType::MultiLineComment)] = ImVec4(0.42f, 0.55f, 0.35f, 1.0f);
    palette[static_cast<int>(TokenType::Preprocessor)] = ImVec4(0.61f, 0.43f, 0.62f, 1.0f); // Magenta
    palette[static_cast<int>(TokenType::Identifier)] = ImVec4(0.61f, 0.82f, 0.98f, 1.0f);   // Light blue
    palette[static_cast<int>(TokenType::Punctuation)] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
    palette[static_cast<int>(TokenType::Operator)] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
    palette[static_cast<int>(TokenType::Function)] = ImVec4(0.86f, 0.82f, 0.53f, 1.0f);     // Yellow
}

void CustomTextEditor::initializeLanguage() {
    languageDef = LanguageDefinition();

    switch (language) {
        case SyntaxLanguage::Cpp:
            languageDef.keywords = {
                "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
                "break", "case", "catch", "class", "compl", "concept", "const", "consteval",
                "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
                "co_yield", "decltype", "default", "delete", "do", "dynamic_cast", "else",
                "enum", "explicit", "export", "extern", "false", "for", "friend", "goto",
                "if", "inline", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
                "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
                "register", "reinterpret_cast", "requires", "return", "sizeof", "static",
                "static_assert", "static_cast", "struct", "switch", "template", "this",
                "thread_local", "throw", "true", "try", "typedef", "typeid", "typename",
                "union", "using", "virtual", "volatile", "while", "xor", "xor_eq",
                "override", "final"
            };
            languageDef.types = {
                "void", "bool", "char", "wchar_t", "char8_t", "char16_t", "char32_t",
                "short", "int", "long", "signed", "unsigned", "float", "double",
                "int8_t", "int16_t", "int32_t", "int64_t",
                "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                "size_t", "ptrdiff_t", "nullptr_t",
                "string", "vector", "map", "unordered_map", "set", "unordered_set",
                "array", "list", "deque", "queue", "stack", "pair", "tuple",
                "unique_ptr", "shared_ptr", "weak_ptr",
                "function", "optional", "variant", "any"
            };
            // Add engine types from auto-generated header
            for (const auto& t : getEngineTypeNames()) languageDef.types.insert(t);
            languageDef.singleLineComment = "//";
            languageDef.multiLineCommentStart = "/*";
            languageDef.multiLineCommentEnd = "*/";
            languageDef.preprocessorPrefix = "#";
            break;

        case SyntaxLanguage::Lua:
            languageDef.keywords = {
                "and", "break", "do", "else", "elseif", "end", "false", "for",
                "function", "goto", "if", "in", "local", "nil", "not", "or",
                "repeat", "return", "then", "true", "until", "while"
            };
            languageDef.types = {
                "string", "number", "boolean", "table", "function", "thread",
                "userdata"
            };
            // Add engine types from auto-generated header
            for (const auto& t : getEngineTypeNames()) languageDef.types.insert(t);
            languageDef.builtinFunctions = {
                "assert", "collectgarbage", "dofile", "error", "getmetatable",
                "ipairs", "load", "loadfile", "next", "pairs", "pcall", "print",
                "rawequal", "rawget", "rawlen", "rawset", "require", "select",
                "setmetatable", "tonumber", "tostring", "type", "xpcall",
                "coroutine", "debug", "io", "math", "os", "package", "string", "table", "utf8"
            };
            // Add engine builtins (static classes + enums) from auto-generated header
            for (const auto& b : getEngineBuiltinNames()) languageDef.builtinFunctions.insert(b);
            for (const auto& e : getEngineEnumNames()) languageDef.builtinFunctions.insert(e);
            languageDef.singleLineComment = "--";
            languageDef.multiLineCommentStart = "--[[";
            languageDef.multiLineCommentEnd = "]]";
            break;

        case SyntaxLanguage::CMake:
            languageDef.keywords = {
                "if", "elseif", "else", "endif", "foreach", "endforeach", "while", "endwhile",
                "function", "endfunction", "macro", "endmacro", "return", "break", "continue",
                "option", "set", "unset", "list", "string", "math", "file", "configure_file",
                "add_executable", "add_library", "add_subdirectory", "add_custom_command",
                "add_custom_target", "add_dependencies", "add_definitions", "add_compile_definitions",
                "add_compile_options", "add_link_options", "target_link_libraries",
                "target_include_directories", "target_compile_definitions", "target_compile_options",
                "target_compile_features", "target_sources", "target_link_options",
                "include", "include_directories", "link_directories", "link_libraries",
                "find_package", "find_library", "find_path", "find_file", "find_program",
                "project", "cmake_minimum_required", "message", "install", "export",
                "set_target_properties", "set_property", "get_property", "get_target_property",
                "get_filename_component", "get_directory_property", "define_property",
                "enable_testing", "add_test", "set_tests_properties",
                "execute_process", "cmake_policy", "cmake_parse_arguments"
            };
            languageDef.types = {
                "BOOL", "STRING", "PATH", "FILEPATH", "INTERNAL", "CACHE", "FORCE",
                "PARENT_SCOPE", "GLOBAL", "DIRECTORY", "TARGET", "SOURCE", "TEST",
                "PRIVATE", "PUBLIC", "INTERFACE", "IMPORTED", "SHARED", "STATIC", "MODULE",
                "OBJECT", "UNKNOWN", "ALIAS", "REQUIRED", "QUIET", "COMPONENTS", "CONFIG",
                "TRUE", "FALSE", "ON", "OFF", "YES", "NO",
                "AND", "OR", "NOT", "COMMAND", "POLICY", "TARGET", "TEST", "DEFINED",
                "EXISTS", "IS_DIRECTORY", "IS_SYMLINK", "IS_ABSOLUTE", "MATCHES",
                "LESS", "GREATER", "EQUAL", "LESS_EQUAL", "GREATER_EQUAL",
                "STRLESS", "STRGREATER", "STREQUAL", "STRLESS_EQUAL", "STRGREATER_EQUAL",
                "VERSION_LESS", "VERSION_GREATER", "VERSION_EQUAL"
            };
            languageDef.builtinFunctions = {
                "CMAKE_SOURCE_DIR", "CMAKE_BINARY_DIR", "CMAKE_CURRENT_SOURCE_DIR",
                "CMAKE_CURRENT_BINARY_DIR", "CMAKE_CURRENT_LIST_DIR", "CMAKE_CURRENT_LIST_FILE",
                "PROJECT_SOURCE_DIR", "PROJECT_BINARY_DIR", "PROJECT_NAME", "PROJECT_VERSION",
                "CMAKE_CXX_STANDARD", "CMAKE_C_STANDARD", "CMAKE_BUILD_TYPE",
                "CMAKE_INSTALL_PREFIX", "CMAKE_MODULE_PATH", "CMAKE_PREFIX_PATH",
                "CMAKE_SYSTEM_NAME", "CMAKE_SYSTEM_VERSION", "CMAKE_SYSTEM_PROCESSOR",
                "CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER", "CMAKE_LINKER",
                "CMAKE_CXX_FLAGS", "CMAKE_C_FLAGS", "CMAKE_EXE_LINKER_FLAGS",
                "CMAKE_SHARED_LINKER_FLAGS", "CMAKE_STATIC_LINKER_FLAGS",
                "WIN32", "UNIX", "APPLE", "MSVC", "MINGW", "LINUX"
            };
            languageDef.singleLineComment = "#";
            languageDef.multiLineCommentStart = "#[[";
            languageDef.multiLineCommentEnd = "]]";
            break;

        default:
            break;
    }

    tokenizeAll();
}

void CustomTextEditor::initializeSuggestions() {
    if (!suggestions) return;

    // Configure the suggestions engine
    suggestions->SetMinPrefixLength(1);
    suggestions->SetMaxSuggestions(15);
    suggestions->SetFuzzyMatching(true);
    suggestions->SetCaseSensitive(false);

    // Pass language definitions to suggestions engine
    suggestions->SetKeywords(languageDef.keywords);
    suggestions->SetTypes(languageDef.types);
    suggestions->SetBuiltinFunctions(languageDef.builtinFunctions);

    // Add common snippets for C++
    if (language == SyntaxLanguage::Cpp) {
        suggestions->AddSnippet("if", "if (${1:condition}) {\n\t${2}\n}", "if statement");
        suggestions->AddSnippet("else", "else {\n\t${1}\n}", "else clause");
        suggestions->AddSnippet("elif", "else if (${1:condition}) {\n\t${2}\n}", "else if statement");
        suggestions->AddSnippet("for", "for (${1:int i = 0}; ${2:i < count}; ${3:++i}) {\n\t${4}\n}", "for loop");
        suggestions->AddSnippet("fori", "for (int ${1:i} = 0; ${1:i} < ${2:count}; ++${1:i}) {\n\t${3}\n}", "for loop with index");
        suggestions->AddSnippet("foreach", "for (const auto& ${1:item} : ${2:container}) {\n\t${3}\n}", "range-based for loop");
        suggestions->AddSnippet("while", "while (${1:condition}) {\n\t${2}\n}", "while loop");
        suggestions->AddSnippet("do", "do {\n\t${1}\n} while (${2:condition});", "do-while loop");
        suggestions->AddSnippet("switch", "switch (${1:expression}) {\n\tcase ${2:value}:\n\t\t${3}\n\t\tbreak;\n\tdefault:\n\t\tbreak;\n}", "switch statement");
        suggestions->AddSnippet("class", "class ${1:ClassName} {\npublic:\n\t${1:ClassName}();\n\t~${1:ClassName}();\n\nprivate:\n\t${2}\n};", "class definition");
        suggestions->AddSnippet("struct", "struct ${1:StructName} {\n\t${2}\n};", "struct definition");
        suggestions->AddSnippet("func", "${1:void} ${2:functionName}(${3}) {\n\t${4}\n}", "function definition");
        suggestions->AddSnippet("main", "int main(int argc, char* argv[]) {\n\t${1}\n\treturn 0;\n}", "main function");
        suggestions->AddSnippet("inc", "#include <${1:header}>", "include system header");
        suggestions->AddSnippet("incp", "#include \"${1:header}\"", "include local header");
        suggestions->AddSnippet("ifndef", "#ifndef ${1:GUARD}\n#define ${1:GUARD}\n\n${2}\n\n#endif // ${1:GUARD}", "include guard");
        suggestions->AddSnippet("pragma", "#pragma once", "pragma once");
        suggestions->AddSnippet("cout", "std::cout << ${1} << std::endl;", "cout statement");
        suggestions->AddSnippet("cerr", "std::cerr << ${1} << std::endl;", "cerr statement");
        suggestions->AddSnippet("try", "try {\n\t${1}\n} catch (${2:const std::exception& e}) {\n\t${3}\n}", "try-catch block");
        suggestions->AddSnippet("lambda", "[${1:capture}](${2:params}) {\n\t${3}\n}", "lambda expression");
        suggestions->AddSnippet("nullptr", "nullptr", "null pointer");
        suggestions->AddSnippet("auto", "auto ${1:var} = ${2:value};", "auto variable");
        suggestions->AddSnippet("const", "const ${1:type} ${2:name} = ${3:value};", "const variable");
        suggestions->AddSnippet("constexpr", "constexpr ${1:type} ${2:name} = ${3:value};", "constexpr variable");
    } else if (language == SyntaxLanguage::Lua) {
        suggestions->AddSnippet("if", "if ${1:condition} then\n\t${2}\nend", "if statement");
        suggestions->AddSnippet("ife", "if ${1:condition} then\n\t${2}\nelse\n\t${3}\nend", "if-else statement");
        suggestions->AddSnippet("elif", "elseif ${1:condition} then\n\t${2}", "elseif clause");
        suggestions->AddSnippet("for", "for ${1:i} = ${2:1}, ${3:10} do\n\t${4}\nend", "numeric for loop");
        suggestions->AddSnippet("forp", "for ${1:k}, ${2:v} in pairs(${3:table}) do\n\t${4}\nend", "pairs loop");
        suggestions->AddSnippet("fori", "for ${1:i}, ${2:v} in ipairs(${3:table}) do\n\t${4}\nend", "ipairs loop");
        suggestions->AddSnippet("while", "while ${1:condition} do\n\t${2}\nend", "while loop");
        suggestions->AddSnippet("repeat", "repeat\n\t${1}\nuntil ${2:condition}", "repeat-until loop");
        suggestions->AddSnippet("func", "function ${1:name}(${2:args})\n\t${3}\nend", "function definition");
        suggestions->AddSnippet("lfunc", "local function ${1:name}(${2:args})\n\t${3}\nend", "local function");
        suggestions->AddSnippet("local", "local ${1:name} = ${2:value}", "local variable");
        suggestions->AddSnippet("req", "local ${1:module} = require(\"${2:module}\")", "require module");
        suggestions->AddSnippet("ret", "return ${1:value}", "return statement");
        suggestions->AddSnippet("print", "print(${1:value})", "print statement");
    } else if (language == SyntaxLanguage::CMake) {
        suggestions->AddSnippet("if", "if(${1:condition})\n\t${2}\nendif()", "if statement");
        suggestions->AddSnippet("ife", "if(${1:condition})\n\t${2}\nelse()\n\t${3}\nendif()", "if-else statement");
        suggestions->AddSnippet("foreach", "foreach(${1:item} IN LISTS ${2:list})\n\t${3}\nendforeach()", "foreach loop");
        suggestions->AddSnippet("func", "function(${1:name} ${2:args})\n\t${3}\nendfunction()", "function definition");
        suggestions->AddSnippet("macro", "macro(${1:name} ${2:args})\n\t${3}\nendmacro()", "macro definition");
        suggestions->AddSnippet("add_exe", "add_executable(${1:target}\n\t${2:sources}\n)", "add executable");
        suggestions->AddSnippet("add_lib", "add_library(${1:target} ${2:STATIC}\n\t${3:sources}\n)", "add library");
        suggestions->AddSnippet("target_link", "target_link_libraries(${1:target}\n\t${2:PRIVATE}\n\t${3:libraries}\n)", "link libraries");
        suggestions->AddSnippet("target_inc", "target_include_directories(${1:target}\n\t${2:PRIVATE}\n\t${3:directories}\n)", "include directories");
        suggestions->AddSnippet("find", "find_package(${1:package} ${2:REQUIRED})", "find package");
        suggestions->AddSnippet("set", "set(${1:variable} ${2:value})", "set variable");
        suggestions->AddSnippet("msg", "message(STATUS \"${1:message}\")", "status message");
        suggestions->AddSnippet("option", "option(${1:OPTION_NAME} \"${2:description}\" ${3:OFF})", "option definition");
        suggestions->AddSnippet("cmake_min", "cmake_minimum_required(VERSION ${1:3.16})", "cmake minimum version");
        suggestions->AddSnippet("project", "project(${1:name}\n\tVERSION ${2:1.0.0}\n\tLANGUAGES ${3:CXX}\n)", "project definition");
    }

    // Add engine API suggestions for Lua and C++
    if (language == SyntaxLanguage::Lua || language == SyntaxLanguage::Cpp) {
        addEngineAPISuggestions();
    }
}

void CustomTextEditor::addEngineAPISuggestions() {
    if (!suggestions) return;

    static const auto& apiSymbols = getEngineAPISymbols();

    static const std::unordered_map<std::string, SuggestionKind> kindMap = {
        {"Class", SuggestionKind::Class},
        {"Enum", SuggestionKind::Enum},
        {"EnumMember", SuggestionKind::EnumMember},
        {"Method", SuggestionKind::Method},
        {"Function", SuggestionKind::Function},
        {"Property", SuggestionKind::Property},
        {"Constant", SuggestionKind::Constant},
        {"Variable", SuggestionKind::Variable},
    };

    for (const auto& sym : apiSymbols) {
        SuggestionKind sk = SuggestionKind::Variable;
        auto it = kindMap.find(sym.kind);
        if (it != kindMap.end()) sk = it->second;
        suggestions->AddSymbol(sym.name, sk, sym.detail, sym.parent ? sym.parent : "");

        // Build inheritance map from class detail strings like "class Mesh : Object"
        if (sk == SuggestionKind::Class && sym.detail) {
            std::string detail = sym.detail;
            size_t colonPos = detail.find(" : ");
            if (colonPos != std::string::npos) {
                std::string className = sym.name;
                std::string parentName = detail.substr(colonPos + 3);
                // Trim any trailing whitespace
                while (!parentName.empty() && parentName.back() == ' ') parentName.pop_back();
                suggestions->SetClassParent(className, parentName);
            }
        }
    }
}

void CustomTextEditor::SetLanguage(SyntaxLanguage lang) {
    if (language != lang) {
        language = lang;
        initializeLanguage();
        initializeSuggestions();
    }
}

const char* CustomTextEditor::GetLanguageName() const {
    switch (language) {
        case SyntaxLanguage::Cpp: return "C++";
        case SyntaxLanguage::Lua: return "Lua";
        case SyntaxLanguage::CMake: return "CMake";
        default: return "Plain Text";
    }
}

void CustomTextEditor::SetText(const std::string& text) {
    lines.clear();
    lineTokens.clear();

    std::string line;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    lines.push_back(line);

    tokenizeAll();

    cursors.clear();
    cursors.push_back(Cursor());
    primaryCursor = 0;

    // Reset scroll position to top
    scrollX = 0;
    scrollY = 0;

    undoBuffer.clear();
    undoIndex = 0;
}

std::string CustomTextEditor::GetText() const {
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i < lines.size() - 1) {
            result += '\n';
        }
    }
    return result;
}

std::vector<std::string> CustomTextEditor::GetTextLines() const {
    return lines;
}

void CustomTextEditor::SetCursorPosition(int line, int column) {
    cursors.clear();
    Cursor cursor;
    cursor.position.line = std::clamp(line, 0, static_cast<int>(lines.size()) - 1);
    cursor.position.column = std::clamp(column, 0, static_cast<int>(lines[cursor.position.line].size()));
    cursor.selection.start = cursor.position;
    cursor.selection.end = cursor.position;
    cursors.push_back(cursor);
    primaryCursor = 0;
}

void CustomTextEditor::GetCursorPosition(int& line, int& column) const {
    if (!cursors.empty()) {
        line = cursors[primaryCursor].position.line;
        column = cursors[primaryCursor].position.column;
    } else {
        line = 0;
        column = 0;
    }
}

bool CustomTextEditor::HasSelection() const {
    for (const auto& cursor : cursors) {
        if (!cursor.selection.isEmpty()) {
            return true;
        }
    }
    return false;
}

std::string CustomTextEditor::GetSelectedText() const {
    std::string result;
    bool first = true;
    for (const auto& cursor : cursors) {
        if (!cursor.selection.isEmpty()) {
            if (!first) result += '\n';
            result += getRange(cursor.selection.getMin(), cursor.selection.getMax());
            first = false;
        }
    }
    return result;
}

void CustomTextEditor::SelectAll() {
    cursors.clear();
    Cursor cursor;
    cursor.selection.start = TextPosition(0, 0);
    cursor.selection.end = TextPosition(static_cast<int>(lines.size()) - 1, 
                                         static_cast<int>(lines.back().size()));
    cursor.position = cursor.selection.end;
    cursors.push_back(cursor);
    primaryCursor = 0;
}

void CustomTextEditor::ClearSelection() {
    for (auto& cursor : cursors) {
        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;
    }
}

void CustomTextEditor::AddCursor(int line, int column) {
    Cursor cursor;
    cursor.position.line = std::clamp(line, 0, static_cast<int>(lines.size()) - 1);
    cursor.position.column = std::clamp(column, 0, static_cast<int>(lines[cursor.position.line].size()));
    cursor.selection.start = cursor.position;
    cursor.selection.end = cursor.position;
    cursors.push_back(cursor);
}

void CustomTextEditor::ClearExtraCursors() {
    if (cursors.size() > 1) {
        Cursor primary = cursors[primaryCursor];
        cursors.clear();
        cursors.push_back(primary);
        primaryCursor = 0;
    }
}

void CustomTextEditor::tokenizeLine(int lineIndex) {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return;

    while (static_cast<int>(lineTokens.size()) <= lineIndex) {
        lineTokens.push_back({});
    }

    lineTokens[lineIndex].clear();
    const std::string& line = lines[lineIndex];

    if (line.empty()) return;

    int i = 0;
    int len = static_cast<int>(line.size());
    bool inMultiLineComment = false;

    // Check if previous line ends in multi-line comment
    if (lineIndex > 0) {
        const auto& prevTokens = lineTokens[lineIndex - 1];
        for (const auto& token : prevTokens) {
            if (token.type == TokenType::MultiLineComment) {
                int tokenEnd = token.start + token.length;
                if (tokenEnd >= static_cast<int>(lines[lineIndex - 1].size())) {
                    // Check if multi-line comment was closed
                    const std::string& prevLine = lines[lineIndex - 1];
                    std::string tokenText = prevLine.substr(token.start, token.length);
                    if (!languageDef.multiLineCommentEnd.empty()) {
                        if (tokenText.find(languageDef.multiLineCommentEnd) == std::string::npos ||
                            tokenText.rfind(languageDef.multiLineCommentEnd) < 
                            tokenText.rfind(languageDef.multiLineCommentStart)) {
                            inMultiLineComment = true;
                        }
                    }
                }
            }
        }
    }

    while (i < len) {
        // Skip whitespace
        if (std::isspace(line[i])) {
            ++i;
            continue;
        }

        // Multi-line comment continuation
        if (inMultiLineComment) {
            int start = i;
            size_t endPos = line.find(languageDef.multiLineCommentEnd, i);
            if (endPos != std::string::npos) {
                i = static_cast<int>(endPos + languageDef.multiLineCommentEnd.size());
                inMultiLineComment = false;
            } else {
                i = len;
            }
            lineTokens[lineIndex].push_back({start, i - start, TokenType::MultiLineComment});
            continue;
        }

        // Preprocessor (C/C++)
        if (!languageDef.preprocessorPrefix.empty() && 
            line.compare(i, languageDef.preprocessorPrefix.size(), languageDef.preprocessorPrefix) == 0) {
            int start = i;
            i = len;
            lineTokens[lineIndex].push_back({start, i - start, TokenType::Preprocessor});
            continue;
        }

        // Single-line comment
        if (!languageDef.singleLineComment.empty() &&
            line.compare(i, languageDef.singleLineComment.size(), languageDef.singleLineComment) == 0) {
            lineTokens[lineIndex].push_back({i, len - i, TokenType::Comment});
            break;
        }

        // Multi-line comment start
        if (!languageDef.multiLineCommentStart.empty() &&
            line.compare(i, languageDef.multiLineCommentStart.size(), languageDef.multiLineCommentStart) == 0) {
            int start = i;
            i += static_cast<int>(languageDef.multiLineCommentStart.size());
            size_t endPos = line.find(languageDef.multiLineCommentEnd, i);
            if (endPos != std::string::npos) {
                i = static_cast<int>(endPos + languageDef.multiLineCommentEnd.size());
            } else {
                i = len;
                inMultiLineComment = true;
            }
            lineTokens[lineIndex].push_back({start, i - start, TokenType::MultiLineComment});
            continue;
        }

        // String literals
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            int start = i++;
            while (i < len && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < len) {
                    i += 2;
                } else {
                    ++i;
                }
            }
            if (i < len) ++i;
            lineTokens[lineIndex].push_back({start, i - start, TokenType::String});
            continue;
        }

        // Lua long strings [[ ]]
        if (language == SyntaxLanguage::Lua && line[i] == '[' && i + 1 < len && line[i + 1] == '[') {
            int start = i;
            i += 2;
            size_t endPos = line.find("]]", i);
            if (endPos != std::string::npos) {
                i = static_cast<int>(endPos + 2);
            } else {
                i = len;
            }
            lineTokens[lineIndex].push_back({start, i - start, TokenType::String});
            continue;
        }

        // Numbers
        if (std::isdigit(line[i]) || (line[i] == '.' && i + 1 < len && std::isdigit(line[i + 1]))) {
            int start = i;
            bool hasDecimal = false;
            bool hasExponent = false;

            // Hex prefix
            if (line[i] == '0' && i + 1 < len && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
                i += 2;
                while (i < len && std::isxdigit(line[i])) ++i;
            } else {
                while (i < len) {
                    if (std::isdigit(line[i])) {
                        ++i;
                    } else if (line[i] == '.' && !hasDecimal && !hasExponent) {
                        hasDecimal = true;
                        ++i;
                    } else if ((line[i] == 'e' || line[i] == 'E') && !hasExponent) {
                        hasExponent = true;
                        ++i;
                        if (i < len && (line[i] == '+' || line[i] == '-')) ++i;
                    } else if (line[i] == 'f' || line[i] == 'F' || line[i] == 'l' || line[i] == 'L' ||
                               line[i] == 'u' || line[i] == 'U') {
                        ++i;
                    } else {
                        break;
                    }
                }
            }
            lineTokens[lineIndex].push_back({start, i - start, TokenType::Number});
            continue;
        }

        // Identifiers and keywords
        if (std::isalpha(line[i]) || line[i] == '_') {
            int start = i;
            while (i < len && (std::isalnum(line[i]) || line[i] == '_')) ++i;

            std::string word = line.substr(start, i - start);
            TokenType type = classifyWord(word);

            // Check if followed by '(' to detect function calls
            int j = i;
            while (j < len && std::isspace(line[j])) ++j;
            if (j < len && line[j] == '(' && type == TokenType::Identifier) {
                type = TokenType::Function;
            }

            lineTokens[lineIndex].push_back({start, i - start, type});
            continue;
        }

        // Operators and punctuation
        const char* operators = "+-*/%=<>!&|^~?:";
        const char* punctuation = "(){}[],.;";

        if (std::strchr(operators, line[i])) {
            int start = i++;
            // Handle multi-character operators
            while (i < len && std::strchr(operators, line[i])) ++i;
            lineTokens[lineIndex].push_back({start, i - start, TokenType::Operator});
            continue;
        }

        if (std::strchr(punctuation, line[i])) {
            lineTokens[lineIndex].push_back({i, 1, TokenType::Punctuation});
            ++i;
            continue;
        }

        // Default: single character
        lineTokens[lineIndex].push_back({i, 1, TokenType::Default});
        ++i;
    }
}

void CustomTextEditor::tokenizeAll() {
    lineTokens.clear();
    lineTokens.resize(lines.size());
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        tokenizeLine(i);
    }
}

TokenType CustomTextEditor::classifyWord(const std::string& word) const {
    if (languageDef.keywords.count(word)) return TokenType::Keyword;
    if (languageDef.types.count(word)) return TokenType::Type;
    if (languageDef.builtinFunctions.count(word)) return TokenType::Function;
    return TokenType::Identifier;
}

// -- Multi-cursor position adjustment helpers --

static TextPosition adjustPosAfterDelete(const TextPosition& pos, const TextPosition& delStart, const TextPosition& delEnd) {
    if (pos <= delStart) return pos;
    if (pos <= delEnd) return delStart;
    if (pos.line == delEnd.line) {
        return TextPosition(delStart.line, delStart.column + (pos.column - delEnd.column));
    }
    return TextPosition(pos.line - (delEnd.line - delStart.line), pos.column);
}

static TextPosition adjustPosAfterInsert(const TextPosition& pos, const TextPosition& insertPos, const TextPosition& afterPos) {
    if (pos < insertPos) return pos;
    int deltaLines = afterPos.line - insertPos.line;
    if (pos.line == insertPos.line) {
        if (deltaLines > 0) {
            return TextPosition(pos.line + deltaLines, pos.column - insertPos.column + afterPos.column);
        }
        return TextPosition(pos.line, pos.column + (afterPos.column - insertPos.column));
    }
    return TextPosition(pos.line + deltaLines, pos.column);
}

static void adjustCursorAfterDelete(Cursor& cursor, const TextPosition& delStart, const TextPosition& delEnd) {
    cursor.position = adjustPosAfterDelete(cursor.position, delStart, delEnd);
    cursor.selection.start = adjustPosAfterDelete(cursor.selection.start, delStart, delEnd);
    cursor.selection.end = adjustPosAfterDelete(cursor.selection.end, delStart, delEnd);
}

static void adjustCursorAfterInsert(Cursor& cursor, const TextPosition& insertPos, const TextPosition& afterPos) {
    cursor.position = adjustPosAfterInsert(cursor.position, insertPos, afterPos);
    cursor.selection.start = adjustPosAfterInsert(cursor.selection.start, insertPos, afterPos);
    cursor.selection.end = adjustPosAfterInsert(cursor.selection.end, insertPos, afterPos);
}

void CustomTextEditor::InsertText(const std::string& text, bool allowAutoIndent) {
    if (readOnly || text.empty()) return;

    addUndoRecord();

    // Build sorted index list (ascending by effective edit position)
    std::vector<size_t> order(cursors.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        auto editStart = [&](const Cursor& c) {
            return c.selection.isEmpty() ? c.position : c.selection.getMin();
        };
        return editStart(cursors[a]) < editStart(cursors[b]);
    });

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];

        // Delete selection first
        if (!cursor.selection.isEmpty()) {
            TextPosition delStart = cursor.selection.getMin();
            TextPosition delEnd = cursor.selection.getMax();
            deleteRange(delStart, delEnd);
            cursor.position = delStart;
            cursor.selection.start = cursor.position;
            cursor.selection.end = cursor.position;

            // Adjust all subsequent cursors for the deletion
            for (size_t j = idx + 1; j < order.size(); ++j) {
                adjustCursorAfterDelete(cursors[order[j]], delStart, delEnd);
            }
        }

        TextPosition insertPos = cursor.position;
        insertTextAtCursor(cursor, text, allowAutoIndent);
        TextPosition afterPos = cursor.position;

        // Adjust all subsequent cursors for the insertion
        for (size_t j = idx + 1; j < order.size(); ++j) {
            adjustCursorAfterInsert(cursors[order[j]], insertPos, afterPos);
        }
        if (showParamHint) {
            paramHintAnchor = adjustPosAfterInsert(paramHintAnchor, insertPos, afterPos);
        }
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();

    if (onTextChanged) onTextChanged();
}

void CustomTextEditor::insertTextAtCursor(Cursor& cursor, const std::string& text, bool allowAutoIndent) {
    int line = cursor.position.line;
    int col = cursor.position.column;

    for (char c : text) {
        if (c == '\n') {
            std::string currentLine = lines[line];
            std::string before = currentLine.substr(0, col);
            std::string after = currentLine.substr(col);

            lines[line] = before;
            lines.insert(lines.begin() + line + 1, after);

            ++line;
            col = 0;

            // Auto-indent
            if (autoIndent && allowAutoIndent && line > 0) {
                int indent = getLineIndent(line - 1);
                // Increase indent after { or :
                if (!before.empty()) {
                    char lastChar = before.back();
                    if (lastChar == '{' || lastChar == ':') {
                        indent += tabSize;
                    }
                }
                std::string indentStr = getIndentString(indent);
                lines[line] = indentStr + lines[line];
                col = static_cast<int>(indentStr.size());
            }
        } else if (c == '\t') {
            int spacesToInsert = tabSize - (col % tabSize);
            std::string spaces(spacesToInsert, ' ');
            lines[line].insert(col, spaces);
            col += spacesToInsert;
        } else {
            lines[line].insert(col, 1, c);
            ++col;
        }
    }

    cursor.position.line = line;
    cursor.position.column = col;
    cursor.selection.start = cursor.position;
    cursor.selection.end = cursor.position;
    cursor.preferredColumn = col;
}

void CustomTextEditor::DeleteSelection() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order(cursors.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        auto editStart = [&](const Cursor& c) {
            return c.selection.isEmpty() ? c.position : c.selection.getMin();
        };
        return editStart(cursors[a]) < editStart(cursors[b]);
    });

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];
        if (!cursor.selection.isEmpty()) {
            TextPosition delStart = cursor.selection.getMin();
            TextPosition delEnd = cursor.selection.getMax();
            deleteRange(delStart, delEnd);
            cursor.position = delStart;
            cursor.selection.start = cursor.position;
            cursor.selection.end = cursor.position;

            for (size_t j = idx + 1; j < order.size(); ++j) {
                adjustCursorAfterDelete(cursors[order[j]], delStart, delEnd);
            }
        }
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();

    if (onTextChanged) onTextChanged();
}

void CustomTextEditor::Backspace() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order(cursors.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        auto editStart = [&](const Cursor& c) {
            return c.selection.isEmpty() ? c.position : c.selection.getMin();
        };
        return editStart(cursors[a]) < editStart(cursors[b]);
    });

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];

        TextPosition delStart, delEnd;
        bool didDelete = true;

        if (!cursor.selection.isEmpty()) {
            delStart = cursor.selection.getMin();
            delEnd = cursor.selection.getMax();
            deleteRange(delStart, delEnd);
            cursor.position = delStart;
        } else if (cursor.position.column > 0) {
            delEnd = cursor.position;
            cursor.position.column--;
            delStart = cursor.position;
            lines[cursor.position.line].erase(cursor.position.column, 1);
        } else if (cursor.position.line > 0) {
            delEnd = cursor.position;
            int prevLineLen = static_cast<int>(lines[cursor.position.line - 1].size());
            lines[cursor.position.line - 1] += lines[cursor.position.line];
            lines.erase(lines.begin() + cursor.position.line);
            cursor.position.line--;
            cursor.position.column = prevLineLen;
            delStart = cursor.position;
        } else {
            didDelete = false;
        }

        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;

        if (didDelete) {
            for (size_t j = idx + 1; j < order.size(); ++j) {
                adjustCursorAfterDelete(cursors[order[j]], delStart, delEnd);
            }
            if (showParamHint && cursor.selection.isEmpty()) {
                paramHintAnchor = adjustPosAfterDelete(paramHintAnchor, delStart, delEnd);
            }
        }
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();

    if (onTextChanged) onTextChanged();
}

void CustomTextEditor::Delete() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order(cursors.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        auto editStart = [&](const Cursor& c) {
            return c.selection.isEmpty() ? c.position : c.selection.getMin();
        };
        return editStart(cursors[a]) < editStart(cursors[b]);
    });

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];

        TextPosition delStart, delEnd;
        bool didDelete = true;

        if (!cursor.selection.isEmpty()) {
            delStart = cursor.selection.getMin();
            delEnd = cursor.selection.getMax();
            deleteRange(delStart, delEnd);
            cursor.position = delStart;
        } else {
            int lineLen = static_cast<int>(lines[cursor.position.line].size());
            if (cursor.position.column < lineLen) {
                delStart = cursor.position;
                delEnd = TextPosition(cursor.position.line, cursor.position.column + 1);
                lines[cursor.position.line].erase(cursor.position.column, 1);
            } else if (cursor.position.line < static_cast<int>(lines.size()) - 1) {
                delStart = cursor.position;
                delEnd = TextPosition(cursor.position.line + 1, 0);
                lines[cursor.position.line] += lines[cursor.position.line + 1];
                lines.erase(lines.begin() + cursor.position.line + 1);
            } else {
                didDelete = false;
            }
        }

        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;

        if (didDelete) {
            for (size_t j = idx + 1; j < order.size(); ++j) {
                adjustCursorAfterDelete(cursors[order[j]], delStart, delEnd);
            }
            if (showParamHint && cursor.selection.isEmpty()) {
                paramHintAnchor = adjustPosAfterDelete(paramHintAnchor, delStart, delEnd);
            }
        }
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();

    if (onTextChanged) onTextChanged();
}

void CustomTextEditor::deleteRange(const TextPosition& start, const TextPosition& end) {
    if (start == end) return;

    TextPosition minPos = start < end ? start : end;
    TextPosition maxPos = start < end ? end : start;

    // Bounds checking
    if (minPos.line < 0 || minPos.line >= static_cast<int>(lines.size())) return;
    if (maxPos.line < 0 || maxPos.line >= static_cast<int>(lines.size())) return;

    minPos.column = std::clamp(minPos.column, 0, static_cast<int>(lines[minPos.line].size()));
    maxPos.column = std::clamp(maxPos.column, 0, static_cast<int>(lines[maxPos.line].size()));

    if (minPos.line == maxPos.line) {
        if (minPos.column < maxPos.column) {
            lines[minPos.line].erase(minPos.column, maxPos.column - minPos.column);
        }
    } else {
        std::string before = lines[minPos.line].substr(0, minPos.column);
        std::string after = lines[maxPos.line].substr(maxPos.column);

        lines[minPos.line] = before + after;
        lines.erase(lines.begin() + minPos.line + 1, lines.begin() + maxPos.line + 1);
    }

    if (showParamHint) {
        paramHintAnchor = adjustPosAfterDelete(paramHintAnchor, minPos, maxPos);
    }
}

std::string CustomTextEditor::getRange(const TextPosition& start, const TextPosition& end) const {
    if (start == end) return "";

    TextPosition minPos = start < end ? start : end;
    TextPosition maxPos = start < end ? end : start;

    // Bounds checking
    if (minPos.line < 0 || minPos.line >= static_cast<int>(lines.size())) return "";
    if (maxPos.line < 0 || maxPos.line >= static_cast<int>(lines.size())) return "";

    minPos.column = std::clamp(minPos.column, 0, static_cast<int>(lines[minPos.line].size()));
    maxPos.column = std::clamp(maxPos.column, 0, static_cast<int>(lines[maxPos.line].size()));

    if (minPos.line == maxPos.line) {
        if (minPos.column >= maxPos.column) return "";
        return lines[minPos.line].substr(minPos.column, maxPos.column - minPos.column);
    }

    std::string result = lines[minPos.line].substr(minPos.column) + "\n";
    for (int i = minPos.line + 1; i < maxPos.line; ++i) {
        result += lines[i] + "\n";
    }
    result += lines[maxPos.line].substr(0, maxPos.column);

    return result;
}

void CustomTextEditor::Undo(int steps) {
    if (!CanUndo()) return;

    if (showParamHint) closeParamHint();

    for (int i = 0; i < steps && undoIndex > 0; ++i) {
        --undoIndex;
        if (undoIndex < static_cast<int>(undoBuffer.size())) {
            // Store current state as afterText before restoring
            undoBuffer[undoIndex].afterText = GetText();
            undoBuffer[undoIndex].afterCursors = cursors;

            // Restore previous state
            lines.clear();
            std::string line;
            for (char c : undoBuffer[undoIndex].beforeText) {
                if (c == '\n') {
                    lines.push_back(line);
                    line.clear();
                } else if (c != '\r') {
                    line += c;
                }
            }
            lines.push_back(line);
            tokenizeAll();

            if (!undoBuffer[undoIndex].beforeCursors.empty()) {
                cursors = undoBuffer[undoIndex].beforeCursors;
            }
            ensureValidCursors();
        }
    }
}

void CustomTextEditor::Redo(int steps) {
    if (!CanRedo()) return;

    if (showParamHint) closeParamHint();

    for (int i = 0; i < steps && undoIndex < static_cast<int>(undoBuffer.size()); ++i) {
        if (!undoBuffer[undoIndex].afterText.empty()) {
            // Restore after state
            lines.clear();
            std::string line;
            for (char c : undoBuffer[undoIndex].afterText) {
                if (c == '\n') {
                    lines.push_back(line);
                    line.clear();
                } else if (c != '\r') {
                    line += c;
                }
            }
            lines.push_back(line);
            tokenizeAll();

            if (!undoBuffer[undoIndex].afterCursors.empty()) {
                cursors = undoBuffer[undoIndex].afterCursors;
            }
            ensureValidCursors();
        }
        ++undoIndex;
    }
}

void CustomTextEditor::addUndoRecord() {
    // Remove any redo history
    if (undoIndex < static_cast<int>(undoBuffer.size())) {
        undoBuffer.erase(undoBuffer.begin() + undoIndex, undoBuffer.end());
    }

    UndoRecord record;
    record.beforeText = GetText();
    record.afterText = "";  // Will be filled by finalizeUndoRecord
    record.beforeCursors = cursors;
    record.timestamp = std::chrono::steady_clock::now();

    // Try to merge with previous record if it's recent
    if (!undoBuffer.empty()) {
        auto& last = undoBuffer.back();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            record.timestamp - last.timestamp).count();
        if (elapsed < 300 && !last.isMerged) {
            // Merge: update the merged flag but keep the original beforeText
            last.isMerged = true;
            // Don't add new record, the previous one will be updated
            return;
        }
    }

    undoBuffer.push_back(record);
    ++undoIndex;

    // Limit undo buffer size
    while (undoBuffer.size() > maxUndoSteps) {
        undoBuffer.erase(undoBuffer.begin());
        if (undoIndex > 0) --undoIndex;
    }
}

void CustomTextEditor::finalizeUndoRecord() {
    // Store the "after" state in the most recent undo record
    if (!undoBuffer.empty() && undoIndex > 0) {
        undoBuffer[undoIndex - 1].afterText = GetText();
        undoBuffer[undoIndex - 1].afterCursors = cursors;
    }
}

void CustomTextEditor::Copy() {
    std::string text = GetSelectedText();
    if (text.empty()) {
        // Copy entire line if no selection
        if (!cursors.empty()) {
            int line = cursors[primaryCursor].position.line;
            text = lines[line] + "\n";
        }
    }

    if (!text.empty()) {
        ImGui::SetClipboardText(text.c_str());
    }
}

void CustomTextEditor::Cut() {
    if (readOnly) return;

    if (HasSelection()) {
        Copy();
        DeleteSelection();
    } else {
        // Cut entire line (VSCode behavior)
        Copy();
        if (!cursors.empty()) {
            addUndoRecord();
            int line = cursors[primaryCursor].position.line;
            if (lines.size() > 1) {
                lines.erase(lines.begin() + line);
                if (line >= static_cast<int>(lines.size())) line = static_cast<int>(lines.size()) - 1;
                cursors[primaryCursor].position = TextPosition(line, 0);
            } else {
                lines[0].clear();
                cursors[primaryCursor].position = TextPosition(0, 0);
            }
            cursors[primaryCursor].selection.start = cursors[primaryCursor].position;
            cursors[primaryCursor].selection.end = cursors[primaryCursor].position;
            tokenizeAll();
            finalizeUndoRecord();
            if (onTextChanged) onTextChanged();
        }
    }
}

void CustomTextEditor::Paste() {
    if (readOnly) return;

    const char* clipboard = ImGui::GetClipboardText();
    if (clipboard) {
        InsertText(clipboard, false);
    }
}

void CustomTextEditor::SetSearchText(const std::string& text) {
    searchText = text;
    updateSearchResults();
}

void CustomTextEditor::OpenFind() {
    showFindDialog = true;
    // Copy selection to search buffer if there's one
    if (HasSelection()) {
        std::string selection = GetSelectedText();
        // Only use single-line selections
        if (selection.find('\n') == std::string::npos && selection.size() < sizeof(findInputBuffer) - 1) {
            strncpy(findInputBuffer, selection.c_str(), sizeof(findInputBuffer) - 1);
            findInputBuffer[sizeof(findInputBuffer) - 1] = '\0';
            SetSearchText(selection);
        }
    }
}

void CustomTextEditor::CloseFind() {
    showFindDialog = false;
    // Clear search highlights
    searchText.clear();
    searchResults.clear();
    currentSearchResult = -1;
}

void CustomTextEditor::updateSearchResults() {
    searchResults.clear();
    currentSearchResult = -1;

    if (searchText.empty()) return;

    std::string searchLower = searchText;
    if (!findCaseSensitive) {
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
    }

    for (int lineIdx = 0; lineIdx < static_cast<int>(lines.size()); ++lineIdx) {
        std::string line = lines[lineIdx];
        if (!findCaseSensitive) {
            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
        }

        size_t pos = 0;
        while ((pos = line.find(searchLower, pos)) != std::string::npos) {
            searchResults.push_back(TextPosition(lineIdx, static_cast<int>(pos)));
            ++pos;
        }
    }
}

bool CustomTextEditor::ReplaceNext(bool caseSensitive, bool wholeWord) {
    if (searchText.empty() || readOnly) return false;

    TextPosition pMin = cursors[primaryCursor].selection.getMin();
    TextPosition pMax = cursors[primaryCursor].selection.getMax();

    bool match = false;
    if (!cursors[primaryCursor].selection.isEmpty() && pMin.line == pMax.line && static_cast<size_t>(pMax.column - pMin.column) == searchText.size()) {
        std::string selText = lines[pMin.line].substr(pMin.column, searchText.size());
        std::string cmpText = searchText;
        if (!caseSensitive) {
            std::transform(selText.begin(), selText.end(), selText.begin(), ::tolower);
            std::transform(cmpText.begin(), cmpText.end(), cmpText.begin(), ::tolower);
        }
        if (selText == cmpText) {
            match = true;
        }
    }

    if (match) {
        if (cursors.size() > 1) {
            Cursor pC = cursors[primaryCursor];
            cursors.clear();
            cursors.push_back(pC);
            primaryCursor = 0;
        }
        InsertText(replaceInputBuffer, false);
    }

    return FindNext(caseSensitive, wholeWord);
}

bool CustomTextEditor::FindNext(bool caseSensitive, bool wholeWord) {
    if (searchResults.empty()) return false;

    TextPosition cursorPos = cursors[primaryCursor].position;
    // If we have a selection, use the end of selection as starting point to avoid finding the same text again
    if (!cursors[primaryCursor].selection.isEmpty()) {
        cursorPos = cursors[primaryCursor].selection.getMax();
    }

    pendingScrollToCursor = true;

    for (size_t i = 0; i < searchResults.size(); ++i) {
        if (searchResults[i] >= cursorPos) {
            currentSearchResult = static_cast<int>(i);
            SetCursorPosition(searchResults[i].line, searchResults[i].column);

            // Select the found text
            cursors[primaryCursor].selection.start = searchResults[i];
            cursors[primaryCursor].selection.end = TextPosition(
                searchResults[i].line,
                searchResults[i].column + static_cast<int>(searchText.size())
            );
            cursors[primaryCursor].position = cursors[primaryCursor].selection.end;
            scrollToCursor();

            return true;
        }
    }

    // Wrap around
    if (!searchResults.empty()) {
        currentSearchResult = 0;
        SetCursorPosition(searchResults[0].line, searchResults[0].column);

        cursors[primaryCursor].selection.start = searchResults[0];
        cursors[primaryCursor].selection.end = TextPosition(
            searchResults[0].line,
            searchResults[0].column + static_cast<int>(searchText.size())
        );
        cursors[primaryCursor].position = cursors[primaryCursor].selection.end;

        return true;
    }

    return false;
}

bool CustomTextEditor::FindPrevious(bool caseSensitive, bool wholeWord) {
    if (searchResults.empty()) return false;

    TextPosition cursorPos = cursors[primaryCursor].position;
    // If we have a selection use start as pivot
    if (!cursors[primaryCursor].selection.isEmpty()) {
        cursorPos = cursors[primaryCursor].selection.getMin();
    }

    pendingScrollToCursor = true;

    for (int i = static_cast<int>(searchResults.size()) - 1; i >= 0; --i) {
        if (searchResults[i] < cursorPos) {
            currentSearchResult = i;
            SetCursorPosition(searchResults[i].line, searchResults[i].column);

            cursors[primaryCursor].selection.start = searchResults[i];
            cursors[primaryCursor].selection.end = TextPosition(
                searchResults[i].line,
                searchResults[i].column + static_cast<int>(searchText.size())
            );
            cursors[primaryCursor].position = cursors[primaryCursor].selection.start;

            return true;
        }
    }

    // Wrap around
    if (!searchResults.empty()) {
        int lastIdx = static_cast<int>(searchResults.size()) - 1;
        currentSearchResult = lastIdx;
        SetCursorPosition(searchResults[lastIdx].line, searchResults[lastIdx].column);

        cursors[primaryCursor].selection.start = searchResults[lastIdx];
        cursors[primaryCursor].selection.end = TextPosition(
            searchResults[lastIdx].line,
            searchResults[lastIdx].column + static_cast<int>(searchText.size())
        );
        cursors[primaryCursor].position = cursors[primaryCursor].selection.start;

        return true;
    }

    return false;
}

void CustomTextEditor::SelectAllOccurrences(const std::string& text, bool caseSensitive) {
    if (text.empty()) return;

    searchText = text;
    updateSearchResults();

    if (searchResults.empty()) return;

    cursors.clear();
    for (const auto& result : searchResults) {
        Cursor cursor;
        cursor.selection.start = result;
        cursor.selection.end = TextPosition(result.line, result.column + static_cast<int>(text.size()));
        cursor.position = cursor.selection.end;
        cursors.push_back(cursor);
    }
    primaryCursor = 0;
}

int CustomTextEditor::ReplaceAll(const std::string& find, const std::string& replace, bool caseSensitive) {
    if (find.empty() || readOnly) return 0;

    addUndoRecord();

    int count = 0;
    for (int lineIdx = static_cast<int>(lines.size()) - 1; lineIdx >= 0; --lineIdx) {
        std::string lineText = lines[lineIdx];
        std::string findText = find;
        if (!caseSensitive) {
            std::transform(lineText.begin(), lineText.end(), lineText.begin(), ::tolower);
            std::transform(findText.begin(), findText.end(), findText.begin(), ::tolower);
        }
        size_t pos = lineText.rfind(findText);
        while (pos != std::string::npos) {
            lines[lineIdx].replace(pos, find.size(), replace);
            ++count;
            if (pos > 0) {
                // Rebuild lowercase view after replacement
                lineText = lines[lineIdx];
                if (!caseSensitive) {
                    std::transform(lineText.begin(), lineText.end(), lineText.begin(), ::tolower);
                }
                pos = lineText.rfind(findText, pos - 1);
            } else {
                break;
            }
        }
    }

    if (count > 0) {
        tokenizeAll();
        finalizeUndoRecord();
        updateSearchResults();
        if (onTextChanged) onTextChanged();
    }

    return count;
}

int CustomTextEditor::getLineIndent(int lineIndex) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0;

    int indent = 0;
    for (char c : lines[lineIndex]) {
        if (c == ' ') {
            ++indent;
        } else if (c == '\t') {
            indent += tabSize;
        } else {
            break;
        }
    }
    return indent;
}

std::string CustomTextEditor::getIndentString(int level) const {
    return std::string(level, ' ');
}

void CustomTextEditor::ensureValidCursors() {
    for (auto& cursor : cursors) {
        cursor.position = clampPosition(cursor.position);
        cursor.selection.start = clampPosition(cursor.selection.start);
        cursor.selection.end = clampPosition(cursor.selection.end);
    }
}

void CustomTextEditor::scrollToCursor() {
    if (cursors.empty() || lineHeight <= 0) return;

    TextPosition cursorPos = cursors[primaryCursor].position;
    float cursorY = cursorPos.line * lineHeight;
    float cursorX = textStartX + cursorPos.column * charWidth;

    // Vertical scrolling using ImGui
    float viewHeight = ImGui::GetWindowHeight();
    if (viewHeight <= 0) viewHeight = 400; // Default fallback

    float newScrollY = scrollY;
    if (cursorY < scrollY) {
        newScrollY = cursorY;
    } else if (cursorY > scrollY + viewHeight - lineHeight * 2) {
        newScrollY = cursorY - viewHeight + lineHeight * 2;
    }
    newScrollY = std::max(0.0f, newScrollY);
    if (newScrollY != scrollY) {
        scrollY = newScrollY;
        ImGui::SetScrollY(newScrollY);
    }

    // Horizontal scrolling using ImGui
    float viewWidth = ImGui::GetWindowWidth();
    if (viewWidth <= 0) viewWidth = 600; // Default fallback

    float newScrollX = scrollX;
    if (cursorX < scrollX + textStartX) {
        newScrollX = std::max(0.0f, cursorX - textStartX);
    } else if (cursorX > scrollX + viewWidth - charWidth * 2) {
        newScrollX = cursorX - viewWidth + charWidth * 4;
    }
    newScrollX = std::max(0.0f, newScrollX);
    if (newScrollX != scrollX) {
        scrollX = newScrollX;
        ImGui::SetScrollX(newScrollX);
    }
}

void CustomTextEditor::moveSelectedText(const TextPosition& destPos) {
    if (readOnly || cursors.empty()) return;

    // Only handle single selection for now
    // If multiple selections, we'll just ignore for simplicity or handle first?
    // Let's iterate and see if mouse is inside ANY selection.
    // The callers already verified this partially.

    // Find valid selection that we are dragging
    // With current logic, we likely only support drag drop if we have one or if we treat all as valid.

    // Let's merge cursors first to be safe
    mergeCursors();

    // Find which cursor range we are moving.
    // Actually, `GetSelectedText` gets ALL text.
    // Let's implement full logical move.

    std::string text = GetSelectedText();
    if (text.empty()) return;

    // Clamp destPos to valid bounds
    TextPosition clampedDest = destPos;
    clampedDest.line = std::clamp(clampedDest.line, 0, static_cast<int>(lines.size()) - 1);
    clampedDest.column = std::clamp(clampedDest.column, 0, static_cast<int>(lines[clampedDest.line].size()));

    // Check if destPos is inside any selection
    for (const auto& cursor : cursors) {
        if (!cursor.selection.isEmpty()) {
            TextPosition min = cursor.selection.getMin();
            TextPosition max = cursor.selection.getMax();
            if (clampedDest >= min && clampedDest < max) return; // Drop inside selection
        }
    }

    addUndoRecord();

    // Case: Dest is AFTER all selections
    // Case: Dest is BEFORE all selections
    // Case: Mixed.. tricky.

    // Simplest approach: Delete then Insert. Adjust destPos.
    // This works reliably if we have single selection.
    // If multiple, deleting earlier ones shifts later ones.

    // Re-verify single cursor support
    if (cursors.size() != 1) return; // Supporting multi-cursor drag drop is complex

    TextPosition start = cursors[0].selection.getMin();
    TextPosition end = cursors[0].selection.getMax();

    // Clamp selection bounds
    start.line = std::clamp(start.line, 0, static_cast<int>(lines.size()) - 1);
    start.column = std::clamp(start.column, 0, static_cast<int>(lines[start.line].size()));
    end.line = std::clamp(end.line, 0, static_cast<int>(lines.size()) - 1);
    end.column = std::clamp(end.column, 0, static_cast<int>(lines[end.line].size()));

    TextPosition insertPos = clampedDest;

    deleteRange(start, end);

    // If insertPos was after the deleted range, we need to adjust it
    if (insertPos > start) {
        if (insertPos.line == end.line) {
             if (start.line == end.line) {
                 insertPos.column -= (end.column - start.column);
             } else {
                 // dest was on the same line as the end of deletion
                 // End line is merged into start line.
                 // The text relative to 'end' is now relative to 'start' + length of start line
                 // But deleteRange handled the merge.

                 // However, we just have Coordinates.
                 // We need to map old coordinate to new coordinate.

                 // If insertPos.line was 'end.line', it is now 'start.line'.
                 // The column was insertPos.column.
                 // The part of line 'end' AFTER 'end.column' is appended to 'start.line' AFTER 'start.column'.
                 // So new column = start.column + (insertPos.column - end.column)
                 insertPos.line = start.line;
                 insertPos.column = start.column + (insertPos.column - end.column);
             }
        } else {
            // insertPos.line > end.line
            // Lines deleted = (end.line - start.line)
            insertPos.line -= (end.line - start.line);
        }
    }

    // Clamp insertPos after adjustments
    insertPos.line = std::clamp(insertPos.line, 0, static_cast<int>(lines.size()) - 1);
    insertPos.column = std::clamp(insertPos.column, 0, static_cast<int>(lines[insertPos.line].size()));

    // Insert text
    cursors.clear();
    Cursor c;
    c.position = insertPos;
    c.selection.start = insertPos;
    c.selection.end = insertPos;
    cursors.push_back(c);
    primaryCursor = 0;

    insertTextAtCursor(cursors[0], text, false);

    // Select the inserted text
    cursors[0].selection.start = insertPos;
    cursors[0].selection.end = cursors[0].position; // Position moved after insert

    tokenizeAll();
    finalizeUndoRecord();

    if (onTextChanged) onTextChanged();
}

void CustomTextEditor::mergeCursors() {
    if (cursors.size() <= 1) return;

    sortCursors();

    std::vector<Cursor> merged;
    for (const auto& cursor : cursors) {
        if (merged.empty()) {
            merged.push_back(cursor);
        } else {
            auto& last = merged.back();
            if (cursor.position == last.position) {
                // Merge selections
                TextPosition minStart = std::min(last.selection.getMin(), cursor.selection.getMin());
                TextPosition maxEnd = std::max(last.selection.getMax(), cursor.selection.getMax());
                last.selection.start = minStart;
                last.selection.end = maxEnd;
            } else {
                merged.push_back(cursor);
            }
        }
    }

    cursors = merged;
    primaryCursor = std::min(primaryCursor, static_cast<int>(cursors.size()) - 1);
}

void CustomTextEditor::sortCursors() {
    std::sort(cursors.begin(), cursors.end(), [](const Cursor& a, const Cursor& b) {
        return a.position < b.position;
    });
}

TextPosition CustomTextEditor::clampPosition(const TextPosition& pos) const {
    TextPosition result = pos;
    result.line = std::clamp(result.line, 0, static_cast<int>(lines.size()) - 1);
    result.column = std::clamp(result.column, 0, static_cast<int>(lines[result.line].size()));
    return result;
}

void CustomTextEditor::moveCursor(Cursor& cursor, int deltaLine, int deltaCol, bool shift) {
    TextPosition newPos = cursor.position;

    if (deltaLine != 0) {
        newPos.line = std::clamp(newPos.line + deltaLine, 0, static_cast<int>(lines.size()) - 1);

        if (cursor.preferredColumn >= 0) {
            newPos.column = std::min(cursor.preferredColumn, static_cast<int>(lines[newPos.line].size()));
        } else {
            cursor.preferredColumn = newPos.column;
            newPos.column = std::min(newPos.column, static_cast<int>(lines[newPos.line].size()));
        }
    }

    if (deltaCol != 0) {
        newPos.column += deltaCol;
        cursor.preferredColumn = -1;

        // Handle line wrapping
        while (newPos.column < 0 && newPos.line > 0) {
            --newPos.line;
            newPos.column = static_cast<int>(lines[newPos.line].size()) + newPos.column + 1;
        }
        while (newPos.column > static_cast<int>(lines[newPos.line].size()) && 
               newPos.line < static_cast<int>(lines.size()) - 1) {
            newPos.column -= static_cast<int>(lines[newPos.line].size()) + 1;
            ++newPos.line;
        }

        newPos.column = std::clamp(newPos.column, 0, static_cast<int>(lines[newPos.line].size()));
    }

    cursor.position = newPos;

    if (shift) {
        cursor.selection.end = newPos;
    } else {
        cursor.selection.start = newPos;
        cursor.selection.end = newPos;
    }
}

void CustomTextEditor::moveCursorWord(Cursor& cursor, int direction, bool shift) {
    TextPosition pos = cursor.position;

    if (direction > 0) {
        // Move to end of current/next word
        while (pos.column < static_cast<int>(lines[pos.line].size()) && 
               !isWordChar(lines[pos.line][pos.column])) {
            ++pos.column;
        }
        while (pos.column < static_cast<int>(lines[pos.line].size()) && 
               isWordChar(lines[pos.line][pos.column])) {
            ++pos.column;
        }
    } else {
        // Move to start of current/previous word
        if (pos.column > 0) --pos.column;
        while (pos.column > 0 && !isWordChar(lines[pos.line][pos.column])) {
            --pos.column;
        }
        while (pos.column > 0 && isWordChar(lines[pos.line][pos.column - 1])) {
            --pos.column;
        }
    }

    cursor.position = pos;

    if (shift) {
        cursor.selection.end = pos;
    } else {
        cursor.selection.start = pos;
        cursor.selection.end = pos;
    }

    cursor.preferredColumn = -1;
}

void CustomTextEditor::moveCursorToLineStart(Cursor& cursor, bool shift) {
    int firstNonSpace = 0;
    const std::string& line = lines[cursor.position.line];
    while (firstNonSpace < static_cast<int>(line.size()) && std::isspace(line[firstNonSpace])) {
        ++firstNonSpace;
    }

    if (cursor.position.column == firstNonSpace) {
        cursor.position.column = 0;
    } else {
        cursor.position.column = firstNonSpace;
    }

    if (shift) {
        cursor.selection.end = cursor.position;
    } else {
        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;
    }

    cursor.preferredColumn = -1;
}

void CustomTextEditor::moveCursorToLineEnd(Cursor& cursor, bool shift) {
    cursor.position.column = static_cast<int>(lines[cursor.position.line].size());

    if (shift) {
        cursor.selection.end = cursor.position;
    } else {
        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;
    }

    cursor.preferredColumn = -1;
}

bool CustomTextEditor::isWordChar(char c) const {
    return std::isalnum(c) || c == '_';
}

TextPosition CustomTextEditor::findWordStart(const TextPosition& pos) const {
    TextPosition result = pos;
    const std::string& line = lines[result.line];

    while (result.column > 0 && isWordChar(line[result.column - 1])) {
        --result.column;
    }

    return result;
}

TextPosition CustomTextEditor::findWordEnd(const TextPosition& pos) const {
    TextPosition result = pos;
    const std::string& line = lines[result.line];

    while (result.column < static_cast<int>(line.size()) && isWordChar(line[result.column])) {
        ++result.column;
    }

    return result;
}

char CustomTextEditor::getCharAt(const TextPosition& pos) const {
    if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return '\0';
    if (pos.column < 0 || pos.column >= static_cast<int>(lines[pos.line].size())) return '\0';
    return lines[pos.line][pos.column];
}

bool CustomTextEditor::isOpenBracket(char c) const {
    return c == '(' || c == '[' || c == '{';
}

bool CustomTextEditor::isCloseBracket(char c) const {
    return c == ')' || c == ']' || c == '}';
}

char CustomTextEditor::getMatchingBracket(char c) const {
    switch (c) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        default: return '\0';
    }
}

char CustomTextEditor::findMatchingBracket(const TextPosition& pos, TextPosition& matchPos) const {
    char bracket = getCharAt(pos);
    if (!isOpenBracket(bracket) && !isCloseBracket(bracket)) return '\0';

    char target = getMatchingBracket(bracket);
    int direction = isOpenBracket(bracket) ? 1 : -1;
    int depth = 1;

    TextPosition current = pos;
    while (depth > 0) {
        if (direction > 0) {
            ++current.column;
            if (current.column >= static_cast<int>(lines[current.line].size())) {
                ++current.line;
                current.column = 0;
                if (current.line >= static_cast<int>(lines.size())) return '\0';
            }
        } else {
            --current.column;
            if (current.column < 0) {
                --current.line;
                if (current.line < 0) return '\0';
                current.column = static_cast<int>(lines[current.line].size()) - 1;
                if (current.column < 0) current.column = 0;
            }
        }

        char c = getCharAt(current);
        if (c == bracket) ++depth;
        else if (c == target) --depth;
    }

    matchPos = current;
    return target;
}

std::string CustomTextEditor::inferTypeOfVariable(const std::string& varName, int currentLine) const {
    if (varName.empty()) return "";

    // C++ patterns:
    // 1. Standard declaration: [const] Type [*&] varName
    std::regex cppDeclRegex("(?:const\\s+)?([A-Za-z0-9_:]+)(?:<[^>]*>)?\\s*(?:[*&]+\\s*)?" + varName + "\\b");
    // 2. auto varName = new Type(...)
    std::regex cppNewRegex("\\bauto\\s*[*&]*\\s*" + varName + "\\s*=\\s*new\\s+([A-Za-z0-9_:]+)");
    // 3. auto varName = Type(...)  or  auto varName = Type{...}
    std::regex cppAutoCtorRegex("\\bauto\\s*[*&]*\\s*" + varName + "\\s*=\\s*([A-Z][A-Za-z0-9_:]*)\\s*[({]");

    std::regex luaVarRegex("\\b" + varName + "\\s*=\\s*([A-Za-z0-9_\\.:]+)(?:\\(|\\{)");
    std::regex luaLocalVarRegex("\\blocal\\s+" + varName + "\\s*=\\s*([A-Za-z0-9_\\.:]+)(?:\\(|\\{)");

    // Search backwards from the current line
    for (int i = currentLine; i >= 0; --i) {
        if (i >= static_cast<int>(lines.size())) continue;
        const std::string& line = lines[i];
        std::smatch match;

        if (language == SyntaxLanguage::Cpp) {
            // Try auto = new Type first (most specific)
            if (std::regex_search(line, match, cppNewRegex)) {
                return match[1].str();
            }
            // Try auto = Type() / Type{}
            if (std::regex_search(line, match, cppAutoCtorRegex)) {
                return match[1].str();
            }
            // Try standard declaration
            if (std::regex_search(line, match, cppDeclRegex)) {
                std::string typeMatch = match[1].str();
                if (typeMatch != "return" && typeMatch != "new" && typeMatch != "delete" && typeMatch != "auto") {
                    return typeMatch;
                }
            }
        } else if (language == SyntaxLanguage::Lua) {
            if (std::regex_search(line, match, luaLocalVarRegex)) {
                return match[1].str();
            }
            if (std::regex_search(line, match, luaVarRegex)) {
                return match[1].str();
            }
        }
    }
    return "";
}

SuggestionContext CustomTextEditor::buildSuggestionContext() const {
    SuggestionContext ctx;

    if (cursors.empty()) return ctx;

    ctx.isCpp = (language == SyntaxLanguage::Cpp);

    const TextPosition& pos = cursors[primaryCursor].position;

    // Get current word being typed
    TextPosition wordStart = findWordStart(pos);
    ctx.currentWord = getRange(wordStart, pos);

    // Get line content
    if (pos.line >= 0 && pos.line < static_cast<int>(lines.size())) {
        ctx.lineContent = lines[pos.line];
    }

    ctx.cursorColumn = pos.column;
    ctx.lineNumber = pos.line;

    // Check for member access operators
    int checkCol = wordStart.column - 1;
    if (checkCol >= 0 && ctx.lineContent.size() > 0) {
        if (checkCol < static_cast<int>(ctx.lineContent.size())) {
            char c = ctx.lineContent[checkCol];
            if (c == '.') {
                ctx.afterDot = true;
            } else if (c == '>' && checkCol > 0 && ctx.lineContent[checkCol - 1] == '-') {
                ctx.afterArrow = true;
                checkCol--; // move past '-'
            } else if (c == ':' && checkCol > 0 && ctx.lineContent[checkCol - 1] == ':') {
                ctx.afterDoubleColon = true;
                checkCol--; // move past first ':'
            } else if (c == ':') {
                if (language == SyntaxLanguage::Lua) {
                    ctx.afterColon = true;
                }
            }
        }
    }

    // Get previous word for context (e.g. before the dot)
    if (checkCol > 0) {
        TextPosition prevEnd(pos.line, checkCol - 1);
        // Skip whitespace if any
        while (prevEnd.column > 0 && std::isspace(ctx.lineContent[prevEnd.column])) {
            prevEnd.column--;
        }
        if (prevEnd.column >= 0 && (std::isalnum(ctx.lineContent[prevEnd.column]) || ctx.lineContent[prevEnd.column] == '_')) {
            TextPosition prevStart = findWordStart(TextPosition(pos.line, prevEnd.column + 1));
            // getRange uses end column generically as exclusive bound, so we use prevEnd.column+1
            ctx.previousWord = getRange(prevStart, TextPosition(pos.line, prevEnd.column + 1));
        }
    }

    // Infer the type of the previous word for semantic suggestions
    if (ctx.afterDot || ctx.afterArrow || ctx.afterDoubleColon || ctx.afterColon) {
        if (!ctx.previousWord.empty()) {
            if (ctx.afterDoubleColon) {
                // For ::, the previousWord IS the type name (e.g. Vector3::ZERO)
                ctx.targetType = ctx.previousWord;
            } else {
                ctx.targetType = inferTypeOfVariable(ctx.previousWord, pos.line);
                // Fallback: look up the variable in project symbols (e.g. header member variables)
                if (ctx.targetType.empty() && suggestions) {
                    ctx.targetType = suggestions->FindSymbolType(ctx.previousWord);
                }
            }
            // Strip namespace prefix for matching engine API parent types (e.g. "doriax::Mesh" -> "Mesh")
            if (!ctx.targetType.empty()) {
                size_t lastColon = ctx.targetType.rfind(':');
                if (lastColon != std::string::npos) {
                    ctx.targetType = ctx.targetType.substr(lastColon + 1);
                }
            }
        }
    }

    return ctx;
}

void CustomTextEditor::TriggerAutoComplete() {
    if (!autoComplete || readOnly || !suggestions) return;

    autoCompleteAnchor = cursors[primaryCursor].position;
    updateSuggestions();
    showAutoComplete = !currentSuggestions.empty();
    suggestionIndex = 0;
}

void CustomTextEditor::CloseAutoComplete() {
    showAutoComplete = false;
    currentSuggestions.clear();
    autoCompleteItems.clear();
}

void CustomTextEditor::triggerParamHint() {
    if (!suggestions || cursors.empty()) return;

    const TextPosition& pos = cursors[primaryCursor].position;
    if (pos.line < 0 || pos.line >= static_cast<int>(lines.size())) return;

    const std::string& line = lines[pos.line];

    // Find the matching '(' by scanning backwards from cursor, handling nesting
    int parenDepth = 0;
    int openParenCol = -1;
    int searchLine = pos.line;
    int searchCol = pos.column - 1;

    while (searchLine >= 0) {
        const std::string& sLine = lines[searchLine];
        for (int c = searchCol; c >= 0; --c) {
            char ch = sLine[c];
            if (ch == ')') parenDepth++;
            else if (ch == '(') {
                if (parenDepth == 0) {
                    openParenCol = c;
                    break;
                }
                parenDepth--;
            }
        }
        if (openParenCol >= 0) break;
        searchLine--;
        if (searchLine >= 0) searchCol = static_cast<int>(lines[searchLine].size()) - 1;
    }

    if (openParenCol < 0 || searchLine < 0) {
        closeParamHint();
        return;
    }

    // Extract function name before the '('
    const std::string& funcLine = lines[searchLine];
    int nameEnd = openParenCol;
    // Skip whitespace before '('
    while (nameEnd > 0 && std::isspace(funcLine[nameEnd - 1])) nameEnd--;
    int nameStart = nameEnd;
    while (nameStart > 0 && (std::isalnum(funcLine[nameStart - 1]) || funcLine[nameStart - 1] == '_')) nameStart--;
    std::string funcName = funcLine.substr(nameStart, nameEnd - nameStart);

    if (funcName.empty()) {
        closeParamHint();
        return;
    }

    // Try to find the parent type (check for . or : or -> before funcName)
    std::string parentType;
    int beforeFunc = nameStart - 1;
    // Skip whitespace
    while (beforeFunc >= 0 && std::isspace(funcLine[beforeFunc])) beforeFunc--;
    if (beforeFunc >= 0) {
        char accessor = funcLine[beforeFunc];
        if (accessor == '.' || accessor == ':') {
            int varEnd = beforeFunc;
            if (accessor == ':' && beforeFunc > 0 && funcLine[beforeFunc - 1] == ':') {
                // :: static call - the thing before is the type itself
                varEnd = beforeFunc - 2;
                while (varEnd >= 0 && std::isspace(funcLine[varEnd])) varEnd--;
                int varStart = varEnd + 1;
                while (varStart > 0 && (std::isalnum(funcLine[varStart - 1]) || funcLine[varStart - 1] == '_')) varStart--;
                parentType = funcLine.substr(varStart, varEnd - varStart + 1);
            } else {
                // . or single : (Lua method call)
                varEnd = beforeFunc - 1;
                while (varEnd >= 0 && std::isspace(funcLine[varEnd])) varEnd--;
                int varStart = varEnd + 1;
                while (varStart > 0 && (std::isalnum(funcLine[varStart - 1]) || funcLine[varStart - 1] == '_')) varStart--;
                std::string varName = funcLine.substr(varStart, varEnd - varStart + 1);
                parentType = inferTypeOfVariable(varName, searchLine);
                if (parentType.empty() && suggestions) {
                    parentType = suggestions->FindSymbolType(varName);
                }
                // Strip namespace
                size_t lastColon = parentType.rfind(':');
                if (lastColon != std::string::npos) {
                    parentType = parentType.substr(lastColon + 1);
                }
            }
        } else if (accessor == '>' && beforeFunc > 0 && funcLine[beforeFunc - 1] == '-') {
            // -> operator
            int varEnd = beforeFunc - 2;
            while (varEnd >= 0 && std::isspace(funcLine[varEnd])) varEnd--;
            int varStart = varEnd + 1;
            while (varStart > 0 && (std::isalnum(funcLine[varStart - 1]) || funcLine[varStart - 1] == '_')) varStart--;
            std::string varName = funcLine.substr(varStart, varEnd - varStart + 1);
            parentType = inferTypeOfVariable(varName, searchLine);
            if (parentType.empty() && suggestions) {
                parentType = suggestions->FindSymbolType(varName);
            }
            size_t lastColon = parentType.rfind(':');
            if (lastColon != std::string::npos) {
                parentType = parentType.substr(lastColon + 1);
            }
        }
    }

    auto signatures = suggestions->FindSignatures(funcName, parentType);
    if (signatures.empty()) {
        closeParamHint();
        return;
    }

    paramHintFuncName = funcName;
    paramHintSignatures = signatures;
    paramHintOverloadIndex = 0;
    paramHintAnchor = TextPosition(searchLine, openParenCol);
    showParamHint = true;

    updateParamHint();
}

void CustomTextEditor::updateParamHint() {
    if (!showParamHint || cursors.empty()) return;

    const TextPosition& pos = cursors[primaryCursor].position;

    // Validate anchor: the '(' must still exist at the anchor position
    if (paramHintAnchor.line < 0 || paramHintAnchor.line >= static_cast<int>(lines.size())) {
        closeParamHint();
        return;
    }
    const std::string& anchorLine = lines[paramHintAnchor.line];
    if (paramHintAnchor.column < 0 || paramHintAnchor.column >= static_cast<int>(anchorLine.size()) || anchorLine[paramHintAnchor.column] != '(') {
        closeParamHint();
        return;
    }

    // Verify the function name before the anchor is still the same
    int nameEnd = paramHintAnchor.column;
    while (nameEnd > 0 && std::isspace(anchorLine[nameEnd - 1])) nameEnd--;
    int nameStart = nameEnd;
    while (nameStart > 0 && (std::isalnum(anchorLine[nameStart - 1]) || anchorLine[nameStart - 1] == '_')) nameStart--;
    std::string currentFuncName = anchorLine.substr(nameStart, nameEnd - nameStart);

    if (currentFuncName != paramHintFuncName) {
        closeParamHint();
        return;
    }

    // Cursor must be after the '(' anchor
    if (pos < paramHintAnchor || (pos.line == paramHintAnchor.line && pos.column <= paramHintAnchor.column)) {
        closeParamHint();
        return;
    }

    // Count commas between paramHintAnchor '(' and cursor, respecting nesting
    int commaCount = 0;
    int parenDepth = 0;
    int startLine = paramHintAnchor.line;
    int startCol = paramHintAnchor.column + 1; // after '('
    bool inString = false;
    char stringQuote = '\0';

    for (int ln = startLine; ln <= pos.line && ln < static_cast<int>(lines.size()); ++ln) {
        const std::string& line = lines[ln];
        int colStart = (ln == startLine) ? startCol : 0;
        int colEnd = (ln == pos.line) ? pos.column : static_cast<int>(line.size());
        for (int c = colStart; c < colEnd && c < static_cast<int>(line.size()); ++c) {
            char ch = line[c];

            // Handle string literals
            if (inString) {
                if (ch == stringQuote) {
                    // Check if it's escaped (simple check, doesn't handle \\" correctly but usually fine for args)
                    if (c == 0 || line[c - 1] != '\\') {
                        inString = false;
                    }
                }
                continue;
            } else if (ch == '"' || ch == '\'') {
                inString = true;
                stringQuote = ch;
                continue;
            }

            if (ch == '(' || ch == '[' || ch == '{') parenDepth++;
            else if (ch == ')' || ch == ']' || ch == '}') {
                if (parenDepth > 0) parenDepth--;
                else {
                    closeParamHint();
                    return;
                }
            }
            else if (ch == ',' && parenDepth == 0) commaCount++;
        }
    }

    paramHintActiveParam = commaCount;
}

void CustomTextEditor::closeParamHint() {
    showParamHint = false;
    paramHintSignatures.clear();
    paramHintFuncName.clear();
    paramHintActiveParam = 0;
    paramHintOverloadIndex = 0;
}

void CustomTextEditor::renderParamHint(const ImVec2& origin) {
    if (!showParamHint || paramHintSignatures.empty()) return;

    // Bounds-check overload index
    if (paramHintOverloadIndex >= static_cast<int>(paramHintSignatures.size())) {
        paramHintOverloadIndex = 0;
    }

    const std::string& sig = paramHintSignatures[paramHintOverloadIndex];

    // Extract params from "ClassName:funcName(Type1 param1, Type2 param2)"
    size_t openParen = sig.find('(');
    size_t closeParen = sig.rfind(')');
    if (openParen == std::string::npos || closeParen == std::string::npos || closeParen <= openParen) return;

    std::string funcPrefix = sig.substr(0, openParen + 1); // "ClassName:funcName("
    std::string paramStr = sig.substr(openParen + 1, closeParen - openParen - 1);

    // Split params by comma, respecting nested angle brackets
    std::vector<std::string> params;
    int depth = 0;
    std::string current;
    for (char ch : paramStr) {
        if (ch == '<') depth++;
        else if (ch == '>') depth--;
        else if (ch == ',' && depth == 0) {
            params.push_back(current);
            current.clear();
            continue;
        }
        current += ch;
    }
    if (!current.empty() || !paramStr.empty()) {
        params.push_back(current);
    }

    // Trim leading/trailing whitespace from each param
    for (auto& p : params) {
        size_t start = p.find_first_not_of(' ');
        size_t end = p.find_last_not_of(' ');
        if (start != std::string::npos) p = p.substr(start, end - start + 1);
    }

    // Position above the '(' anchor
    ImVec2 screenPos = textToScreen(paramHintAnchor, origin);
    screenPos.y -= lineHeight + 4.0f;

    // Calculate size
    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    // Build text measurement
    std::string fullText = funcPrefix;
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) fullText += ", ";
        fullText += params[i];
    }
    fullText += ")";

    // Overload indicator
    std::string overloadText;
    if (paramHintSignatures.size() > 1) {
        overloadText = std::to_string(paramHintOverloadIndex + 1) + "/" + std::to_string(paramHintSignatures.size());
    }

    float textWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0, fullText.c_str()).x;
    float overloadWidth = overloadText.empty() ? 0 : font->CalcTextSizeA(fontSize, FLT_MAX, 0, overloadText.c_str()).x + 16.0f;
    float padding = 8.0f;
    float popupWidth = textWidth + overloadWidth + padding * 2;
    float popupHeight = lineHeight + padding;

    // Ensure popup doesn't go off-screen
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (screenPos.x + popupWidth > displaySize.x) {
        screenPos.x = displaySize.x - popupWidth - 10;
    }
    if (screenPos.x < 0) screenPos.x = 10;
    if (screenPos.y < 0) {
        screenPos.y = textToScreen(paramHintAnchor, origin).y + lineHeight + 2.0f;
    }

    ImGui::SetNextWindowPos(screenPos);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));

    if (ImGui::Begin("##ParamHint", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 textPos = ImGui::GetCursorScreenPos();

        ImU32 normalColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImU32 activeColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImU32 activeUnderline = ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.65f, 0.95f, 1.0f));

        float x = textPos.x;

        // Draw function prefix
        drawList->AddText(font, fontSize, ImVec2(x, textPos.y), normalColor, funcPrefix.c_str());
        x += font->CalcTextSizeA(fontSize, FLT_MAX, 0, funcPrefix.c_str()).x;

        // Draw each parameter
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) {
                drawList->AddText(font, fontSize, ImVec2(x, textPos.y), normalColor, ", ");
                x += font->CalcTextSizeA(fontSize, FLT_MAX, 0, ", ").x;
            }

            const std::string& param = params[i];
            bool isActive = (static_cast<int>(i) == paramHintActiveParam);
            ImU32 color = isActive ? activeColor : normalColor;

            drawList->AddText(font, fontSize, ImVec2(x, textPos.y), color, param.c_str());
            float paramWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0, param.c_str()).x;

            if (isActive) {
                // Draw underline for active param
                float underlineY = textPos.y + fontSize + 1.0f;
                drawList->AddLine(ImVec2(x, underlineY), ImVec2(x + paramWidth, underlineY), activeUnderline, 2.0f);
            }

            x += paramWidth;
        }

        // Draw closing paren
        drawList->AddText(font, fontSize, ImVec2(x, textPos.y), normalColor, ")");
        x += font->CalcTextSizeA(fontSize, FLT_MAX, 0, ")").x;

        // Draw overload indicator
        if (!overloadText.empty()) {
            x += 12.0f;
            ImU32 dimColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            drawList->AddText(font, fontSize, ImVec2(x, textPos.y), dimColor, overloadText.c_str());
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void CustomTextEditor::UpdateProjectSymbols(const std::vector<ProjectSymbol>& symbols) {
    if (!suggestions) return;

    suggestions->ClearSymbols();
    suggestions->ClearInheritance();

    // Re-add engine API symbols
    if (language == SyntaxLanguage::Lua || language == SyntaxLanguage::Cpp) {
        addEngineAPISuggestions();
    }

    // Add pre-parsed project symbols
    for (const auto& sym : symbols) {
        suggestions->AddSymbol(sym.name, static_cast<SuggestionKind>(sym.kind), sym.detail, sym.parentType, sym.typeInfo);
    }
}

void CustomTextEditor::updateSuggestions() {
    if (!suggestions) return;

    // Update document words in the suggestions engine
    suggestions->UpdateDocumentWords(lines);

    // Build context and get suggestions
    SuggestionContext ctx = buildSuggestionContext();
    autoCompleteAnchor = findWordStart(cursors[primaryCursor].position);

    currentSuggestions = suggestions->GetSuggestions(ctx);

    // Also populate the old autoCompleteItems for backward compatibility
    autoCompleteItems.clear();
    for (const auto& sugg : currentSuggestions) {
        AutoCompleteItem item;
        item.label = sugg.label;
        item.insertText = sugg.insertText;
        item.detail = sugg.detail;
        // Map suggestion kind to token type for compatibility
        switch (sugg.kind) {
            case SuggestionKind::Keyword: item.kind = TokenType::Keyword; break;
            case SuggestionKind::Type: item.kind = TokenType::Type; break;
            case SuggestionKind::Function: 
            case SuggestionKind::Method: item.kind = TokenType::Function; break;
            default: item.kind = TokenType::Identifier; break;
        }
        autoCompleteItems.push_back(item);
    }
}

void CustomTextEditor::updateAutoComplete() {
    updateSuggestions();
}

std::vector<AutoCompleteItem> CustomTextEditor::getCompletionItems(const std::string& prefix) const {
    // This method is kept for backward compatibility
    // The new system uses updateSuggestions() instead
    std::vector<AutoCompleteItem> items;

    auto addItems = [&](const std::unordered_set<std::string>& source, TokenType kind) {
        for (const auto& word : source) {
            if (prefix.empty() || word.find(prefix) == 0) {
                AutoCompleteItem item;
                item.label = word;
                item.insertText = word;
                item.kind = kind;
                items.push_back(item);
            }
        }
    };

    addItems(languageDef.keywords, TokenType::Keyword);
    addItems(languageDef.types, TokenType::Type);
    addItems(languageDef.builtinFunctions, TokenType::Function);

    // Add words from the document
    std::unordered_set<std::string> documentWords;
    for (const auto& line : lines) {
        size_t i = 0;
        while (i < line.size()) {
            if (isWordChar(line[i])) {
                size_t start = i;
                while (i < line.size() && isWordChar(line[i])) ++i;
                std::string word = line.substr(start, i - start);
                if (word.size() >= 3 && word != prefix) {
                    documentWords.insert(word);
                }
            } else {
                ++i;
            }
        }
    }
    addItems(documentWords, TokenType::Identifier);

    // Sort by relevance
    std::sort(items.begin(), items.end(), [&prefix](const AutoCompleteItem& a, const AutoCompleteItem& b) {
        bool aStartsWith = a.label.find(prefix) == 0;
        bool bStartsWith = b.label.find(prefix) == 0;
        if (aStartsWith != bStartsWith) return aStartsWith;
        return a.label < b.label;
    });

    // Limit results
    if (items.size() > 20) {
        items.resize(20);
    }

    return items;
}

void CustomTextEditor::applySuggestion() {
    if (!showAutoComplete || suggestionIndex >= static_cast<int>(currentSuggestions.size())) return;

    addUndoRecord();

    const auto& item = currentSuggestions[suggestionIndex];

    // Delete the current word being typed
    TextPosition pos = cursors[primaryCursor].position;
    TextPosition delStart = autoCompleteAnchor < pos ? autoCompleteAnchor : pos;
    TextPosition delEnd = autoCompleteAnchor < pos ? pos : autoCompleteAnchor;
    deleteRange(delStart, delEnd);
    cursors[primaryCursor].position = delStart;
    cursors[primaryCursor].selection.start = delStart;
    cursors[primaryCursor].selection.end = delStart;

    // Adjust other cursors after deletion
    for (size_t i = 0; i < cursors.size(); ++i) {
        if (static_cast<int>(i) != primaryCursor) {
            adjustCursorAfterDelete(cursors[i], delStart, delEnd);
        }
    }

    // Process snippet placeholders (simple version - just remove ${n:text} markers)
    std::string textToInsert = item.insertText;
    std::string processed;
    size_t i = 0;
    while (i < textToInsert.size()) {
        if (textToInsert[i] == '$' && i + 1 < textToInsert.size() && textToInsert[i + 1] == '{') {
            // Find the matching }
            size_t start = i + 2;
            size_t end = textToInsert.find('}', start);
            if (end != std::string::npos) {
                // Extract content after : if present
                std::string placeholder = textToInsert.substr(start, end - start);
                size_t colonPos = placeholder.find(':');
                if (colonPos != std::string::npos) {
                    processed += placeholder.substr(colonPos + 1);
                }
                i = end + 1;
                continue;
            }
        }
        processed += textToInsert[i];
        ++i;
    }

    // Insert the completion directly (avoid InsertText which creates its own undo record)
    TextPosition insertPos = cursors[primaryCursor].position;
    insertTextAtCursor(cursors[primaryCursor], processed, false);
    TextPosition afterPos = cursors[primaryCursor].position;

    // Adjust other cursors after insertion
    for (size_t i = 0; i < cursors.size(); ++i) {
        if (static_cast<int>(i) != primaryCursor) {
            adjustCursorAfterInsert(cursors[i], insertPos, afterPos);
        }
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();
    CloseAutoComplete();

    if (onTextChanged) onTextChanged();
}

void CustomTextEditor::applyAutoComplete() {
    applySuggestion();
}

void CustomTextEditor::ShowTooltip(const std::string& text, const ImVec2& pos) {
    tooltipText = text;
    tooltipPos = pos;
    showTooltipFlag = true;
    tooltipStartTime = std::chrono::steady_clock::now();
}

void CustomTextEditor::HideTooltip() {
    showTooltipFlag = false;
}

TextPosition CustomTextEditor::screenToText(const ImVec2& screenPos, const ImVec2& origin) const {
    float y = screenPos.y - origin.y;
    float x = screenPos.x - origin.x - textStartX;

    int line = std::clamp(static_cast<int>(y / lineHeight), 0, static_cast<int>(lines.size()) - 1);
    int column = std::clamp(static_cast<int>((x + charWidth * 0.5f) / charWidth), 0, 
                            static_cast<int>(lines[line].size()));

    return TextPosition(line, column);
}

ImVec2 CustomTextEditor::textToScreen(const TextPosition& pos, const ImVec2& origin) const {
    float x = origin.x + textStartX + pos.column * charWidth;
    float y = origin.y + pos.line * lineHeight;
    return ImVec2(x, y);
}

void CustomTextEditor::handleKeyboardInput() {
    ImGuiIO& io = ImGui::GetIO();

    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt = io.KeyAlt;

    // Handle autocomplete navigation first - must return to prevent editor from also processing keys
    if (showAutoComplete && !currentSuggestions.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            CloseAutoComplete();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            suggestionIndex = std::max(0, suggestionIndex - 1);
            scrollToSuggestion = true;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            suggestionIndex = std::min(static_cast<int>(currentSuggestions.size()) - 1, suggestionIndex + 1);
            scrollToSuggestion = true;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Tab)) {
            applySuggestion();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
            suggestionIndex = std::max(0, suggestionIndex - 5);
            scrollToSuggestion = true;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
            suggestionIndex = std::min(static_cast<int>(currentSuggestions.size()) - 1, suggestionIndex + 5);
            scrollToSuggestion = true;
            return;
        }
    }

    // Handle param hint Escape and overload cycling
    if (showParamHint && !showAutoComplete) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closeParamHint();
            return;
        }
        // Ctrl+Up/Down to cycle overloads when multiple exist
        if (paramHintSignatures.size() > 1 && ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                paramHintOverloadIndex = (paramHintOverloadIndex + static_cast<int>(paramHintSignatures.size()) - 1) % static_cast<int>(paramHintSignatures.size());
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                paramHintOverloadIndex = (paramHintOverloadIndex + 1) % static_cast<int>(paramHintSignatures.size());
                return;
            }
        }
    }

    // Handle special keys
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ClearExtraCursors();
        ClearSelection();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A)) {
        SelectAll();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        Copy();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X)) {
        Cut();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
        Paste();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        if (shift) {
            Redo();
        } else {
            Undo();
        }
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
        Redo();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_F)) {
        OpenFind();
    }

    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
        // Select word at cursor or add cursor at next occurrence
        if (!cursors.empty()) {
            std::string word = getRange(cursors.back().selection.getMin(), cursors.back().selection.getMax());
            if (word.empty()) {
                TextPosition start = findWordStart(cursors.back().position);
                TextPosition end = findWordEnd(cursors.back().position);
                cursors.back().selection.start = start;
                cursors.back().selection.end = end;
                cursors.back().position = end;
                primaryCursor = cursors.size() - 1;
            } else {
                // Find next occurrence and add cursor (with wrap-around)
                TextPosition searchStart = cursors.back().selection.getMax();
                bool found = false;
                // Search forward from last cursor
                for (int line = searchStart.line; line < static_cast<int>(lines.size()) && !found; ++line) {
                    size_t startCol = (line == searchStart.line) ? searchStart.column : 0;
                    size_t pos = lines[line].find(word, startCol);
                    if (pos != std::string::npos) {
                        Cursor newCursor;
                        newCursor.selection.start = TextPosition(line, static_cast<int>(pos));
                        newCursor.selection.end = TextPosition(line, static_cast<int>(pos + word.size()));
                        newCursor.position = newCursor.selection.end;
                        cursors.push_back(newCursor);
                        primaryCursor = cursors.size() - 1;
                        found = true;
                    }
                }
                // Wrap around from beginning
                if (!found) {
                    TextPosition wrapEnd = cursors.front().selection.getMin();
                    for (int line = 0; line <= wrapEnd.line && !found; ++line) {
                        size_t endCol = (line == wrapEnd.line) ? wrapEnd.column : lines[line].size();
                        size_t pos = lines[line].find(word, 0);
                        if (pos != std::string::npos && static_cast<int>(pos) < static_cast<int>(endCol)) {
                            Cursor newCursor;
                            newCursor.selection.start = TextPosition(line, static_cast<int>(pos));
                            newCursor.selection.end = TextPosition(line, static_cast<int>(pos + word.size()));
                            newCursor.position = newCursor.selection.end;
                            cursors.push_back(newCursor);
                            primaryCursor = cursors.size() - 1;
                            found = true;
                        }
                    }
                }
                scrollToCursor();
            }
        }
    }

    // Cursor movement
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
        for (auto& cursor : cursors) {
            if (!shift && !cursor.selection.isEmpty()) {
                // Collapse selection to start (VSCode behavior)
                TextPosition minPos = cursor.selection.getMin();
                cursor.position = minPos;
                cursor.selection.start = minPos;
                cursor.selection.end = minPos;
                cursor.preferredColumn = -1;
                if (ctrl) {
                    // After collapsing, also move word-left
                    moveCursorWord(cursor, -1, false);
                }
            } else if (ctrl) {
                moveCursorWord(cursor, -1, shift);
            } else {
                moveCursor(cursor, 0, -1, shift);
            }
        }
        mergeCursors();
        scrollToCursor();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
        for (auto& cursor : cursors) {
            if (!shift && !cursor.selection.isEmpty()) {
                // Collapse selection to end (VSCode behavior)
                TextPosition maxPos = cursor.selection.getMax();
                cursor.position = maxPos;
                cursor.selection.start = maxPos;
                cursor.selection.end = maxPos;
                cursor.preferredColumn = -1;
                if (ctrl) {
                    // After collapsing, also move word-right
                    moveCursorWord(cursor, 1, false);
                }
            } else if (ctrl) {
                moveCursorWord(cursor, 1, shift);
            } else {
                moveCursor(cursor, 0, 1, shift);
            }
        }
        mergeCursors();
        scrollToCursor();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (alt && ctrl) {
            // Add cursor above
            if (!cursors.empty()) {
                TextPosition pos = cursors[0].position;
                if (pos.line > 0) {
                    AddCursor(pos.line - 1, std::min(pos.column, static_cast<int>(lines[pos.line - 1].size())));
                    sortCursors();
                }
            }
        } else {
            for (auto& cursor : cursors) {
                moveCursor(cursor, -1, 0, shift);
            }
            mergeCursors();
            scrollToCursor();
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (alt && ctrl) {
            // Add cursor below
            if (!cursors.empty()) {
                TextPosition pos = cursors.back().position;
                if (pos.line < static_cast<int>(lines.size()) - 1) {
                    AddCursor(pos.line + 1, std::min(pos.column, static_cast<int>(lines[pos.line + 1].size())));
                }
            }
        } else {
            for (auto& cursor : cursors) {
                moveCursor(cursor, 1, 0, shift);
            }
            mergeCursors();
            scrollToCursor();
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        for (auto& cursor : cursors) {
            if (ctrl) {
                cursor.position = TextPosition(0, 0);
                if (!shift) {
                    cursor.selection.start = cursor.position;
                    cursor.selection.end = cursor.position;
                } else {
                    cursor.selection.end = cursor.position;
                }
            } else {
                moveCursorToLineStart(cursor, shift);
            }
        }
        mergeCursors();
        scrollToCursor();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        for (auto& cursor : cursors) {
            if (ctrl) {
                cursor.position = TextPosition(static_cast<int>(lines.size()) - 1, 
                                               static_cast<int>(lines.back().size()));
                if (!shift) {
                    cursor.selection.start = cursor.position;
                    cursor.selection.end = cursor.position;
                } else {
                    cursor.selection.end = cursor.position;
                }
            } else {
                moveCursorToLineEnd(cursor, shift);
            }
        }
        mergeCursors();
        scrollToCursor();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
        int pageSize = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / lineHeight) - 2);
        for (auto& cursor : cursors) {
            moveCursor(cursor, -pageSize, 0, shift);
        }
        mergeCursors();
        scrollToCursor();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
        int pageSize = std::max(1, static_cast<int>(ImGui::GetWindowHeight() / lineHeight) - 2);
        for (auto& cursor : cursors) {
            moveCursor(cursor, pageSize, 0, shift);
        }
        mergeCursors();
        scrollToCursor();
    }

    // Editing keys
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !readOnly) {
        if (ctrl) {
            // Ctrl+Backspace: delete word left
            addUndoRecord();
            for (auto& cursor : cursors) {
                if (!cursor.selection.isEmpty()) {
                    TextPosition delStart = cursor.selection.getMin();
                    TextPosition delEnd = cursor.selection.getMax();
                    deleteRange(delStart, delEnd);
                    cursor.position = delStart;
                } else {
                    TextPosition wordStart = findWordStart(cursor.position);
                    if (wordStart == cursor.position && wordStart.column > 0) {
                        wordStart.column--;
                        wordStart = findWordStart(wordStart);
                    }
                    if (wordStart < cursor.position) {
                        deleteRange(wordStart, cursor.position);
                        cursor.position = wordStart;
                    }
                }
                cursor.selection.start = cursor.position;
                cursor.selection.end = cursor.position;
            }
            mergeCursors();
            tokenizeAll();
            finalizeUndoRecord();
            if (onTextChanged) onTextChanged();
        } else {
            Backspace();
        }
        scrollToCursor();
        if (showAutoComplete) updateAutoComplete();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !readOnly) {
        if (ctrl) {
            // Ctrl+Delete: delete word right
            addUndoRecord();
            for (auto& cursor : cursors) {
                if (!cursor.selection.isEmpty()) {
                    TextPosition delStart = cursor.selection.getMin();
                    TextPosition delEnd = cursor.selection.getMax();
                    deleteRange(delStart, delEnd);
                    cursor.position = delStart;
                } else {
                    TextPosition wordEnd = findWordEnd(cursor.position);
                    if (wordEnd == cursor.position && wordEnd.column < static_cast<int>(lines[wordEnd.line].size())) {
                        wordEnd.column++;
                        wordEnd = findWordEnd(wordEnd);
                    }
                    if (cursor.position < wordEnd) {
                        deleteRange(cursor.position, wordEnd);
                    }
                }
                cursor.selection.start = cursor.position;
                cursor.selection.end = cursor.position;
            }
            mergeCursors();
            tokenizeAll();
            finalizeUndoRecord();
            if (onTextChanged) onTextChanged();
        } else {
            Delete();
        }
        scrollToCursor();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !readOnly) {
        if (!showAutoComplete) {
            bool handled = false;

            // Smart Enter for brackets
            if (cursors.size() == 1) {
                const auto& cursor = cursors[0];
                if (cursor.selection.isEmpty() && cursor.position.column > 0) {
                    char prevChar = getCharAt(TextPosition(cursor.position.line, cursor.position.column - 1));
                    char nextChar = getCharAt(cursor.position);

                    if ((prevChar == '{' && nextChar == '}') ||
                        (prevChar == '[' && nextChar == ']') ||
                        (prevChar == '(' && nextChar == ')')) {

                        // Break open the brackets
                        // 1. Insert indented newline
                        InsertText("\n", true);

                        // 2. Insert another newline for the closing bracket (no auto-indent)
                        InsertText("\n", false);

                        // 3. Fix indentation of closing bracket to match the parent line
                        // The parent line is now 2 lines above
                        int closingBracketLine = cursors[0].position.line;
                        int parentLine = closingBracketLine - 2;

                        if (parentLine >= 0) {
                            // Calculate base indent
                            int baseIndent = getLineIndent(parentLine);
                            std::string indentStr = getIndentString(baseIndent);

                            // Apply indent to closing bracket line
                            // Move cursor to start of line
                            SetCursorPosition(closingBracketLine, 0);
                            InsertText(indentStr, false);

                            // 4. Move cursor back to the middle line (the indented content line)
                            int contentLine = closingBracketLine - 1;
                            int contentCol = lines[contentLine].size();
                            SetCursorPosition(contentLine, contentCol);
                        }

                        handled = true;
                    }
                }
            }

            if (!handled) {
                InsertText("\n");
                scrollToCursor();
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !readOnly) {
        if (!showAutoComplete) {
            bool multiLineSelection = false;
            for (const auto& cursor : cursors) {
                if (cursor.selection.getMin().line != cursor.selection.getMax().line) {
                    multiLineSelection = true;
                    break;
                }
            }

            if (shift || multiLineSelection) {
                // Block indent / unindent
                addUndoRecord();
                bool changed = false;
                std::unordered_set<int> linesProcessed;

                for (auto& cursor : cursors) {
                    TextPosition minPos = cursor.selection.getMin();
                    TextPosition maxPos = cursor.selection.getMax();
                    int endLine = maxPos.line;
                    if (maxPos.column == 0 && endLine > minPos.line) {
                        endLine--; // Don't indent if selection just touches the start of the next line
                    }

                    for (int l = minPos.line; l <= endLine; ++l) {
                        if (linesProcessed.count(l)) continue;
                        linesProcessed.insert(l);

                        if (shift) {
                            // Unindent
                            const std::string& line = lines[l];
                            if (static_cast<int>(line.size()) >= tabSize) {
                                bool canUnindent = true;
                                for (int i = 0; i < tabSize; ++i) {
                                    if (line[i] != ' ') {
                                        canUnindent = false;
                                        break;
                                    }
                                }
                                if (canUnindent) {
                                    lines[l].erase(0, tabSize);
                                    TextPosition delStart(l, 0);
                                    TextPosition delEnd(l, tabSize);
                                    for (auto& c : cursors) {
                                        adjustCursorAfterDelete(c, delStart, delEnd);
                                    }
                                    if (showParamHint) paramHintAnchor = adjustPosAfterDelete(paramHintAnchor, delStart, delEnd);
                                    changed = true;
                                }
                            }
                        } else {
                            // Indent
                            lines[l].insert(0, tabSize, ' ');
                            TextPosition insStart(l, 0);
                            TextPosition insEnd(l, tabSize);
                            for (auto& c : cursors) {
                                adjustCursorAfterInsert(c, insStart, insEnd);
                            }
                            if (showParamHint) paramHintAnchor = adjustPosAfterInsert(paramHintAnchor, insStart, insEnd);
                            changed = true;
                        }
                    }
                }
                if (changed) {
                    tokenizeAll();
                    finalizeUndoRecord();
                    if (onTextChanged) onTextChanged();
                }
            } else {
                InsertText(std::string(tabSize, ' '));
            }
        }
    }

    // Trigger auto-complete with Ctrl+Space
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        TriggerAutoComplete();
    }

    // Update param hint after any cursor movement
    if (showParamHint) {
        updateParamHint();
    }
}

void CustomTextEditor::handleMouseInput() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 contentPos = ImGui::GetCursorScreenPos();

    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt = io.KeyAlt;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        TextPosition clickPos = screenToText(mousePos, contentPos);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClickTime).count();

        if (elapsed < 400 && clickPos == lastClickPos) {
            ++clickCount;
        } else {
            clickCount = 1;
        }

        lastClickTime = now;
        lastClickPos = clickPos;

        if (clickCount == 3) {
            // Triple-click: select line
            Cursor cursor;
            cursor.selection.start = TextPosition(clickPos.line, 0);
            cursor.selection.end = TextPosition(clickPos.line, static_cast<int>(lines[clickPos.line].size()));
            cursor.position = cursor.selection.end;

            if (!ctrl) {
                cursors.clear();
                primaryCursor = 0;
            }
            cursors.push_back(cursor);
            clickCount = 0;
        } else if (clickCount == 2) {
            // Double-click: select word
            TextPosition wordStart = findWordStart(clickPos);
            TextPosition wordEnd = findWordEnd(clickPos);

            Cursor cursor;
            cursor.selection.start = wordStart;
            cursor.selection.end = wordEnd;
            cursor.position = wordEnd;

            if (!ctrl) {
                cursors.clear();
                primaryCursor = 0;
            }
            cursors.push_back(cursor);
        } else {
             // Check for drag start on existing selection
            bool insideSelection = false;
            if (!ctrl && !shift && !alt) {
                for (const auto& cursor : cursors) {
                    if (!cursor.selection.isEmpty()) {
                        TextPosition min = cursor.selection.getMin();
                        TextPosition max = cursor.selection.getMax();
                        if (clickPos >= min && clickPos < max) {
                            insideSelection = true;
                            break;
                        }
                    }
                }
            }

            if (insideSelection) {
                mayDragText = true;
            } else {
                // Single click
                if (alt && shift && !cursors.empty()) {
                    // Box selection / Column selection
                    TextPosition anchor = cursors[primaryCursor].selection.start;
                    int minLine = std::min(anchor.line, clickPos.line);
                    int maxLine = std::max(anchor.line, clickPos.line);

                    cursors.clear();
                    primaryCursor = 0;

                    for (int l = minLine; l <= maxLine; ++l) {
                        Cursor cursor;
                        cursor.position = TextPosition(l, std::min(clickPos.column, static_cast<int>(lines[l].size())));
                        cursor.selection.start = TextPosition(l, std::min(anchor.column, static_cast<int>(lines[l].size())));
                        cursor.selection.end = cursor.position;

                        cursors.push_back(cursor);
                        if (l == clickPos.line) {
                            primaryCursor = cursors.size() - 1;
                        }
                    }
                } else if (ctrl && !shift) {
                    // Ctrl+Click: Add cursor
                    Cursor cursor;
                    cursor.position = clickPos;
                    cursor.selection.start = clickPos;
                    cursor.selection.end = clickPos;
                    cursors.push_back(cursor);
                } else if (shift && !alt && !cursors.empty()) {
                    // Extend selection
                    cursors[primaryCursor].selection.end = clickPos;
                    cursors[primaryCursor].position = clickPos;
                } else {
                    // Move cursor
                    cursors.clear();
                    Cursor cursor;
                    cursor.position = clickPos;
                    cursor.selection.start = clickPos;
                    cursor.selection.end = clickPos;
                    cursors.push_back(cursor);
                    primaryCursor = 0;
                }

                isDragging = true;
            }
        }

        CloseAutoComplete();
    }

    // Handle drag initiation
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        if (mayDragText && !isDraggingText) {
             isDraggingText = true;
             mayDragText = false;
        }
    }

    if (isDraggingText) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        if (!cursors.empty()) {
            cursors[primaryCursor].position = screenToText(mousePos, contentPos);
        }
    }

    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && isDragging) {
        TextPosition dragPos = screenToText(mousePos, contentPos);
        if (!cursors.empty()) {
            cursors[primaryCursor].selection.end = dragPos;
            cursors[primaryCursor].position = dragPos;
        }
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (isDraggingText) {
            TextPosition dropPos = screenToText(mousePos, contentPos);
            moveSelectedText(dropPos);
            isDraggingText = false;
        } else if (mayDragText) {
            // Was just a click inside selection
            cursors.clear();
            Cursor cursor;
            cursor.position = lastClickPos;
            cursor.selection.start = lastClickPos;
            cursor.selection.end = lastClickPos;
            cursors.push_back(cursor);
            primaryCursor = 0;
            mayDragText = false;
        }

        isDragging = false;
    }

    // Mouse wheel scrolling is handled by ImGui automatically

    // Update param hint after mouse clicks may move cursor
    if (showParamHint && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        updateParamHint();
    }
}

void CustomTextEditor::handleTextInput() {
    if (readOnly) return;

    ImGuiIO& io = ImGui::GetIO();

    // Handle text input
    if (io.InputQueueCharacters.Size > 0) {
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            ImWchar c = io.InputQueueCharacters[i];

            // Filter out control characters
            if (c < 32 && c != '\t' && c != '\n') continue;
            if (c >= 127 && c < 256) continue;

            bool handled = false;

            // Check for overtyping closing brackets
            char ch = static_cast<char>(c);
            if (c < 128 && (ch == ')' || ch == ']' || ch == '}')) {
                bool allMatch = true;
                for (const auto& cursor : cursors) {
                    if (getCharAt(cursor.position) != ch) {
                        allMatch = false;
                        break;
                    }
                }

                if (allMatch && !cursors.empty()) {
                    for (auto& cursor : cursors) {
                        moveCursor(cursor, 0, 1, false);
                    }
                    mergeCursors();
                    handled = true;

                    // Update param hint after ')' overtype
                    if (ch == ')' && showParamHint) {
                        updateParamHint();
                    }
                }
            }

            if (!handled) {
                char utf8[5] = {};
                if (c < 0x80) {
                    utf8[0] = static_cast<char>(c);
                } else if (c < 0x800) {
                    utf8[0] = static_cast<char>(0xC0 | (c >> 6));
                    utf8[1] = static_cast<char>(0x80 | (c & 0x3F));
                } else {
                    utf8[0] = static_cast<char>(0xE0 | (c >> 12));
                    utf8[1] = static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    utf8[2] = static_cast<char>(0x80 | (c & 0x3F));
                }

                InsertText(utf8);

                // Auto-close brackets
                if (c == '(' || c == '[' || c == '{') {
                    char closing = getMatchingBracket(static_cast<char>(c));
                    // Store position, insert, then restore
                    // We want cursor BETWEEN brackets, so we use the position returned by the first InsertText
                    auto positions = cursors; 

                    InsertText(std::string(1, closing), false);

                    // Restore cursors to 'positions' (between the brackets)
                    for (size_t j = 0; j < cursors.size() && j < positions.size(); ++j) {
                        cursors[j] = positions[j];
                    }

                    // Trigger parameter hints after '('
                    if (c == '(') {
                        triggerParamHint();
                    }
                } else if (showParamHint) {
                    // Update param hint when typing inside a function call
                    updateParamHint();
                }

                // Update auto-complete
                bool shouldTrigger = false;
                if (autoComplete) {
                    if (std::isalnum(c) || c == '_' || c == '.' || c == ':') {
                        shouldTrigger = true;
                    } else if (c == '>' && language == SyntaxLanguage::Cpp) {
                        // Trigger for '->' operator
                        const auto& pos = cursors[primaryCursor].position;
                        if (pos.column >= 2 && pos.line < static_cast<int>(lines.size())) {
                            if (lines[pos.line][pos.column - 2] == '-') {
                                shouldTrigger = true;
                            }
                        }
                    }
                }
                if (shouldTrigger) {
                    TriggerAutoComplete();
                } else if (showAutoComplete) {
                    // Close autocomplete when typing non-identifier characters
                    // like space, parentheses, operators, etc.
                    CloseAutoComplete();
                }
            }
        }

        // Scroll to cursor after text input
        scrollToCursor();
    }
}

void CustomTextEditor::renderLineNumbers(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine) {
    if (!showLineNumbers) return;

    ImU32 color = ImGui::ColorConvertFloat4ToU32(lineNumberColor);

    for (int i = startLine; i <= endLine && i < static_cast<int>(lines.size()); ++i) {
        float y = origin.y + i * lineHeight;

        char lineNum[16];
        snprintf(lineNum, sizeof(lineNum), "%4d", i + 1);

        drawList->AddText(ImVec2(origin.x + leftMargin, y), color, lineNum);
    }
}

void CustomTextEditor::renderSelections(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine) {
    ImU32 selColor = ImGui::ColorConvertFloat4ToU32(selectionColor);

    for (const auto& cursor : cursors) {
        if (cursor.selection.isEmpty()) continue;

        TextPosition selStart = cursor.selection.getMin();
        TextPosition selEnd = cursor.selection.getMax();

        for (int line = std::max(startLine, selStart.line); 
             line <= std::min(endLine, selEnd.line) && line < static_cast<int>(lines.size()); 
             ++line) {
            int startCol = (line == selStart.line) ? selStart.column : 0;
            int endCol = (line == selEnd.line) ? selEnd.column : static_cast<int>(lines[line].size());

            float x1 = origin.x + textStartX + startCol * charWidth;
            float x2 = origin.x + textStartX + endCol * charWidth;

            // If the selection extends past this line, highlight the newline character
            if (line < selEnd.line) {
                x2 += charWidth;
            }

            float y = origin.y + line * lineHeight;

            if (x2 > x1) {
                drawList->AddRectFilled(ImVec2(x1, y), ImVec2(x2, y + lineHeight), selColor);
            }
        }
    }
}

void CustomTextEditor::renderSearchHighlights(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine) {
    if (searchResults.empty()) return;

    ImU32 highlightColor = ImGui::ColorConvertFloat4ToU32(searchHighlightColor);

    for (const auto& result : searchResults) {
        if (result.line < startLine || result.line > endLine) continue;

        float x = origin.x + textStartX + result.column * charWidth;
        float y = origin.y + result.line * lineHeight;
        float width = searchText.size() * charWidth;

        drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + lineHeight), highlightColor);
    }
}

void CustomTextEditor::renderText(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine) {
    for (int i = startLine; i <= endLine && i < static_cast<int>(lines.size()); ++i) {
        float y = origin.y + i * lineHeight;
        float x = origin.x + textStartX;

        const std::string& line = lines[i];
        const auto& tokens = (i < static_cast<int>(lineTokens.size())) ? lineTokens[i] : std::vector<Token>();

        if (tokens.empty()) {
            // Render entire line in default color
            ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(TokenType::Default)]);
            drawList->AddText(ImVec2(x, y), color, line.c_str());
        } else {
            // Render tokens
            int lastEnd = 0;
            for (const auto& token : tokens) {
                // Render any gap before this token
                if (token.start > lastEnd) {
                    std::string gap = line.substr(lastEnd, token.start - lastEnd);
                    ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(TokenType::Default)]);
                    drawList->AddText(ImVec2(x + lastEnd * charWidth, y), color, gap.c_str());
                }

                // Render the token
                std::string tokenText = line.substr(token.start, token.length);
                ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(token.type)]);
                drawList->AddText(ImVec2(x + token.start * charWidth, y), color, tokenText.c_str());

                lastEnd = token.start + token.length;
            }

            // Render any remaining text
            if (lastEnd < static_cast<int>(line.size())) {
                std::string remaining = line.substr(lastEnd);
                ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(TokenType::Default)]);
                drawList->AddText(ImVec2(x + lastEnd * charWidth, y), color, remaining.c_str());
            }
        }
    }
}

void CustomTextEditor::renderCursors(ImDrawList* drawList, const ImVec2& origin) {
    // Only show cursor when window is focused
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

    // Blink cursor
    float time = ImGui::GetTime();
    bool showCursor = isDraggingText || (fmod(time, 1.0f) < 0.5f);

    if (!showCursor) return;

    ImU32 color = ImGui::ColorConvertFloat4ToU32(cursorColor);

    for (const auto& cursor : cursors) {
        ImVec2 pos = textToScreen(cursor.position, origin);
        drawList->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x, pos.y + lineHeight), color, 2.0f);
    }
}

void CustomTextEditor::renderMatchingBrackets(ImDrawList* drawList, const ImVec2& origin) {
    if (!matchBrackets || cursors.empty()) return;

    TextPosition cursorPos = cursors[primaryCursor].position;

    // Check character at cursor and before cursor
    TextPosition checkPos = cursorPos;
    char c = getCharAt(checkPos);

    if (!isOpenBracket(c) && !isCloseBracket(c)) {
        if (cursorPos.column > 0) {
            checkPos.column--;
            c = getCharAt(checkPos);
        }
    }

    if (!isOpenBracket(c) && !isCloseBracket(c)) return;

    TextPosition matchPos;
    if (findMatchingBracket(checkPos, matchPos) != '\0') {
        ImU32 color = ImGui::ColorConvertFloat4ToU32(matchingBracketColor);

        ImVec2 pos1 = textToScreen(checkPos, origin);
        drawList->AddRectFilled(pos1, ImVec2(pos1.x + charWidth, pos1.y + lineHeight), color);

        ImVec2 pos2 = textToScreen(matchPos, origin);
        drawList->AddRectFilled(pos2, ImVec2(pos2.x + charWidth, pos2.y + lineHeight), color);
    }
}

void CustomTextEditor::renderSuggestions(const ImVec2& origin) {
    suggestionsHovered = false;

    if (!showAutoComplete || currentSuggestions.empty()) return;

    // Position popup directly under the current word being typed
    TextPosition pos = autoCompleteAnchor;
    ImVec2 screenPos = textToScreen(pos, origin);
    // Offset by 1 pixel to be directly under the text baseline
    screenPos.y += lineHeight + 1.0f;

    // Popup dimensions - use GetTextLineHeight for exact line height without spacing
    float popupWidth = 350.0f;
    float itemHeight = ImGui::GetTextLineHeight() + 4.0f; // Add small padding
    float maxVisibleItems = 10.0f;
    float popupMaxHeight = itemHeight * maxVisibleItems + 8.0f; // 8 for padding
    float popupActualHeight = std::min(popupMaxHeight, itemHeight * currentSuggestions.size() + 8.0f);

    // Ensure popup doesn't go off-screen
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    if (screenPos.x + popupWidth > displaySize.x) {
        screenPos.x = displaySize.x - popupWidth - 10;
    }
    if (screenPos.x < 0) {
        screenPos.x = 10;
    }

    // If popup would go below screen, show it above the cursor instead
    if (screenPos.y + popupActualHeight > displaySize.y) {
        screenPos.y = textToScreen(pos, origin).y - popupActualHeight - 2.0f;
    }

    ImGui::SetNextWindowPos(screenPos);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupActualHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.12f, 0.12f, 0.12f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.4f, 0.4f, 0.4f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0.6f, 0.6f, 0.6f, 0.8f));

    if (ImGui::Begin("##SemanticSuggestions", nullptr, flags)) {
        // Check if mouse is hovering this window to block editor scroll
        suggestionsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | 
                                                     ImGuiHoveredFlags_ChildWindows |
                                                     ImGuiHoveredFlags_RootAndChildWindows);

        for (int i = 0; i < static_cast<int>(currentSuggestions.size()); ++i) {
            const auto& item = currentSuggestions[i];

            bool selected = (i == suggestionIndex);

            ImGui::PushID(i);

            // Calculate row bounds for custom rendering
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float availWidth = ImGui::GetContentRegionAvail().x;

            // VSCode-like dark blue selection color
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.016f, 0.224f, 0.369f, 1.0f)); // #04395e
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.17f, 0.27f, 0.44f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.016f, 0.224f, 0.369f, 1.0f));

            // Render selectable with exact item height
            if (ImGui::Selectable("##item", selected, ImGuiSelectableFlags_None, ImVec2(availWidth, itemHeight))) {
                suggestionIndex = i;
                applySuggestion();
            }

            ImGui::PopStyleColor(3);

            // Scroll to selected item only if navigation occurred
            if (selected && scrollToSuggestion) {
                ImGui::SetScrollHereY();
            }

            // Save cursor position for next item (after selectable advances it)
            ImVec2 nextItemPos = ImGui::GetCursorPos();

            // Draw content on top of selectable - center vertically
            float textY = cursorPos.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 4.0f, textY));

            // Icon with color
            ImVec4 iconColor = SemanticSuggestions::GetKindColor(item.kind);
            const char* icon = SemanticSuggestions::GetKindIcon(item.kind);
            ImGui::TextColored(iconColor, "%s", icon);
            ImGui::SameLine();

            // Label
            ImGui::TextUnformatted(item.label.c_str());

            // Detail (right-aligned, dimmed)
            if (!item.detail.empty()) {
                float labelWidth = ImGui::CalcTextSize(item.label.c_str()).x;
                float iconWidth = ImGui::CalcTextSize(icon).x + ImGui::GetStyle().ItemSpacing.x;
                float detailWidth = ImGui::CalcTextSize(item.detail.c_str()).x;
                float spacing = availWidth - labelWidth - iconWidth - detailWidth - 20;

                if (spacing > 20) {
                    ImGui::SameLine(0, spacing);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                    ImGui::TextUnformatted(item.detail.c_str());
                    ImGui::PopStyleColor();
                }
            }

            // Restore cursor position for next item
            ImGui::SetCursorPos(nextItemPos);

            ImGui::PopID();

            // Show documentation tooltip on hover
            if (ImGui::IsItemHovered() && !item.documentation.empty()) {
                ImGui::SetTooltip("%s", item.documentation.c_str());
            }
        }

        // Reset scroll flag after rendering
        scrollToSuggestion = false;
    }
    ImGui::End();

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(4);
}

void CustomTextEditor::renderAutoComplete(const ImVec2& origin) {
    renderSuggestions(origin);
    renderParamHint(origin);
}

void CustomTextEditor::renderTooltip() {
    if (!showTooltipFlag || tooltipText.empty()) return;

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - tooltipStartTime).count();

    if (elapsed < 500) return; // Delay before showing tooltip

    ImGui::SetNextWindowPos(tooltipPos);
    ImGui::SetTooltip("%s", tooltipText.c_str());
}

void CustomTextEditor::renderFindDialog(const ImVec2& editorPos, const ImVec2& editorSize) {
    if (!showFindDialog) return;

    const float padding = 8.0f;
    const float spacing = 4.0f;

    ImVec2 dialogPos(editorPos.x + editorSize.x - 15.0f, editorPos.y + padding);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, 4));

    // Calculate dialog size first, then position it from the right edge
    ImGui::SetNextWindowPos(dialogPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f)); // Pivot at right edge

    if (ImGui::Begin("##FindDialog", nullptr, flags)) {

        // Input field with search input utility - use a fixed width group
        const float inputWidth = 180.0f;
        const float controlHeight = ImGui::GetFrameHeight();

        bool focusInput = ImGui::IsWindowAppearing();

        // Toggle replace
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));

        // Align arrow vertically with the input box
        float currentY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(currentY + (controlHeight - 16.0f) * 0.5f);

        if (ImGui::Button(showReplaceInput ? ICON_FA_CHEVRON_DOWN "##ToggleReplace" : ICON_FA_CHEVRON_RIGHT "##ToggleReplace", ImVec2(16, 16))) {
            showReplaceInput = !showReplaceInput;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Replace");

        ImGui::SetCursorPosY(currentY); // Restore Y position
        ImGui::SameLine();

        // Track if we need to refocus after find
        if (findRefocusInput) {
            ImGui::SetKeyboardFocusHere();
            findRefocusInput = false;
        } else if (focusInput) {
            ImGui::SetKeyboardFocusHere();
        }

        // Wrap in a group with fixed width for the search input
        float findInputStartX = ImGui::GetCursorPosX();
        ImGui::BeginGroup();

        // Use UIUtils::searchInput for consistent look with Output window
        if (UIUtils::searchInput("##FindInput", "Find...", findInputBuffer, sizeof(findInputBuffer), false, &findCaseSensitive, inputWidth)) {
            // Text changed, update search
            if (strcmp(searchText.c_str(), findInputBuffer) != 0) {
                SetSearchText(findInputBuffer);
            }
        }

        // Check for Enter key while the window is focused
        bool enterPressed = ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
        bool shiftEnterPressed = ImGui::IsWindowFocused() && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter);

        ImGui::EndGroup();

        // Check if case sensitivity changed via the search input popup
        if (findLastCaseSensitive != findCaseSensitive) {
            updateSearchResults();
            findLastCaseSensitive = findCaseSensitive;
        }

        // Handle Enter key for find next/previous
        if (shiftEnterPressed) {
            FindPrevious(findCaseSensitive, findWholeWord);
            findRefocusInput = true;
        } else if (enterPressed) {
            FindNext(findCaseSensitive, findWholeWord);
            findRefocusInput = true;
        }

        ImGui::SameLine();

        // Results count with fixed width
        char countBuf[32];
        if (!searchResults.empty()) {
            int current = currentSearchResult >= 0 ? currentSearchResult + 1 : 0;
            snprintf(countBuf, sizeof(countBuf), "%d/%d", current, static_cast<int>(searchResults.size()));
        } else {
            snprintf(countBuf, sizeof(countBuf), "0/0");
        }

        float countWidth = 50.0f;
        float textWidth = ImGui::CalcTextSize(countBuf).x;
        float offsetX = (countWidth - textWidth) * 0.5f;

        ImVec2 cursorPos = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(cursorPos.x + offsetX);

        if (!searchResults.empty()) {
            ImGui::Text("%s", countBuf);
        } else if (strlen(findInputBuffer) > 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", countBuf);
        } else {
            ImGui::TextDisabled("%s", countBuf);
        }

        ImGui::SameLine();
        ImGui::SetCursorPosX(cursorPos.x + countWidth);

        ImGui::SameLine();

        // Previous button
        if (ImGui::Button(ICON_FA_CHEVRON_UP "##Prev") || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_UpArrow))) {
            FindPrevious(findCaseSensitive, findWholeWord);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous (Shift+Enter)");

        ImGui::SameLine();

        // Next button
        if (ImGui::Button(ICON_FA_CHEVRON_DOWN "##Next") || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
            FindNext(findCaseSensitive, findWholeWord);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next (Enter)");

        ImGui::SameLine(0.0f, 15.0f);

        // Close button
        if (ImGui::Button(ICON_FA_XMARK "##Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            CloseFind();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close (Escape)");

        if (showReplaceInput) {
            ImGui::SetCursorPosX(findInputStartX);

            ImGui::SetNextItemWidth(inputWidth);
            ImGui::InputTextWithHint("##ReplaceInput", "Replace", replaceInputBuffer, sizeof(replaceInputBuffer));

            bool replaceEnter = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
            bool replaceShiftEnter = ImGui::IsItemFocused() && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter);

            ImGui::SameLine();

            if (ImGui::Button("Replace") || (replaceEnter && !replaceShiftEnter)) {
                ReplaceNext(findCaseSensitive, findWholeWord);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace Next (Enter)");

            ImGui::SameLine();

            if (ImGui::Button("All") || replaceShiftEnter) {
                ReplaceAll(findInputBuffer, replaceInputBuffer, findCaseSensitive);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Replace All (Shift+Enter)");
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);
}

void CustomTextEditor::Render(const char* title, const ImVec2& size, bool border) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, backgroundColor);

    ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoMove;

    // Prevent ImGui from handling navigation (scrolling with arrows etc) for this window
    // This fixes the issue where pressing Up/Down in the suggestions popup would also scroll the editor
    flags |= ImGuiWindowFlags_NoNavInputs;

    ImVec2 contentSize = size;
    if (contentSize.x == 0) contentSize.x = ImGui::GetContentRegionAvail().x;
    if (contentSize.y == 0) contentSize.y = ImGui::GetContentRegionAvail().y;

    if (ImGui::BeginChild(title, contentSize, border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None, flags)) {
        // Auto-focus editor child when parent window is focused but child is not
        // (e.g., user clicked the window tab/title bar)
        if (!ImGui::IsWindowFocused() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            ImGui::SetWindowFocus();
        }

        if (pendingFocus) {
            ImGui::SetWindowFocus();
            pendingFocus = false;
        }
        // Calculate metrics first
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        charWidth = font->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, "X").x;
        lineHeight = ImGui::GetTextLineHeightWithSpacing();

        if (showLineNumbers) {
            char lineNumBuf[16];
            snprintf(lineNumBuf, sizeof(lineNumBuf), "%d", static_cast<int>(lines.size()));
            lineNumberWidth = font->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, lineNumBuf).x + leftMargin * 2;
        } else {
            lineNumberWidth = 0;
        }

        textStartX = lineNumberWidth + leftMargin;

        // Define content size for proper scrolling
        float maxLineWidth = 0.0f;
        for (const auto& line : lines) {
            float lineWidth = line.size() * charWidth;
            if (lineWidth > maxLineWidth) maxLineWidth = lineWidth;
        }
        float totalWidth = textStartX + maxLineWidth + 50.0f;
        float totalHeight = lines.size() * lineHeight + lineHeight;

        // Create a dummy to set content size for scrolling
        ImVec2 cursorBackup = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Dummy(ImVec2(totalWidth, totalHeight));
        ImGui::SetCursorPos(cursorBackup);

        // Use ImGui's scroll position (but don't update if suggestions popup is hovered)
        // Also restore scroll position if popup is hovered to prevent drift
        if (!suggestionsHovered) {
            scrollY = ImGui::GetScrollY();
            scrollX = ImGui::GetScrollX();
        } else {
            // Restore scroll position to prevent parent window from scrolling
            ImGui::SetScrollY(scrollY);
            ImGui::SetScrollX(scrollX);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // Render autocomplete popup first (so it can capture mouse events)
        ImGui::PushFont(ImGui::GetIO().FontDefault);
        renderAutoComplete(origin);
        ImGui::PopFont();

        // Handle pending scroll (from Find dialog)
        if (pendingScrollToCursor) {
            scrollToCursor();
            pendingScrollToCursor = false;
        }

        // Handle input (but not mouse input if suggestions popup is hovered)
        bool isHovered = ImGui::IsWindowHovered() && !suggestionsHovered;
        if (ImGui::IsWindowFocused()) {
            handleKeyboardInput();
            handleTextInput();
        }
        if ((isHovered || isDragging || isDraggingText) && !suggestionsHovered) {
            handleMouseInput();
        }

        if (isDraggingText) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        } else if (isHovered) {
            // Change to arrow cursor only when hovering the scrollbars
            ImGuiStyle& style = ImGui::GetStyle();
            ImVec2 mPos = ImGui::GetIO().MousePos;
            ImVec2 wPos = ImGui::GetWindowPos();
            ImVec2 wSize = ImGui::GetWindowSize();

            float scrollbarSize = style.ScrollbarSize;
            float right = wPos.x + wSize.x;
            float bottom = wPos.y + wSize.y;

            // Check if mouse is in vertical scrollbar area (right edge)
            bool overVScrollbar = (mPos.x >= right - scrollbarSize && mPos.x <= right);
            // Check if mouse is in horizontal scrollbar area (bottom edge)
            bool overHScrollbar = (mPos.y >= bottom - scrollbarSize && mPos.y <= bottom);

            ImGui::SetMouseCursor((overVScrollbar || overHScrollbar) ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_TextInput);
        }

        // Calculate visible lines
        int startLine = static_cast<int>(scrollY / lineHeight);
        int endLine = startLine + static_cast<int>(contentSize.y / lineHeight) + 2;

        // Render current line highlight
        if (highlightCurrentLine && !cursors.empty()) {
            ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(currentLineColor);
            float y = origin.y + cursors[primaryCursor].position.line * lineHeight;
            drawList->AddRectFilled(
                ImVec2(origin.x, y),
                ImVec2(origin.x + contentSize.x, y + lineHeight),
                lineColor
            );
        }

        // Render components
        renderSearchHighlights(drawList, origin, startLine, endLine);
        renderSelections(drawList, origin, startLine, endLine);
        renderMatchingBrackets(drawList, origin);
        renderLineNumbers(drawList, origin, startLine, endLine);
        renderText(drawList, origin, startLine, endLine);
        renderCursors(drawList, origin);

        // Context Menu
        ImGui::PushFont(ImGui::GetIO().FontDefault);
        if (ImGui::BeginPopupContextWindow("##EditorContextMenu")) {
            if (ImGui::MenuItem(ICON_FA_SCISSORS " Cut", "Ctrl+X", false, !readOnly && HasSelection())) {
                Cut();
            }
            if (ImGui::MenuItem(ICON_FA_COPY " Copy", "Ctrl+C", false, HasSelection())) {
                Copy();
            }
            if (ImGui::MenuItem(ICON_FA_PASTE " Paste", "Ctrl+V", false, !readOnly && ImGui::GetClipboardText() != nullptr)) {
                Paste();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS " Find", "Ctrl+F")) {
                OpenFind();
            }
            ImGui::EndPopup();
        }
        ImGui::PopFont();

        // Render Find Dialog
        ImGui::PushFont(ImGui::GetIO().FontDefault);
        renderFindDialog(ImGui::GetWindowPos(), contentSize);
        ImGui::PopFont();

    }
    ImGui::EndChild();

    ImGui::PopStyleColor();

    // Render tooltip overlay
    renderTooltip();
}

} // namespace doriax::editor
