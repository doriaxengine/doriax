// (c) Eduardo Doria
// SPDX-License-Identifier: MIT

#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <chrono>

namespace doriax::editor {

    // Forward declarations
    class SemanticSuggestions;
    struct SuggestionItem;
    struct SuggestionContext;
    enum class SuggestionKind;

    enum class SyntaxLanguage {
        None,
        Cpp,
        Lua,
        CMake
    };

    // Token types for syntax highlighting
    enum class TokenType {
        Default,
        Keyword,
        Type,
        Number,
        String,
        Comment,
        MultiLineComment,
        Preprocessor,
        Identifier,
        Punctuation,
        Operator,
        Function,
        Count
    };

    struct Token {
        int start;
        int length;
        TokenType type;
    };

    struct TextPosition {
        int line;
        int column;

        TextPosition() : line(0), column(0) {}
        TextPosition(int l, int c) : line(l), column(c) {}

        bool operator==(const TextPosition& other) const {
            return line == other.line && column == other.column;
        }
        bool operator!=(const TextPosition& other) const {
            return !(*this == other);
        }
        bool operator<(const TextPosition& other) const {
            if (line != other.line) return line < other.line;
            return column < other.column;
        }
        bool operator<=(const TextPosition& other) const {
            return *this < other || *this == other;
        }
        bool operator>(const TextPosition& other) const {
            return !(*this <= other);
        }
        bool operator>=(const TextPosition& other) const {
            return !(*this < other);
        }
    };

    struct Selection {
        TextPosition start;
        TextPosition end;

        bool isEmpty() const { return start == end; }
        TextPosition getMin() const { return start < end ? start : end; }
        TextPosition getMax() const { return start > end ? start : end; }
    };

    struct Cursor {
        TextPosition position;
        Selection selection;
        int preferredColumn; // For up/down navigation

        Cursor() : preferredColumn(-1) {}
    };

    struct UndoRecord {
        std::string beforeText;
        std::string afterText;
        std::vector<Cursor> beforeCursors;
        std::vector<Cursor> afterCursors;
        std::chrono::steady_clock::time_point timestamp;
        bool isMerged;

        UndoRecord() : isMerged(false) {}
    };

    struct LanguageDefinition {
        std::unordered_set<std::string> keywords;
        std::unordered_set<std::string> types;
        std::unordered_set<std::string> builtinFunctions;
        std::string singleLineComment;
        std::string multiLineCommentStart;
        std::string multiLineCommentEnd;
        std::string preprocessorPrefix;
    };

    class CustomTextEditor {
    public:
        CustomTextEditor();
        ~CustomTextEditor();

        // Text operations
        void SetText(const std::string& text);
        std::string GetText() const;
        int GetLineCount() const { return static_cast<int>(lines.size()); }

        // Language and styling
        void SetLanguage(SyntaxLanguage lang);
        SyntaxLanguage GetLanguage() const { return language; }
        const char* GetLanguageName() const;

        // Cursor and selection
        void SetCursorPosition(int line, int column);
        void GetCursorPosition(int& line, int& column) const;
        bool HasSelection() const;
        std::string GetSelectedText() const;
        void SelectAll();
        void ClearSelection();
        
        // Multi-cursor support
        void AddCursor(int line, int column);
        void ClearExtraCursors();
        int GetCursorCount() const { return static_cast<int>(cursors.size()); }

        // Editing operations
        void InsertText(const std::string& text, bool allowAutoIndent = true);
        void DeleteSelection();
        void Backspace();
        void Delete();
        void DeleteWordLeft();
        void DeleteWordRight();

        // Undo/Redo
        void Undo(int steps = 1);
        void Redo(int steps = 1);
        bool CanUndo() const { return !readOnly && undoIndex > 0; }
        bool CanRedo() const { return !readOnly && undoIndex < static_cast<int>(undoBuffer.size()); }
        int GetUndoIndex() const { return undoIndex; }

        // Clipboard
        void Copy();
        void Cut();
        void Paste();

        // Find and Replace
        void SetSearchText(const std::string& text);
        bool FindNext();
        bool FindPrevious();
        bool ReplaceNext();
        void OpenFind();
        void CloseFind();
        bool IsFindOpen() const { return showFindDialog; }
        int ReplaceAll(const std::string& find, const std::string& replace, bool caseSensitive = true);

