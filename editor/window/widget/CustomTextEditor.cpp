// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#include "CustomTextEditor.h"
#include "InputTextContextMenu.h"
#include "SemanticSuggestions.h"
#include "engine_api_suggestions.h"
#include "external/IconsFontAwesome6.h"
#include "util/UIUtils.h"
#include "App.h"
#include "Theme.h"

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

namespace {

// Case-folded copy, for case-insensitive search
std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Identifier that ends at endColumn, skipping trailing whitespace
std::string wordEndingAt(const std::string& line, int endColumn) {
    int end = std::min(endColumn, static_cast<int>(line.size()) - 1);
    while (end >= 0 && std::isspace(static_cast<unsigned char>(line[end]))) end--;

    int start = end + 1;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[start - 1])) || line[start - 1] == '_')) start--;

    if (start > end) return "";
    return line.substr(start, end - start + 1);
}

bool isEngineApiConstructorSymbol(const EngineAPISymbol& sym) {
    if (!sym.name || !sym.parent || !sym.detail) return false;

    const std::string name = sym.name;
    const std::string parent = sym.parent;
    const std::string detail = sym.detail;
    return !name.empty() &&
           parent == name &&
           detail.rfind(name + "(", 0) == 0;
}

bool isUtf8Continuation(unsigned char c) {
    return (c & 0xC0) == 0x80;
}

} // namespace

CustomTextEditor::CustomTextEditor()
    : primaryCursor(0)
    , undoIndex(0)
    , language(SyntaxLanguage::None)
    , readOnly(false)
    , tabSize(4)
    , showLineNumbers(true)
    , autoIndent(true)
    , highlightCurrentLine(true)
    , matchBrackets(true)
    , autoComplete(true)
    , isDragging(false)
    , isDraggingText(false)
    , mayDragText(false)
    , clickCount(0)
    , scrollX(0)
    , scrollY(0)
    , showAutoComplete(false)
    , suggestionIndex(0)
    , suggestionsHovered(false)
    , showParamHint(false)
    , paramHintActiveParam(0)
    , paramHintOverloadIndex(0)
    , currentSearchResult(-1)
    , showFindDialog(false)
    , showReplaceInput(false)
    , findCaseSensitive(false)
    , charWidth(0)
    , lineHeight(0)
    , lineHeightFactor(1.35f)
    , textOffsetY(0)
    , lineNumberDigits(1)
    , lineNumberWidth(0)
    , leftMargin(10)
    , textStartX(0)
    , measureFont(nullptr)
    , measureFontSize(0)
    , maxLineWidth(0)
    , maxLineWidthDirty(true)
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

    // Snippets and symbols are appended, so a language switch has to drop the old ones
    suggestions->ClearSnippets();
    suggestions->ClearSymbols();
    suggestions->ClearInheritance();

    suggestions->SetMinPrefixLength(1);
    // The popup scrolls, so a high cap keeps inherited members from being crowded out
    suggestions->SetMaxSuggestions(200);
    suggestions->SetFuzzyMatching(true);
    suggestions->SetCaseSensitive(false);

    suggestions->SetKeywords(languageDef.keywords);
    suggestions->SetTypes(languageDef.types);
    suggestions->SetBuiltinFunctions(languageDef.builtinFunctions);

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
        {"CppMethod", SuggestionKind::Method},
        {"Function", SuggestionKind::Function},
        {"Property", SuggestionKind::Property},
        {"Constant", SuggestionKind::Constant},
        {"Variable", SuggestionKind::Variable},
    };

    // "CppMethod" is unbound in Lua: suggesting it there yields a nil call at runtime.
    const bool isLua = (language == SyntaxLanguage::Lua);

    for (const auto& sym : apiSymbols) {
        if (isEngineApiConstructorSymbol(sym)) continue;
        if (isLua && sym.kind && std::string(sym.kind) == "CppMethod") continue;

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

void CustomTextEditor::setLinesFromText(const std::string& text) {
    lines.clear();

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
}

void CustomTextEditor::SetText(const std::string& text) {
    setLinesFromText(text);
    tokenizeAll();

    cursors.clear();
    cursors.push_back(Cursor());
    primaryCursor = 0;

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

void CustomTextEditor::SetCursorPosition(int line, int column) {
    cursors.clear();
    Cursor cursor;
    cursor.position = clampPosition(TextPosition(line, column));
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

void CustomTextEditor::GetCursorDisplayPosition(int& line, int& column) const {
    GetCursorPosition(line, column);
    column = byteOffsetToVisualColumn(line, column);
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
    cursor.position = clampPosition(TextPosition(line, column));
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
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
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
        if (std::isdigit(static_cast<unsigned char>(line[i])) || (line[i] == '.' && i + 1 < len && std::isdigit(static_cast<unsigned char>(line[i + 1])))) {
            int start = i;
            bool hasDecimal = false;
            bool hasExponent = false;

            // Hex prefix
            if (line[i] == '0' && i + 1 < len && (line[i + 1] == 'x' || line[i + 1] == 'X')) {
                i += 2;
                while (i < len && std::isxdigit(static_cast<unsigned char>(line[i]))) ++i;
            } else {
                while (i < len) {
                    if (std::isdigit(static_cast<unsigned char>(line[i]))) {
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
        if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
            int start = i;
            while (i < len && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;

            std::string word = line.substr(start, i - start);
            TokenType type = classifyWord(word);

            // A name followed by '(' is a call
            int j = i;
            while (j < len && std::isspace(static_cast<unsigned char>(line[j]))) ++j;
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
            while (i < len && std::strchr(operators, line[i])) ++i;
            lineTokens[lineIndex].push_back({start, i - start, TokenType::Operator});
            continue;
        }

        if (std::strchr(punctuation, line[i])) {
            lineTokens[lineIndex].push_back({i, 1, TokenType::Punctuation});
            ++i;
            continue;
        }

        // Non-ASCII runs stay whole so every token is valid UTF-8
        if (static_cast<unsigned char>(line[i]) >= 0x80) {
            int start = i;
            while (i < len && static_cast<unsigned char>(line[i]) >= 0x80) ++i;
            lineTokens[lineIndex].push_back({start, i - start, TokenType::Default});
            continue;
        }

        lineTokens[lineIndex].push_back({i, 1, TokenType::Default});
        ++i;
    }
}

void CustomTextEditor::tokenizeAll() {
    // Every edit retokenizes, so this is also where cached widths expire
    maxLineWidthDirty = true;
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

    std::vector<size_t> order = cursorEditOrder();

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];

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
            int visualCol = byteOffsetToVisualColumn(line, col);
            int spacesToInsert = tabSize - (visualCol % tabSize);
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
    cursor.preferredColumn = byteOffsetToVisualColumn(line, col);
}

void CustomTextEditor::DeleteSelection() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order = cursorEditOrder();

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
}

void CustomTextEditor::Backspace() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order = cursorEditOrder();

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
            cursor.position.column = prevUtf8Column(cursor.position.line, cursor.position.column);
            delStart = cursor.position;
            lines[cursor.position.line].erase(delStart.column, delEnd.column - delStart.column);
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
}

void CustomTextEditor::Delete() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order = cursorEditOrder();

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
                delEnd = TextPosition(cursor.position.line, nextUtf8Column(cursor.position.line, cursor.position.column));
                lines[cursor.position.line].erase(delStart.column, delEnd.column - delStart.column);
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
}

void CustomTextEditor::DeleteWordLeft() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order = cursorEditOrder();

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];

        TextPosition delStart, delEnd;

        if (!cursor.selection.isEmpty()) {
            delStart = cursor.selection.getMin();
            delEnd = cursor.selection.getMax();
        } else {
            delStart = findDeleteWordStart(cursor.position);
            delEnd = cursor.position;
        }

        if (delStart < delEnd) {
            deleteRange(delStart, delEnd);
            cursor.position = delStart;

            for (size_t j = idx + 1; j < order.size(); ++j) {
                adjustCursorAfterDelete(cursors[order[j]], delStart, delEnd);
            }
        }

        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();
}

