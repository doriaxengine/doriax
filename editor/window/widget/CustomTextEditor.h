#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <memory>
#include <regex>
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

    struct AutoCompleteItem {
        std::string label;
        std::string insertText;
        std::string detail;
        TokenType kind;
    };

    struct LanguageDefinition {
        std::unordered_set<std::string> keywords;
        std::unordered_set<std::string> types;
        std::unordered_set<std::string> builtinFunctions;
        std::string singleLineComment;
        std::string multiLineCommentStart;
        std::string multiLineCommentEnd;
        std::string preprocessorPrefix;
        bool caseSensitive;

        LanguageDefinition() : caseSensitive(true) {}
    };

    class CustomTextEditor {
    public:
        CustomTextEditor();
        ~CustomTextEditor();

        // Text operations
        void SetText(const std::string& text);
        std::string GetText() const;
        std::vector<std::string> GetTextLines() const;
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
        bool FindNext(bool caseSensitive = true, bool wholeWord = false);
        bool FindPrevious(bool caseSensitive = true, bool wholeWord = false);
        void OpenFind();
        void CloseFind();
        bool IsFindOpen() const { return showFindDialog; }
        void SelectAllOccurrences(const std::string& text, bool caseSensitive = true);
        int ReplaceAll(const std::string& find, const std::string& replace, bool caseSensitive = true);

        // Editor settings
        void SetReadOnly(bool value) { readOnly = value; }
        bool IsReadOnly() const { return readOnly; }
        void SetTabSize(int size) { tabSize = size; }
        int GetTabSize() const { return tabSize; }
        void SetShowLineNumbers(bool show) { showLineNumbers = show; }
        void SetShowWhitespace(bool show) { showWhitespace = show; }
        void SetAutoIndent(bool enable) { autoIndent = enable; }
        void SetHighlightCurrentLine(bool enable) { highlightCurrentLine = enable; }
        void SetMatchBrackets(bool enable) { matchBrackets = enable; }
        void SetShowMinimap(bool show) { showMinimap = show; }

        // Auto-complete
        void SetAutoComplete(bool enable) { autoComplete = enable; }
        void TriggerAutoComplete();
        void CloseAutoComplete();
        bool IsAutoCompleteOpen() const { return showAutoComplete; }

        // Tooltip
        void ShowTooltip(const std::string& text, const ImVec2& pos);
        void HideTooltip();

        // Rendering
        void Render(const char* title, const ImVec2& size = ImVec2(0, 0), bool border = false);

        // Callbacks
        using TextChangedCallback = std::function<void()>;
        void SetTextChangedCallback(TextChangedCallback callback) { onTextChanged = callback; }

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
        bool showWhitespace;
        bool autoIndent;
        bool highlightCurrentLine;
        bool matchBrackets;
        bool showMinimap;
        bool autoComplete;
        bool isDragging;
        bool isDraggingText;
        bool mayDragText;
        bool isDoubleClick;
        bool isTripleClick;
        std::chrono::steady_clock::time_point lastClickTime;
        TextPosition lastClickPos;
        int clickCount;

        // Scroll state
        float scrollX;
        float scrollY;

        // Auto-complete state
        bool showAutoComplete;
        std::vector<AutoCompleteItem> autoCompleteItems;
        int autoCompleteIndex;
        TextPosition autoCompleteAnchor;
        std::string autoCompleteFilter;
        bool suggestionsHovered;  // Track if suggestions popup is hovered

        // Tooltip state
        bool showTooltipFlag;
        std::string tooltipText;
        ImVec2 tooltipPos;
        std::chrono::steady_clock::time_point tooltipStartTime;

        // Search state
        std::string searchText;
        std::vector<TextPosition> searchResults;
        int currentSearchResult;
        bool showFindDialog;
        char findInputBuffer[256];
        bool findCaseSensitive;
        bool findWholeWord;
        bool pendingScrollToCursor = false;
        bool findRefocusInput = false;
        bool findLastCaseSensitive = false;

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
        float lineNumberWidth;
        float leftMargin;
        float textStartX;

        // Callback
        TextChangedCallback onTextChanged;

        // Semantic suggestions engine
        std::unique_ptr<SemanticSuggestions> suggestions;
        std::vector<SuggestionItem> currentSuggestions;
        int suggestionIndex;
        bool scrollToSuggestion; // Flag to sync scroll with selection only on change

        // Internal methods
        void initializeLanguage();
        void initializePalette();
        void initializeSuggestions();
        void updateSuggestions();
        void applySuggestion();
        void renderSuggestions(const ImVec2& origin);
        SuggestionContext buildSuggestionContext() const;
        void tokenizeLine(int lineIndex);
        void tokenizeAll();
        TokenType classifyWord(const std::string& word) const;

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
        void beginUndoGroup();
        void endUndoGroup();

        int getLineIndent(int lineIndex) const;
        std::string getIndentString(int level) const;
        void autoIndentLine(int lineIndex);

        void updateAutoComplete();
        void applyAutoComplete();
        std::vector<AutoCompleteItem> getCompletionItems(const std::string& prefix) const;

        TextPosition screenToText(const ImVec2& screenPos, const ImVec2& origin) const;
        ImVec2 textToScreen(const TextPosition& pos, const ImVec2& origin) const;

        void renderLineNumbers(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderText(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderSelections(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderCursors(ImDrawList* drawList, const ImVec2& origin);
        void renderMatchingBrackets(ImDrawList* drawList, const ImVec2& origin);
        void renderSearchHighlights(ImDrawList* drawList, const ImVec2& origin, int startLine, int endLine);
        void renderAutoComplete(const ImVec2& origin);
        void renderTooltip();
        void renderFindDialog(const ImVec2& editorPos, const ImVec2& editorSize);
        void renderMinimap(ImDrawList* drawList, const ImVec2& origin, const ImVec2& size);

        char getCharAt(const TextPosition& pos) const;
        char findMatchingBracket(const TextPosition& pos, TextPosition& matchPos) const;
        bool isOpenBracket(char c) const;
        bool isCloseBracket(char c) const;
        char getMatchingBracket(char c) const;

        bool isWordChar(char c) const;
        TextPosition findWordStart(const TextPosition& pos) const;
        TextPosition findWordEnd(const TextPosition& pos) const;

        void updateSearchResults();

        int getColumnFromOffset(int lineIndex, int offset) const;
        int getOffsetFromColumn(int lineIndex, int column) const;
    };

} // namespace doriax::editor