        // Editor settings
        void SetReadOnly(bool value) { readOnly = value; }
        bool IsReadOnly() const { return readOnly; }
        void SetTabSize(int size) { tabSize = size; }
        int GetTabSize() const { return tabSize; }
        void SetShowLineNumbers(bool show) { showLineNumbers = show; }
        void SetAutoIndent(bool enable) { autoIndent = enable; }
        void SetHighlightCurrentLine(bool enable) { highlightCurrentLine = enable; }
        void SetMatchBrackets(bool enable) { matchBrackets = enable; }
        void SetLineHeightFactor(float factor) { lineHeightFactor = factor; } // relative to the pushed ImGui font size
        void RequestFocus() { pendingFocus = true; }

        // Auto-complete
        void SetAutoComplete(bool enable) { autoComplete = enable; }
        void TriggerAutoComplete(bool manualInvoke = false);
        void CloseAutoComplete();
        bool IsAutoCompleteOpen() const { return showAutoComplete; }

        struct ProjectSymbol {
            std::string name;
            SuggestionKind kind;
            std::string detail;
            std::string parentType; // Class or type this belongs to
            std::string typeInfo;   // Declared type, e.g. "Mesh" for "Mesh* box4"
        };

        // Project symbols (scanned from sibling files at runtime)
        void UpdateProjectSymbols(const std::vector<ProjectSymbol>& symbols);

        // Rendering
        void Render(const char* title, const ImVec2& size = ImVec2(0, 0), bool border = false);

        // Font zoom requests (Ctrl+= / Ctrl+- / Ctrl+0 / Ctrl+MouseWheel); delta is +1/-1 steps, 0 means reset
        using FontZoomCallback = std::function<void(int)>;
        void SetFontZoomCallback(FontZoomCallback callback) { onFontZoom = callback; }

    private:
        // Text data
        std::vector<std::string> lines;
        std::vector<std::vector<Token>> lineTokens;

        // Cursors and selection
        std::vector<Cursor> cursors;
        int primaryCursor;

        // Undo/Redo
        std::vector<UndoRecord> undoBuffer;
        int undoIndex;
        static const int maxUndoSteps = 1000;

        // Language support
        SyntaxLanguage language;
        LanguageDefinition languageDef;

        // Editor state
        bool readOnly;
        int tabSize;
        bool showLineNumbers;
        bool autoIndent;
        bool highlightCurrentLine;
        bool matchBrackets;
        bool autoComplete;
        bool isDragging;
        bool isDraggingText;
        bool mayDragText;
        std::chrono::steady_clock::time_point lastClickTime;
        TextPosition lastClickPos;
        int clickCount;

        // Scroll state
        float scrollX;
        float scrollY;

        // Auto-complete state
        bool showAutoComplete;
        TextPosition autoCompleteAnchor;
        bool suggestionsHovered;

        // Parameter hint state
        bool showParamHint;
        std::vector<std::string> paramHintSignatures;  // All overload signatures
        int paramHintActiveParam;                       // Which param is active (0-based)
        int paramHintOverloadIndex;                     // Which overload is shown
        TextPosition paramHintAnchor;                   // Where the '(' is
        std::string paramHintFuncName;                  // Name of the function that triggered it

        // Search state
        std::string searchText;
        std::vector<TextPosition> searchResults;
        int currentSearchResult;
        bool showFindDialog;
        bool showReplaceInput;
        char findInputBuffer[256];
        char replaceInputBuffer[256];
        bool findCaseSensitive;
        bool pendingScrollToCursor = false;
        bool findRefocusInput = false;
        bool pendingFocus = false;
        bool findLastCaseSensitive = false;
        bool showContextMenu = false;
        ImVec2 contextMenuPos;

        // Visual settings
        ImVec4 palette[static_cast<int>(TokenType::Count)];
        ImVec4 backgroundColor;
        ImVec4 lineNumberColor;
        ImVec4 currentLineColor;
        ImVec4 selectionColor;
        ImVec4 cursorColor;
        ImVec4 matchingBracketColor;
        ImVec4 searchHighlightColor;