void CustomTextEditor::DeleteWordRight() {
    if (readOnly) return;

    addUndoRecord();

    std::vector<size_t> order = cursorEditOrder();

    for (size_t idx = 0; idx < order.size(); ++idx) {
        auto& cursor = cursors[order[idx]];

        TextPosition delStart, delEnd;

        if (!cursor.selection.isEmpty()) {
            delStart = cursor.selection.getMin();
            delEnd = cursor.selection.getMax();
        } else {
            delStart = cursor.position;
            delEnd = findDeleteWordEnd(cursor.position);
        }

        if (delStart < delEnd) {
            deleteRange(delStart, delEnd);
            cursor.position = delStart;

            for (size_t j = idx + 1; j < order.size(); ++j) {
                adjustCursorAfterDelete(cursors[order[j]], delStart, delEnd);
            }
        }

        cursor.selection.start = cursor.position;
        cursor.selection.end = cursor.position;
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();
}

void CustomTextEditor::deleteRange(const TextPosition& start, const TextPosition& end) {
    if (start == end) return;

    TextPosition minPos = start < end ? start : end;
    TextPosition maxPos = start < end ? end : start;

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
            undoBuffer[undoIndex].afterText = GetText();
            undoBuffer[undoIndex].afterCursors = cursors;

            setLinesFromText(undoBuffer[undoIndex].beforeText);
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
            setLinesFromText(undoBuffer[undoIndex].afterText);
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
            // Keep the previous record and its beforeText, finalize updates its afterText
            last.isMerged = true;
            return;
        }
    }

    undoBuffer.push_back(record);
    ++undoIndex;

    while (undoBuffer.size() > maxUndoSteps) {
        undoBuffer.erase(undoBuffer.begin());
        if (undoIndex > 0) --undoIndex;
    }
}

void CustomTextEditor::finalizeUndoRecord() {
    if (!undoBuffer.empty() && undoIndex > 0) {
        undoBuffer[undoIndex - 1].afterText = GetText();
        undoBuffer[undoIndex - 1].afterCursors = cursors;
    }
}

