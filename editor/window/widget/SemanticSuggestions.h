#pragma once

#include "imgui.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <algorithm>
#include <cctype>

namespace doriax::editor {

    enum class SuggestionKind {
        Keyword,
        Type,
        Function,
        Variable,
        Snippet,
        Class,
        Interface,
        Module,
        Property,
        Method,
        Field,
        Constant,
        Enum,
        EnumMember,
        Operator,
        Text
    };

    struct SuggestionItem {
        std::string label;           // Display text
        std::string insertText;      // Text to insert
        std::string detail;          // Additional info (e.g., type signature)
        std::string documentation;   // Full documentation
        SuggestionKind kind;
        int score;                   // Match score for ranking
        bool deprecated;             // Strike-through if deprecated
        
        SuggestionItem() : kind(SuggestionKind::Text), score(0), deprecated(false) {}
    };

    struct SuggestionContext {
        std::string currentWord;     // Word being typed
        std::string lineContent;     // Full line content
        int cursorColumn;            // Cursor position in line
        int lineNumber;              // Current line number
        std::string previousWord;    // Word before current (for context)
        bool afterDot;               // Is cursor after a '.' (member access)
        bool afterArrow;             // Is cursor after '->' (pointer member access)
        bool afterDoubleColon;       // Is cursor after '::' (scope resolution)
    };

    class SemanticSuggestions {
    public:
        SemanticSuggestions();
        ~SemanticSuggestions();

        // Configuration
        void SetMinPrefixLength(int length) { minPrefixLength = length; }
        void SetMaxSuggestions(int max) { maxSuggestions = max; }
        void SetFuzzyMatching(bool enable) { fuzzyMatching = enable; }
        void SetCaseSensitive(bool sensitive) { caseSensitive = sensitive; }

        // Language setup
        void SetKeywords(const std::unordered_set<std::string>& keywords);
        void SetTypes(const std::unordered_set<std::string>& types);
        void SetBuiltinFunctions(const std::unordered_set<std::string>& functions);
        void AddSnippet(const std::string& trigger, const std::string& expansion, const std::string& description);
        void ClearSnippets();

        // Document context
        void UpdateDocumentWords(const std::vector<std::string>& lines);
        void AddSymbol(const std::string& name, SuggestionKind kind, const std::string& detail = "");
        void ClearSymbols();

        // Core functionality
        std::vector<SuggestionItem> GetSuggestions(const SuggestionContext& context);
        bool ShouldTrigger(char typedChar, const SuggestionContext& context) const;

        // UI helpers
        static const char* GetKindIcon(SuggestionKind kind);
        static ImVec4 GetKindColor(SuggestionKind kind);
        static const char* GetKindName(SuggestionKind kind);

    private:
        // Settings
        int minPrefixLength;
        int maxSuggestions;
        bool fuzzyMatching;
        bool caseSensitive;

        // Word sources
        std::unordered_set<std::string> keywords;
        std::unordered_set<std::string> types;
        std::unordered_set<std::string> builtinFunctions;
        std::unordered_set<std::string> documentWords;
        std::vector<SuggestionItem> snippets;
        std::vector<SuggestionItem> symbols;

        // Matching
        int calculateMatchScore(const std::string& candidate, const std::string& query) const;
        bool matchesQuery(const std::string& candidate, const std::string& query) const;
        bool fuzzyMatch(const std::string& candidate, const std::string& query, int& outScore) const;
        bool prefixMatch(const std::string& candidate, const std::string& query) const;
        bool substringMatch(const std::string& candidate, const std::string& query) const;

        // Helpers
        std::string toLower(const std::string& str) const;
        bool isWordChar(char c) const;
        void addCandidates(std::vector<SuggestionItem>& results, 
                          const std::unordered_set<std::string>& source,
                          SuggestionKind kind,
                          const std::string& query);
    };

} // namespace doriax::editor