        // Character metrics
        float charWidth;
        float lineHeight;
        float lineHeightFactor;
        float textOffsetY; // centers glyphs vertically within lineHeight
        int lineNumberDigits;
        float lineNumberWidth;
        float leftMargin;
        float textStartX;

        FontZoomCallback onFontZoom;

        // Semantic suggestions engine
        std::unique_ptr<SemanticSuggestions> suggestions;
        std::vector<SuggestionItem> currentSuggestions;
        int suggestionIndex;
        bool scrollToSuggestion; // Flag to sync scroll with selection only on change

        // Internal methods
        void initializeLanguage();
        void initializePalette();
        void initializeSuggestions();
        void addEngineAPISuggestions();
        void updateSuggestions(bool manualInvoke = false);
        void applySuggestion();
        void renderSuggestions(const ImVec2& origin);
        bool isInCommentOrString(const TextPosition& pos) const;
        void triggerParamHint();
        void updateParamHint();
        void closeParamHint();
        void renderParamHint(const ImVec2& origin);
        SuggestionContext buildSuggestionContext() const;
        std::string inferTypeOfVariable(const std::string& varName, int currentLine) const;
        // Type of the expression ending at endColumn, for member completion and param hints
        std::string inferTypeBefore(int lineIndex, int endColumn) const;
        void tokenizeLine(int lineIndex);
        void tokenizeAll();
        TokenType classifyWord(const std::string& word) const;

        void setLinesFromText(const std::string& text);
        // Cursors sorted by edit position, so an edit never shifts one not yet applied
        std::vector<size_t> cursorEditOrder() const;

        void ensureValidCursors();
        void mergeCursors();
        void sortCursors();
        void scrollToCursor();
        TextPosition clampPosition(const TextPosition& pos) const;

        void handleKeyboardInput();
        void handleMouseInput();
        void handleTextInput();

        void moveCursor(Cursor& cursor, int deltaLine, int deltaCol, bool shift);
        void moveCursorWord(Cursor& cursor, int direction, bool shift);
        void moveCursorToLineStart(Cursor& cursor, bool shift);
        void moveCursorToLineEnd(Cursor& cursor, bool shift);

        void insertTextAtCursor(Cursor& cursor, const std::string& text, bool allowAutoIndent);
        void deleteRange(const TextPosition& start, const TextPosition& end);
        void moveSelectedText(const TextPosition& destPos);
        std::string getRange(const TextPosition& start, const TextPosition& end) const;

        void addUndoRecord();
        void finalizeUndoRecord();

        int getLineIndent(int lineIndex) const;
        std::string getIndentString(int level) const;

        TextPosition screenToText(const ImVec2& screenPos, const ImVec2& origin) const;
        ImVec2 textToScreen(const TextPosition& pos, const ImVec2& origin) const;

        void renderLineNumbers(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderText(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderSelections(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderCursors(ImDrawList* drawList, const ImVec2& origin);
        void renderMatchingBrackets(ImDrawList* drawList, const ImVec2& origin);
        void renderSearchHighlights(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderAutoComplete(const ImVec2& origin);
        void renderFindDialog(const ImVec2& editorPos, const ImVec2& editorSize);
        void renderContextMenu();

        void placeCursorAtClick(const TextPosition& clickPos);

        char getCharAt(const TextPosition& pos) const;
        char findMatchingBracket(const TextPosition& pos, TextPosition& matchPos) const;
        bool isOpenBracket(char c) const;
        bool isCloseBracket(char c) const;
        char getMatchingBracket(char c) const;

        bool isWordChar(char c) const;
        int prevUtf8Column(int lineIndex, int column) const;
        int nextUtf8Column(int lineIndex, int column) const;
        int byteOffsetToVisualColumn(int lineIndex, int byteOffset) const;
        int visualColumnToByteOffset(int lineIndex, int visualColumn) const;
        float byteOffsetToPixelX(int lineIndex, int byteOffset) const;
        int pixelXToByteOffset(int lineIndex, float pixelX) const;
        float lineWidthPixels(int lineIndex) const;
        TextPosition findWordStart(const TextPosition& pos) const;
        TextPosition findWordEnd(const TextPosition& pos) const;
        TextPosition findDeleteWordStart(const TextPosition& pos) const;
        TextPosition findDeleteWordEnd(const TextPosition& pos) const;

        void updateSearchResults();
    };

} // namespace doriax::editor