void CustomTextEditor::Copy() {
    std::string text = GetSelectedText();
    if (text.empty()) {
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
    searchText.clear();
    searchResults.clear();
    currentSearchResult = -1;
}

void CustomTextEditor::updateSearchResults() {
    searchResults.clear();
    currentSearchResult = -1;

    if (searchText.empty()) return;

    std::string searchLower = findCaseSensitive ? searchText : toLower(searchText);

    for (int lineIdx = 0; lineIdx < static_cast<int>(lines.size()); ++lineIdx) {
        std::string line = findCaseSensitive ? lines[lineIdx] : toLower(lines[lineIdx]);

        size_t pos = 0;
        while ((pos = line.find(searchLower, pos)) != std::string::npos) {
            searchResults.push_back(TextPosition(lineIdx, static_cast<int>(pos)));
            ++pos;
        }
    }
}

bool CustomTextEditor::ReplaceNext() {
    if (searchText.empty() || readOnly) return false;

    TextPosition pMin = cursors[primaryCursor].selection.getMin();
    TextPosition pMax = cursors[primaryCursor].selection.getMax();

    bool match = false;
    if (!cursors[primaryCursor].selection.isEmpty() && pMin.line == pMax.line && static_cast<size_t>(pMax.column - pMin.column) == searchText.size()) {
        std::string selText = lines[pMin.line].substr(pMin.column, searchText.size());
        match = findCaseSensitive ? (selText == searchText)
                                  : (toLower(selText) == toLower(searchText));
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

    return FindNext();
}

bool CustomTextEditor::FindNext() {
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

            cursors[primaryCursor].selection.start = searchResults[i];
            cursors[primaryCursor].selection.end = TextPosition(
                searchResults[i].line,
                searchResults[i].column + static_cast<int>(searchText.size())
            );
            cursors[primaryCursor].position = cursors[primaryCursor].selection.end;

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

bool CustomTextEditor::FindPrevious() {
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

int CustomTextEditor::ReplaceAll(const std::string& find, const std::string& replace, bool caseSensitive) {
    if (find.empty() || readOnly) return 0;

    addUndoRecord();

    const std::string findText = caseSensitive ? find : toLower(find);

    int count = 0;
    for (int lineIdx = static_cast<int>(lines.size()) - 1; lineIdx >= 0; --lineIdx) {
        std::string lineText = caseSensitive ? lines[lineIdx] : toLower(lines[lineIdx]);

        size_t pos = lineText.rfind(findText);
        while (pos != std::string::npos) {
            lines[lineIdx].replace(pos, find.size(), replace);
            ++count;
            if (pos == 0) break;

            // The replacement changed the line, so the folded view is rebuilt
            lineText = caseSensitive ? lines[lineIdx] : toLower(lines[lineIdx]);
            pos = lineText.rfind(findText, pos - 1);
        }
    }

    if (count > 0) {
        tokenizeAll();
        finalizeUndoRecord();
        updateSearchResults();
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
    float cursorX = textStartX + byteOffsetToPixelX(cursorPos.line, cursorPos.column);

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

    mergeCursors();

    std::string text = GetSelectedText();
    if (text.empty()) return;

    TextPosition clampedDest = destPos;
    clampedDest.line = std::clamp(clampedDest.line, 0, static_cast<int>(lines.size()) - 1);
    clampedDest.column = std::clamp(clampedDest.column, 0, static_cast<int>(lines[clampedDest.line].size()));

    // Dropping inside the selection itself moves nothing
    for (const auto& cursor : cursors) {
        if (!cursor.selection.isEmpty()) {
            TextPosition min = cursor.selection.getMin();
            TextPosition max = cursor.selection.getMax();
            if (clampedDest >= min && clampedDest < max) return;
        }
    }

    // Deleting one selection shifts the others, so only a single one is dragged
    if (cursors.size() != 1) return;

    addUndoRecord();

    TextPosition start = cursors[0].selection.getMin();
    TextPosition end = cursors[0].selection.getMax();

    start.line = std::clamp(start.line, 0, static_cast<int>(lines.size()) - 1);
    start.column = std::clamp(start.column, 0, static_cast<int>(lines[start.line].size()));
    end.line = std::clamp(end.line, 0, static_cast<int>(lines.size()) - 1);
    end.column = std::clamp(end.column, 0, static_cast<int>(lines[end.line].size()));

    TextPosition insertPos = clampedDest;

    deleteRange(start, end);

    // The removed range shifts a destination that came after it
    if (insertPos > start) {
        if (insertPos.line != end.line) {
            insertPos.line -= (end.line - start.line);
        } else if (start.line == end.line) {
            insertPos.column -= (end.column - start.column);
        } else {
            // The tail of the end line is now appended to the start line
            insertPos.line = start.line;
            insertPos.column = start.column + (insertPos.column - end.column);
        }
    }

    insertPos.line = std::clamp(insertPos.line, 0, static_cast<int>(lines.size()) - 1);
    insertPos.column = std::clamp(insertPos.column, 0, static_cast<int>(lines[insertPos.line].size()));

    cursors.clear();
    Cursor cursor;
    cursor.position = insertPos;
    cursor.selection.start = insertPos;
    cursor.selection.end = insertPos;
    cursors.push_back(cursor);
    primaryCursor = 0;

    insertTextAtCursor(cursors[0], text, false);

    cursors[0].selection.start = insertPos;
    cursors[0].selection.end = cursors[0].position;

    tokenizeAll();
    finalizeUndoRecord();
}

std::vector<size_t> CustomTextEditor::cursorEditOrder() const {
    std::vector<size_t> order(cursors.size());
    std::iota(order.begin(), order.end(), 0);

    std::sort(order.begin(), order.end(), [this](size_t a, size_t b) {
        auto editStart = [](const Cursor& cursor) {
            return cursor.selection.isEmpty() ? cursor.position : cursor.selection.getMin();
        };
        return editStart(cursors[a]) < editStart(cursors[b]);
    });

    return order;
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
    while (result.column > 0 && result.column < static_cast<int>(lines[result.line].size()) &&
           isUtf8Continuation(static_cast<unsigned char>(lines[result.line][result.column]))) {
        --result.column;
    }
    return result;
}

void CustomTextEditor::moveCursor(Cursor& cursor, int deltaLine, int deltaCol, bool shift) {
    TextPosition newPos = cursor.position;

    if (deltaLine != 0) {
        int preferredVisualColumn = cursor.preferredColumn;
        if (preferredVisualColumn < 0) {
            preferredVisualColumn = byteOffsetToVisualColumn(newPos.line, newPos.column);
        }

        newPos.line = std::clamp(newPos.line + deltaLine, 0, static_cast<int>(lines.size()) - 1);

        cursor.preferredColumn = preferredVisualColumn;
        newPos.column = visualColumnToByteOffset(newPos.line, preferredVisualColumn);
    }

    if (deltaCol != 0) {
        cursor.preferredColumn = -1;

        if (deltaCol < 0) {
            for (int i = 0; i < -deltaCol; ++i) {
                if (newPos.column > 0) {
                    newPos.column = prevUtf8Column(newPos.line, newPos.column);
                } else if (newPos.line > 0) {
                    --newPos.line;
                    newPos.column = static_cast<int>(lines[newPos.line].size());
                }
            }
        } else {
            for (int i = 0; i < deltaCol; ++i) {
                if (newPos.column < static_cast<int>(lines[newPos.line].size())) {
                    newPos.column = nextUtf8Column(newPos.line, newPos.column);
                } else if (newPos.line < static_cast<int>(lines.size()) - 1) {
                    ++newPos.line;
                    newPos.column = 0;
                }
            }
        }

        newPos = clampPosition(newPos);
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
    while (firstNonSpace < static_cast<int>(line.size()) && std::isspace(static_cast<unsigned char>(line[firstNonSpace]))) {
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
    unsigned char value = static_cast<unsigned char>(c);
    return std::isalnum(value) || c == '_' || value >= 0x80;
}

int CustomTextEditor::prevUtf8Column(int lineIndex, int column) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0;
    const std::string& line = lines[lineIndex];
    int result = std::clamp(column, 0, static_cast<int>(line.size()));
    if (result <= 0) return 0;
    --result;
    while (result > 0 && isUtf8Continuation(static_cast<unsigned char>(line[result]))) {
        --result;
    }
    return result;
}

int CustomTextEditor::nextUtf8Column(int lineIndex, int column) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0;
    const std::string& line = lines[lineIndex];
    int result = std::clamp(column, 0, static_cast<int>(line.size()));
    if (result >= static_cast<int>(line.size())) return static_cast<int>(line.size());
    ++result;
    while (result < static_cast<int>(line.size()) &&
           isUtf8Continuation(static_cast<unsigned char>(line[result]))) {
        ++result;
    }
    return result;
}

int CustomTextEditor::byteOffsetToVisualColumn(int lineIndex, int byteOffset) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0;
    const std::string& line = lines[lineIndex];
    int clampedOffset = std::clamp(byteOffset, 0, static_cast<int>(line.size()));
    int visualColumn = 0;
    for (int i = 0; i < clampedOffset; ++i) {
        if (!isUtf8Continuation(static_cast<unsigned char>(line[i]))) {
            ++visualColumn;
        }
    }
    return visualColumn;
}

int CustomTextEditor::visualColumnToByteOffset(int lineIndex, int visualColumn) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0;
    const std::string& line = lines[lineIndex];
    int targetColumn = std::max(0, visualColumn);
    int currentColumn = 0;
    for (int i = 0; i < static_cast<int>(line.size()); i = nextUtf8Column(lineIndex, i)) {
        if (currentColumn >= targetColumn) return i;
        ++currentColumn;
    }
    return static_cast<int>(line.size());
}

float CustomTextEditor::measureText(const char* begin, const char* end) const {
    ImFont* font = measureFont ? measureFont : ImGui::GetFont();
    float fontSize = measureFont ? measureFontSize : ImGui::GetFontSize();
    return font->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, begin, end).x;
}

float CustomTextEditor::byteOffsetToPixelX(int lineIndex, int byteOffset) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0.0f;
    const std::string& line = lines[lineIndex];
    int clampedOffset = std::clamp(byteOffset, 0, static_cast<int>(line.size()));
    while (clampedOffset > 0 &&
           clampedOffset < static_cast<int>(line.size()) &&
           isUtf8Continuation(static_cast<unsigned char>(line[clampedOffset]))) {
        --clampedOffset;
    }
    return measureText(line.c_str(), line.c_str() + clampedOffset);
}

int CustomTextEditor::pixelXToByteOffset(int lineIndex, float pixelX) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return 0;
    const std::string& line = lines[lineIndex];
    if (pixelX <= 0.0f || line.empty()) return 0;

    int previous = 0;
    float previousX = 0.0f;
    for (int current = nextUtf8Column(lineIndex, 0);
         current <= static_cast<int>(line.size());
         current = nextUtf8Column(lineIndex, current)) {
        float currentX = previousX + measureText(line.c_str() + previous, line.c_str() + current);
        if (pixelX < (previousX + currentX) * 0.5f) {
            return previous;
        }
        if (current == static_cast<int>(line.size())) {
            return current;
        }
        previous = current;
        previousX = currentX;
    }

    return static_cast<int>(line.size());
}

float CustomTextEditor::computeMaxLineWidth() const {
    float widest = 0.0f;
    for (const auto& line : lines) {
        float lineWidth = measureText(line.c_str(), line.c_str() + line.size());
        if (lineWidth > widest) widest = lineWidth;
    }
    return widest;
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

TextPosition CustomTextEditor::findDeleteWordStart(const TextPosition& pos) const {
    if (pos.column == 0) {
        if (pos.line == 0) return pos;
        return TextPosition(pos.line - 1, static_cast<int>(lines[pos.line - 1].size()));
    }

    const std::string& line = lines[pos.line];
    TextPosition result = pos;

    // A whitespace run goes as a whole, so one press clears an indentation
    while (result.column > 0 && std::isspace(static_cast<unsigned char>(line[result.column - 1]))) {
        --result.column;
    }
    if (pos.column - result.column > 1) return result;

    if (result.column > 0 && !isWordChar(line[result.column - 1])) {
        --result.column;
        return result;
    }

    return findWordStart(result);
}

TextPosition CustomTextEditor::findDeleteWordEnd(const TextPosition& pos) const {
    const std::string& line = lines[pos.line];
    const int lineSize = static_cast<int>(line.size());

    if (pos.column == lineSize) {
        if (pos.line == static_cast<int>(lines.size()) - 1) return pos;
        return TextPosition(pos.line + 1, 0);
    }

    TextPosition result = pos;

    while (result.column < lineSize && std::isspace(static_cast<unsigned char>(line[result.column]))) {
        ++result.column;
    }
    if (result.column - pos.column > 1) return result;

    if (result.column < lineSize && !isWordChar(line[result.column])) {
        ++result.column;
        return result;
    }

    return findWordEnd(result);
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

    // [const] Type [*&] varName
    std::regex cppDeclRegex("(?:const\\s+)?([A-Za-z0-9_:]+)(?:<[^>]*>)?\\s*(?:[*&]+\\s*)?" + varName + "\\b");
    // auto varName = new Type(...)
    std::regex cppNewRegex("\\bauto\\s*[*&]*\\s*" + varName + "\\s*=\\s*new\\s+([A-Za-z0-9_:]+)");
    // auto varName = Type(...) or Type{...}
    std::regex cppAutoCtorRegex("\\bauto\\s*[*&]*\\s*" + varName + "\\s*=\\s*([A-Z][A-Za-z0-9_:]*)\\s*[({]");

    std::regex luaVarRegex("\\b" + varName + "\\s*=\\s*([A-Za-z0-9_\\.:]+)(?:\\(|\\{)");
    std::regex luaLocalVarRegex("\\blocal\\s+" + varName + "\\s*=\\s*([A-Za-z0-9_\\.:]+)(?:\\(|\\{)");

    for (int i = currentLine; i >= 0; --i) {
        if (i >= static_cast<int>(lines.size())) continue;
        const std::string& line = lines[i];
        // Every pattern needs varName, so lines without it can't match any of them
        if (line.find(varName) == std::string::npos) continue;
        std::smatch match;

        if (language == SyntaxLanguage::Cpp) {
            // Most specific first
            if (std::regex_search(line, match, cppNewRegex)) {
                return match[1].str();
            }
            if (std::regex_search(line, match, cppAutoCtorRegex)) {
                return match[1].str();
            }
            if (std::regex_search(line, match, cppDeclRegex)) {
                std::string typeMatch = match[1].str();
                if (typeMatch != "return" && typeMatch != "new" && typeMatch != "delete" && typeMatch != "auto") {
                    return typeMatch;
                }
            }
        } else if (language == SyntaxLanguage::Lua) {
            if (std::regex_search(line, match, luaLocalVarRegex) ||
                std::regex_search(line, match, luaVarRegex)) {
                std::string typeName = match[1].str();
                // Constructor-style call: "local p = Player.new()" / "Player.create()" -> Player
                size_t sep = typeName.find_first_of(".:");
                if (sep != std::string::npos) {
                    std::string suffix = typeName.substr(sep + 1);
                    if (suffix == "new" || suffix == "create") {
                        typeName = typeName.substr(0, sep);
                    }
                }
                return typeName;
            }
        }
    }
    return "";
}

std::string CustomTextEditor::inferTypeBefore(int lineIndex, int endColumn) const {
    if (lineIndex < 0 || lineIndex >= static_cast<int>(lines.size())) return "";

    std::string name = wordEndingAt(lines[lineIndex], endColumn);
    if (name.empty()) return "";

    // A class or enum name used directly is a static access (Engine.setScene, Scaling.NATIVE)
    if (suggestions && suggestions->IsKnownClassOrEnum(name)) return name;

    std::string type = inferTypeOfVariable(name, lineIndex);
    if (type.empty() && suggestions) {
        type = suggestions->FindSymbolType(name);
    }

    // Engine API parent types are stored without the namespace
    size_t lastColon = type.rfind(':');
    return (lastColon != std::string::npos) ? type.substr(lastColon + 1) : type;
}

SuggestionContext CustomTextEditor::buildSuggestionContext() const {
    SuggestionContext ctx;

    if (cursors.empty()) return ctx;

    ctx.isCpp = (language == SyntaxLanguage::Cpp);

    const TextPosition& pos = cursors[primaryCursor].position;

    TextPosition wordStart = findWordStart(pos);
    ctx.currentWord = getRange(wordStart, pos);

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

    // The word before the accessor is what member completion resolves against
    if (checkCol > 0) {
        TextPosition prevEnd(pos.line, checkCol - 1);
        while (prevEnd.column > 0 && std::isspace(static_cast<unsigned char>(ctx.lineContent[prevEnd.column]))) {
            prevEnd.column--;
        }
        if (prevEnd.column >= 0 && (std::isalnum(static_cast<unsigned char>(ctx.lineContent[prevEnd.column])) || ctx.lineContent[prevEnd.column] == '_')) {
            TextPosition prevStart = findWordStart(TextPosition(pos.line, prevEnd.column + 1));
            // getRange uses end column generically as exclusive bound, so we use prevEnd.column+1
            ctx.previousWord = getRange(prevStart, TextPosition(pos.line, prevEnd.column + 1));
        }
    }

    if ((ctx.afterDot || ctx.afterArrow || ctx.afterDoubleColon || ctx.afterColon) && !ctx.previousWord.empty()) {
        // After '::' the previous word already is the type name (Vector3::ZERO)
        ctx.targetType = ctx.afterDoubleColon ? ctx.previousWord : inferTypeBefore(pos.line, checkCol - 1);
    }

    return ctx;
}

void CustomTextEditor::TriggerAutoComplete(bool manualInvoke) {
    if (!autoComplete || readOnly || !suggestions) return;

    autoCompleteAnchor = cursors[primaryCursor].position;
    updateSuggestions(manualInvoke);
    showAutoComplete = !currentSuggestions.empty();
    suggestionIndex = 0;
    scrollToSuggestion = true; // scroll back to top when the popup opens or refilters
}

void CustomTextEditor::CloseAutoComplete() {
    showAutoComplete = false;
    currentSuggestions.clear();
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
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(funcLine[nameEnd - 1]))) nameEnd--;
    int nameStart = nameEnd;
    while (nameStart > 0 && (std::isalnum(static_cast<unsigned char>(funcLine[nameStart - 1])) || funcLine[nameStart - 1] == '_')) nameStart--;
    std::string funcName = funcLine.substr(nameStart, nameEnd - nameStart);

    if (funcName.empty()) {
        closeParamHint();
        return;
    }

    // The accessor before the name gives the parent type
    std::string parentType;
    int beforeFunc = nameStart - 1;
    while (beforeFunc >= 0 && std::isspace(static_cast<unsigned char>(funcLine[beforeFunc]))) beforeFunc--;

    if (beforeFunc >= 0) {
        char accessor = funcLine[beforeFunc];
        if (accessor == ':' && beforeFunc > 0 && funcLine[beforeFunc - 1] == ':') {
            // Static call, the name before '::' is the type itself
            parentType = wordEndingAt(funcLine, beforeFunc - 2);
        } else if (accessor == '.' || accessor == ':') {
            parentType = inferTypeBefore(searchLine, beforeFunc - 1);
        } else if (accessor == '>' && beforeFunc > 0 && funcLine[beforeFunc - 1] == '-') {
            parentType = inferTypeBefore(searchLine, beforeFunc - 2);
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
    while (nameEnd > 0 && std::isspace(static_cast<unsigned char>(anchorLine[nameEnd - 1]))) nameEnd--;
    int nameStart = nameEnd;
    while (nameStart > 0 && (std::isalnum(static_cast<unsigned char>(anchorLine[nameStart - 1])) || anchorLine[nameStart - 1] == '_')) nameStart--;
    std::string currentFuncName = anchorLine.substr(nameStart, nameEnd - nameStart);

    if (currentFuncName != paramHintFuncName) {
        closeParamHint();
        return;
    }

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

            if (inString) {
                if (ch == stringQuote) {
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

    for (auto& p : params) {
        size_t start = p.find_first_not_of(' ');
        size_t end = p.find_last_not_of(' ');
        if (start != std::string::npos) p = p.substr(start, end - start + 1);
    }

    // Position above the '(' anchor
    ImVec2 screenPos = textToScreen(paramHintAnchor, origin);
    screenPos.y -= lineHeight + 4.0f;

    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    std::string fullText = funcPrefix;
    for (size_t i = 0; i < params.size(); i++) {
        if (i > 0) fullText += ", ";
        fullText += params[i];
    }
    fullText += ")";

    std::string overloadText;
    if (paramHintSignatures.size() > 1) {
        overloadText = std::to_string(paramHintOverloadIndex + 1) + "/" + std::to_string(paramHintSignatures.size());
    }

    float textWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0, fullText.c_str()).x;
    float overloadWidth = overloadText.empty() ? 0 : font->CalcTextSizeA(fontSize, FLT_MAX, 0, overloadText.c_str()).x + 16.0f;
    float padding = Theme::dpi(8.0f);
    float popupWidth = textWidth + overloadWidth + padding * 2;
    float popupHeight = lineHeight + padding;

    // Ensure popup doesn't go off-screen
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (screenPos.x + popupWidth > displaySize.x) {
        screenPos.x = displaySize.x - popupWidth - Theme::dpi(10.0f);
    }
    if (screenPos.x < 0) screenPos.x = Theme::dpi(10.0f);
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::dpi(4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));

    if (ImGui::Begin("##ParamHint", nullptr, flags)) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 textPos = ImGui::GetCursorScreenPos();

        ImU32 normalColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
        ImU32 activeColor = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImU32 activeUnderline = ImGui::ColorConvertFloat4ToU32(ImVec4(0.35f, 0.65f, 0.95f, 1.0f));

        float x = textPos.x;

        drawList->AddText(font, fontSize, ImVec2(x, textPos.y), normalColor, funcPrefix.c_str());
        x += font->CalcTextSizeA(fontSize, FLT_MAX, 0, funcPrefix.c_str()).x;

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
                float underlineY = textPos.y + fontSize + 1.0f;
                drawList->AddLine(ImVec2(x, underlineY), ImVec2(x + paramWidth, underlineY), activeUnderline, 2.0f);
            }

            x += paramWidth;
        }

        drawList->AddText(font, fontSize, ImVec2(x, textPos.y), normalColor, ")");
        x += font->CalcTextSizeA(fontSize, FLT_MAX, 0, ")").x;

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

    if (language == SyntaxLanguage::Lua || language == SyntaxLanguage::Cpp) {
        addEngineAPISuggestions();
    }

    // Engine API symbols are authoritative (real signatures). Engine headers may live
    // inside the project tree, so the project scanner can produce duplicates of the
    // same members with a generic "project function" detail — skip those.
    static const std::unordered_set<std::string> engineKeys = [] {
        std::unordered_set<std::string> keys;
        for (const auto& sym : getEngineAPISymbols()) {
            if (isEngineApiConstructorSymbol(sym)) continue;
            keys.insert(std::string(sym.name) + "\x1f" + (sym.parent ? sym.parent : ""));
        }
        return keys;
    }();

    for (const auto& sym : symbols) {
        if (engineKeys.find(sym.name + "\x1f" + sym.parentType) != engineKeys.end()) continue;
        suggestions->AddSymbol(sym.name, sym.kind, sym.detail, sym.parentType, sym.typeInfo);
    }
}

bool CustomTextEditor::isInCommentOrString(const TextPosition& pos) const {
    if (pos.line < 0 || pos.line >= static_cast<int>(lineTokens.size())) return false;

    // Look at the character just typed (left of the cursor)
    int col = pos.column > 0 ? pos.column - 1 : 0;
    for (const auto& token : lineTokens[pos.line]) {
        if (col >= token.start && col < token.start + token.length) {
            return token.type == TokenType::Comment ||
                   token.type == TokenType::MultiLineComment ||
                   token.type == TokenType::String;
        }
    }
    return false;
}

void CustomTextEditor::updateSuggestions(bool manualInvoke) {
    if (!suggestions || cursors.empty()) return;

    if (isInCommentOrString(cursors[primaryCursor].position)) {
        currentSuggestions.clear();
        showAutoComplete = false;
        return;
    }

    suggestions->UpdateDocumentWords(lines);

    SuggestionContext ctx = buildSuggestionContext();
    ctx.manualInvoke = manualInvoke;
    autoCompleteAnchor = findWordStart(cursors[primaryCursor].position);

    currentSuggestions = suggestions->GetSuggestions(ctx);

    // Close the popup if filtering (e.g. backspace) left nothing to show
    if (showAutoComplete && currentSuggestions.empty()) {
        showAutoComplete = false;
    }

    // Every refilter re-ranks the list, so the selection snaps back to the best
    // match at the top, like VSCode does on each keystroke
    if (!currentSuggestions.empty()) {
        suggestionIndex = 0;
        scrollToSuggestion = true;
    }
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

    for (size_t i = 0; i < cursors.size(); ++i) {
        if (static_cast<int>(i) != primaryCursor) {
            adjustCursorAfterDelete(cursors[i], delStart, delEnd);
        }
    }

    // Snippet placeholders are reduced to their default text, ${n:text} -> text
    std::string textToInsert = item.insertText;
    std::string processed;
    size_t i = 0;
    while (i < textToInsert.size()) {
        if (textToInsert[i] == '$' && i + 1 < textToInsert.size() && textToInsert[i + 1] == '{') {
            size_t start = i + 2;
            size_t end = textToInsert.find('}', start);
            if (end != std::string::npos) {
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

    for (size_t i = 0; i < cursors.size(); ++i) {
        if (static_cast<int>(i) != primaryCursor) {
            adjustCursorAfterInsert(cursors[i], insertPos, afterPos);
        }
    }

    mergeCursors();
    tokenizeAll();
    finalizeUndoRecord();
    CloseAutoComplete();
}

TextPosition CustomTextEditor::screenToText(const ImVec2& screenPos, const ImVec2& origin) const {
    float y = screenPos.y - origin.y;
    float x = screenPos.x - origin.x - textStartX;

    int line = std::clamp(static_cast<int>(y / lineHeight), 0, static_cast<int>(lines.size()) - 1);
    int column = pixelXToByteOffset(line, x);

    return TextPosition(line, column);
}

ImVec2 CustomTextEditor::textToScreen(const TextPosition& pos, const ImVec2& origin) const {
    float x = origin.x + textStartX + byteOffsetToPixelX(pos.line, pos.column);
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

    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd))) {
        if (onFontZoom) onFontZoom(1);
    }

    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract))) {
        if (onFontZoom) onFontZoom(-1);
    }

    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_0) || ImGui::IsKeyPressed(ImGuiKey_Keypad0))) {
        if (onFontZoom) onFontZoom(0);
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
                    int visualCol = byteOffsetToVisualColumn(pos.line, pos.column);
                    AddCursor(pos.line - 1, visualColumnToByteOffset(pos.line - 1, visualCol));
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
                    int visualCol = byteOffsetToVisualColumn(pos.line, pos.column);
                    AddCursor(pos.line + 1, visualColumnToByteOffset(pos.line + 1, visualCol));
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

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace) && !readOnly) {
        if (ctrl) {
            DeleteWordLeft();
        } else {
            Backspace();
        }
        scrollToCursor();
        if (showAutoComplete) updateSuggestions();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !readOnly) {
        if (ctrl) {
            DeleteWordRight();
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

                        // Break the brackets open: an indented line for the content
                        // and one for the closing bracket
                        InsertText("\n", true);
                        InsertText("\n", false);

                        int closingBracketLine = cursors[0].position.line;
                        int parentLine = closingBracketLine - 2;

                        if (parentLine >= 0) {
                            SetCursorPosition(closingBracketLine, 0);
                            InsertText(getIndentString(getLineIndent(parentLine)), false);

                            int contentLine = closingBracketLine - 1;
                            SetCursorPosition(contentLine, static_cast<int>(lines[contentLine].size()));
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
                }
            } else {
                InsertText(std::string(tabSize, ' '));
            }
        }
    }

    // Trigger auto-complete with Ctrl+Space (manual invoke shows the full list)
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Space)) {
        TriggerAutoComplete(true);
    }

    if (showParamHint) {
        updateParamHint();
    }
}

void CustomTextEditor::handleMouseInput() {
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    ImVec2 contentPos = ImGui::GetCursorScreenPos();

    bool ctrl = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt = io.KeyAlt;

    // Ctrl+mouse wheel zooms the font (ImGui skips scrolling while Ctrl is held)
    if (ctrl && io.MouseWheel != 0.0f) {
        if (onFontZoom) onFontZoom(io.MouseWheel > 0.0f ? 1 : -1);
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        showContextMenu = false;
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

                    int anchorVisual = byteOffsetToVisualColumn(anchor.line, anchor.column);
                    int clickVisual = byteOffsetToVisualColumn(clickPos.line, clickPos.column);

                    for (int l = minLine; l <= maxLine; ++l) {
                        Cursor cursor;
                        cursor.position = TextPosition(l, visualColumnToByteOffset(l, clickVisual));
                        cursor.selection.start = TextPosition(l, visualColumnToByteOffset(l, anchorVisual));
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

    if (showParamHint && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        updateParamHint();
    }

    if (!suggestionsHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        placeCursorAtClick(screenToText(mousePos, contentPos));
        CloseAutoComplete();
        showContextMenu = true;
        contextMenuPos = mousePos;
    }
}

void CustomTextEditor::handleTextInput() {
    if (readOnly) return;

    ImGuiIO& io = ImGui::GetIO();

    if (io.InputQueueCharacters.Size > 0) {
        for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
            ImWchar c = io.InputQueueCharacters[i];

            if (c < 32 && c != '\t' && c != '\n') continue;
            if (c == 127 || (c >= 0x80 && c <= 0x9F)) continue; // DEL and C1 controls

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

                    // The cursors go back between the brackets after the closing one is typed
                    auto positions = cursors;
                    InsertText(std::string(1, closing), false);
                    for (size_t j = 0; j < cursors.size() && j < positions.size(); ++j) {
                        cursors[j] = positions[j];
                    }

                    if (c == '(') {
                        triggerParamHint();
                    }
                } else if (showParamHint) {
                    updateParamHint();
                }

                bool shouldTrigger = false;
                if (autoComplete) {
                    if ((c < 128 && std::isalnum(static_cast<unsigned char>(c))) || c == '_' || c == '.' || c == ':') {
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
                    CloseAutoComplete();
                }
            }
        }

        scrollToCursor();
    }
}

void CustomTextEditor::renderLineNumbers(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine) {
    if (!showLineNumbers) return;

    ImU32 color = ImGui::ColorConvertFloat4ToU32(lineNumberColor);

    for (int i = startLine; i <= endLine && i < static_cast<int>(lines.size()); ++i) {
        float y = origin.y + i * lineHeight;

        char lineNum[16];
        snprintf(lineNum, sizeof(lineNum), "%*d", lineNumberDigits, i + 1);

        drawList->AddText(ImVec2(origin.x + leftMargin, y + textOffsetY), color, lineNum);
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

            float x1 = origin.x + textStartX + byteOffsetToPixelX(line, startCol);
            float x2 = origin.x + textStartX + byteOffsetToPixelX(line, endCol);

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

        int endColumn = result.column + static_cast<int>(searchText.size());
        float x = origin.x + textStartX + byteOffsetToPixelX(result.line, result.column);
        float y = origin.y + result.line * lineHeight;
        float width = byteOffsetToPixelX(result.line, endColumn) -
                      byteOffsetToPixelX(result.line, result.column);

        drawList->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + lineHeight), highlightColor);
    }
}

void CustomTextEditor::renderText(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine) {
    static const std::vector<Token> noTokens;

    for (int i = startLine; i <= endLine && i < static_cast<int>(lines.size()); ++i) {
        float y = origin.y + i * lineHeight + textOffsetY;
        float x = origin.x + textStartX;

        const std::string& line = lines[i];
        const auto& tokens = (i < static_cast<int>(lineTokens.size())) ? lineTokens[i] : noTokens;

        if (tokens.empty()) {
            ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(TokenType::Default)]);
            drawList->AddText(ImVec2(x, y), color, line.c_str());
        } else {
            int lastEnd = 0;
            for (const auto& token : tokens) {
                if (token.start > lastEnd) {
                    std::string gap = line.substr(lastEnd, token.start - lastEnd);
                    ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(TokenType::Default)]);
                    drawList->AddText(ImVec2(x, y), color, gap.c_str());
                    x += measureText(line.c_str() + lastEnd, line.c_str() + token.start);
                }

                std::string tokenText = line.substr(token.start, token.length);
                ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(token.type)]);
                drawList->AddText(ImVec2(x, y), color, tokenText.c_str());

                lastEnd = token.start + token.length;
                x += measureText(line.c_str() + token.start, line.c_str() + lastEnd);
            }

            if (lastEnd < static_cast<int>(line.size())) {
                std::string remaining = line.substr(lastEnd);
                ImU32 color = ImGui::ColorConvertFloat4ToU32(palette[static_cast<int>(TokenType::Default)]);
                drawList->AddText(ImVec2(x, y), color, remaining.c_str());
            }
        }
    }
}

void CustomTextEditor::renderCursors(ImDrawList* drawList, const ImVec2& origin) {
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;

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
    screenPos.y += lineHeight + 1.0f;

    float popupWidth = 350.0f;
    float itemHeight = ImGui::GetTextLineHeight() + 4.0f;
    float maxVisibleItems = 10.0f;
    float popupMaxHeight = itemHeight * maxVisibleItems + 8.0f;
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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(4.0f, 4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Theme::dpi(ImVec2(4.0f, 0.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::dpi(4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, Theme::dpi(10.0f));
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

        // Applying clears currentSuggestions, so it waits until the list is done being read
        int clickedIndex = -1;

        for (int i = 0; i < static_cast<int>(currentSuggestions.size()); ++i) {
            const auto& item = currentSuggestions[i];

            bool selected = (i == suggestionIndex);

            ImGui::PushID(i);

            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float availWidth = ImGui::GetContentRegionAvail().x;

            // VSCode-like dark blue selection color
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.016f, 0.224f, 0.369f, 1.0f)); // #04395e
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.17f, 0.27f, 0.44f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.016f, 0.224f, 0.369f, 1.0f));

            if (ImGui::Selectable("##item", selected, ImGuiSelectableFlags_None, ImVec2(availWidth, itemHeight))) {
                clickedIndex = i;
            }

            ImGui::PopStyleColor(3);

            // Scroll to selected item only if navigation occurred
            if (selected && scrollToSuggestion) {
                ImGui::SetScrollHereY();
            }

            ImVec2 nextItemPos = ImGui::GetCursorPos();

            // Draw content on top of selectable - center vertically
            float textY = cursorPos.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(cursorPos.x + 4.0f, textY));

            ImVec4 iconColor = SemanticSuggestions::GetKindColor(item.kind);
            const char* icon = SemanticSuggestions::GetKindIcon(item.kind);
            ImGui::TextColored(iconColor, "%s", icon);
            ImGui::SameLine();

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

            ImGui::SetCursorPos(nextItemPos);

            ImGui::PopID();

            if (ImGui::IsItemHovered() && !item.documentation.empty()) {
                ImGui::SetTooltip("%s", item.documentation.c_str());
            }
        }

        if (clickedIndex >= 0) {
            suggestionIndex = clickedIndex;
            applySuggestion();
        }

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

void CustomTextEditor::renderFindDialog(const ImVec2& editorPos, const ImVec2& editorSize) {
    if (!showFindDialog) return;

    const float padding = Theme::dpi(8.0f);
    const float spacing = Theme::dpi(4.0f);

    ImVec2 dialogPos(editorPos.x + editorSize.x - Theme::dpi(15.0f), editorPos.y + padding);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(8.0f, 6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, Theme::dpi(4.0f)));

    // Calculate dialog size first, then position it from the right edge
    ImGui::SetNextWindowPos(dialogPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f)); // Pivot at right edge

    if (ImGui::Begin("##FindDialog", nullptr, flags)) {

        const float inputWidth = Theme::dpi(180.0f);
        const float controlHeight = ImGui::GetFrameHeight();

        bool focusInput = ImGui::IsWindowAppearing();

        // Toggle replace
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));

        // Align arrow vertically with the input box
        const float chevronSize = Theme::dpi(16.0f);
        float currentY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(currentY + (controlHeight - chevronSize) * 0.5f);

        if (ImGui::Button(showReplaceInput ? ICON_FA_CHEVRON_DOWN "##ToggleReplace" : ICON_FA_CHEVRON_RIGHT "##ToggleReplace", ImVec2(chevronSize, chevronSize))) {
            showReplaceInput = !showReplaceInput;
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle Replace");

        ImGui::SetCursorPosY(currentY); // Restore Y position
        ImGui::SameLine();

        if (findRefocusInput) {
            ImGui::SetKeyboardFocusHere();
            findRefocusInput = false;
        } else if (focusInput) {
            ImGui::SetKeyboardFocusHere();
        }

        float findInputStartX = ImGui::GetCursorPosX();
        ImGui::BeginGroup();

        // Use UIUtils::searchInput for consistent look with Output window
        if (UIUtils::searchInput("##FindInput", "Find...", findInputBuffer, sizeof(findInputBuffer), false, &findCaseSensitive, inputWidth)) {
            if (strcmp(searchText.c_str(), findInputBuffer) != 0) {
                SetSearchText(findInputBuffer);
            }
        }

        bool enterPressed = ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter);
        bool shiftEnterPressed = ImGui::IsWindowFocused() && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter);

        ImGui::EndGroup();

        // Check if case sensitivity changed via the search input popup
        if (findLastCaseSensitive != findCaseSensitive) {
            updateSearchResults();
            findLastCaseSensitive = findCaseSensitive;
        }

        if (shiftEnterPressed) {
            FindPrevious();
            findRefocusInput = true;
        } else if (enterPressed) {
            FindNext();
            findRefocusInput = true;
        }

        ImGui::SameLine();

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

        if (ImGui::Button(ICON_FA_CHEVRON_UP "##Prev") || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_UpArrow))) {
            FindPrevious();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous (Shift+Enter)");

        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_CHEVRON_DOWN "##Next") || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_DownArrow))) {
            FindNext();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next (Enter)");

        ImGui::SameLine(0.0f, 15.0f);

        if (ImGui::Button(ICON_FA_XMARK "##Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            CloseFind();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close (Escape)");

        if (showReplaceInput) {
            ImGui::SetCursorPosX(findInputStartX);

            ImGui::SetNextItemWidth(inputWidth);
            ImGui::InputTextWithHint("##ReplaceInput", "Replace", replaceInputBuffer, sizeof(replaceInputBuffer));
            bool replaceInputFocused = ImGui::IsItemFocused();
            InputTextContextMenu::drawForLastItem(replaceInputBuffer, sizeof(replaceInputBuffer));

            bool replaceEnter = replaceInputFocused && ImGui::IsKeyPressed(ImGuiKey_Enter);
            bool replaceShiftEnter = replaceInputFocused && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Enter);

            ImGui::SameLine();

            if (ImGui::Button("Replace") || (replaceEnter && !replaceShiftEnter)) {
                ReplaceNext();
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

void CustomTextEditor::placeCursorAtClick(const TextPosition& clickPos) {
    for (const auto& cursor : cursors) {
        if (!cursor.selection.isEmpty()) {
            TextPosition min = cursor.selection.getMin();
            TextPosition max = cursor.selection.getMax();
            if (clickPos >= min && clickPos < max) {
                return;
            }
        }
    }

    cursors.clear();
    Cursor cursor;
    cursor.position = clickPos;
    cursor.selection.start = clickPos;
    cursor.selection.end = clickPos;
    cursors.push_back(cursor);
    primaryCursor = 0;
}

void CustomTextEditor::renderContextMenu() {
    if (!showContextMenu) {
        return;
    }

    ImGui::SetNextWindowPos(contextMenuPos, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::PushID(this);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(4.0f, 4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Theme::dpi(ImVec2(8.0f, 6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::dpi(4.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));

    if (ImGui::Begin("##ContextMenu", nullptr, flags)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            showContextMenu = false;
        } else if (!ImGui::IsWindowHovered() &&
                   (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
            showContextMenu = false;
        } else {
            const bool canUseClipboard = ImGui::GetClipboardText() != nullptr;
            const bool canEdit = !readOnly;
            const bool hasLine = !lines.empty();

            auto menuItem = [&](const char* label, const char* shortcut, bool enabled, auto action) {
                if (ImGui::MenuItem(label, shortcut, false, enabled)) {
                    action();
                    showContextMenu = false;
                }
            };

            menuItem(ICON_FA_ROTATE_LEFT " Undo", "Ctrl+Z", canEdit && CanUndo(), [&] { Undo(); });
            menuItem(ICON_FA_ROTATE_RIGHT " Redo", "Ctrl+Y", canEdit && CanRedo(), [&] { Redo(); });
            ImGui::Separator();
            menuItem(ICON_FA_SCISSORS " Cut", "Ctrl+X", canEdit && hasLine, [&] { Cut(); });
            menuItem(ICON_FA_COPY " Copy", "Ctrl+C", hasLine, [&] { Copy(); });
            menuItem(ICON_FA_PASTE " Paste", "Ctrl+V", canEdit && canUseClipboard, [&] { Paste(); });
            ImGui::Separator();
            menuItem(ICON_FA_I_CURSOR " Select All", "Ctrl+A", hasLine, [&] { SelectAll(); });
            ImGui::Separator();
            menuItem(ICON_FA_MAGNIFYING_GLASS " Find", "Ctrl+F", true, [&] { OpenFind(); });
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    ImGui::PopID();
}

void CustomTextEditor::Render(const char* title, const ImVec2& size, bool border) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, backgroundColor);

    ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar | ImGuiWindowFlags_NoMove;

    // Without this, Up/Down in the suggestions popup would also scroll the editor
    flags |= ImGuiWindowFlags_NoNavInputs;

    ImVec2 contentSize = size;
    if (contentSize.x == 0) contentSize.x = ImGui::GetContentRegionAvail().x;
    if (contentSize.y == 0) contentSize.y = ImGui::GetContentRegionAvail().y;

    ImGui::PushID(this);
    if (ImGui::BeginChild(title, contentSize, border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None, flags)) {
        // Focus the child when only the parent is focused (a click on the tab or title bar).
        // A popup is skipped because stealing focus would close it.
        if (!ImGui::IsWindowFocused() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
            ImGui::SetWindowFocus();
        }

        if (pendingFocus) {
            ImGui::SetWindowFocus();
            pendingFocus = false;
        }
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        // Cached widths are in pixels, so a font change expires them
        if (font != measureFont || fontSize != measureFontSize) {
            measureFont = font;
            measureFontSize = fontSize;
            maxLineWidthDirty = true;
        }
        charWidth = font->CalcTextSizeA(fontSize, FLT_MAX, -1.0f, "X").x;
        lineHeight = std::round(fontSize * lineHeightFactor);
        // Center the font's real line box (ascent-descent), which can differ from fontSize
        ImFontBaked* baked = ImGui::GetFontBaked();
        textOffsetY = (lineHeight - (baked->Ascent - baked->Descent)) * 0.5f;

        if (showLineNumbers) {
            lineNumberDigits = 1;
            for (size_t n = lines.size(); n >= 10; n /= 10) lineNumberDigits++;
            lineNumberDigits = std::max(lineNumberDigits, 4);
            lineNumberWidth = lineNumberDigits * charWidth + leftMargin * 2;
        } else {
            lineNumberWidth = 0;
        }

        textStartX = lineNumberWidth + leftMargin;

        if (maxLineWidthDirty) {
            maxLineWidth = computeMaxLineWidth();
            maxLineWidthDirty = false;
        }
        float totalWidth = textStartX + maxLineWidth + 50.0f;
        float totalHeight = lines.size() * lineHeight + lineHeight;

        // The dummy gives the child its scrollable content size
        ImVec2 cursorBackup = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Dummy(ImVec2(totalWidth, totalHeight));
        ImGui::SetCursorPos(cursorBackup);

        // A hovered suggestions popup keeps its own scroll, restoring ours stops the drift
        if (!suggestionsHovered) {
            scrollY = ImGui::GetScrollY();
            scrollX = ImGui::GetScrollX();
        } else {
            ImGui::SetScrollY(scrollY);
            ImGui::SetScrollX(scrollX);
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();

        // Render autocomplete popup first (so it can capture mouse events)
        ImGui::PushFont(ImGui::GetIO().FontDefault);
        renderAutoComplete(origin);
        ImGui::PopFont();

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

            bool overVScrollbar = (mPos.x >= right - scrollbarSize && mPos.x <= right);
            bool overHScrollbar = (mPos.y >= bottom - scrollbarSize && mPos.y <= bottom);

            ImGui::SetMouseCursor((overVScrollbar || overHScrollbar) ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_TextInput);
        }

        int startLine = static_cast<int>(scrollY / lineHeight);
        int endLine = startLine + static_cast<int>(contentSize.y / lineHeight) + 2;

        if (highlightCurrentLine && !cursors.empty()) {
            ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(currentLineColor);
            float y = origin.y + cursors[primaryCursor].position.line * lineHeight;
            drawList->AddRectFilled(
                ImVec2(origin.x, y),
                ImVec2(origin.x + contentSize.x, y + lineHeight),
                lineColor
            );
        }

        renderSearchHighlights(drawList, origin, startLine, endLine);
        renderSelections(drawList, origin, startLine, endLine);
        renderMatchingBrackets(drawList, origin);
        renderLineNumbers(drawList, origin, startLine, endLine);
        renderText(drawList, origin, startLine, endLine);
        renderCursors(drawList, origin);

        ImGui::PushFont(ImGui::GetIO().FontDefault);
        renderFindDialog(ImGui::GetWindowPos(), contentSize);
        ImGui::PopFont();

    }
    ImGui::EndChild();
    ImGui::PopID();

    ImGui::PopStyleColor();

    // Context menu is a top-level window (ImGui popups do not work reliably inside this child).
    ImGui::PushFont(ImGui::GetIO().FontDefault);
    renderContextMenu();
    ImGui::PopFont();
}

} // namespace doriax::editor
