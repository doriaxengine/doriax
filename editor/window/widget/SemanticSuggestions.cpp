#include "SemanticSuggestions.h"
#include "external/IconsFontAwesome6.h"
#include <algorithm>
#include <cmath>

namespace doriax::editor {

SemanticSuggestions::SemanticSuggestions()
    : minPrefixLength(1)
    , maxSuggestions(20)
    , fuzzyMatching(true)
    , caseSensitive(false)
{
}

SemanticSuggestions::~SemanticSuggestions() {
}

void SemanticSuggestions::SetKeywords(const std::unordered_set<std::string>& kw) {
    keywords = kw;
}

void SemanticSuggestions::SetTypes(const std::unordered_set<std::string>& t) {
    types = t;
}

void SemanticSuggestions::SetBuiltinFunctions(const std::unordered_set<std::string>& funcs) {
    builtinFunctions = funcs;
}

void SemanticSuggestions::AddSnippet(const std::string& trigger, const std::string& expansion, const std::string& description) {
    SuggestionItem item;
    item.label = trigger;
    item.insertText = expansion;
    item.detail = description;
    item.kind = SuggestionKind::Snippet;
    snippets.push_back(item);
}

void SemanticSuggestions::ClearSnippets() {
    snippets.clear();
}

void SemanticSuggestions::UpdateDocumentWords(const std::vector<std::string>& lines) {
    documentWords.clear();
    
    for (const auto& line : lines) {
        size_t i = 0;
        while (i < line.size()) {
            if (isWordChar(line[i])) {
                size_t start = i;
                while (i < line.size() && isWordChar(line[i])) ++i;
                std::string word = line.substr(start, i - start);
                // Only add words with at least 2 characters
                if (word.size() >= 2) {
                    documentWords.insert(word);
                }
            } else {
                ++i;
            }
        }
    }
}

void SemanticSuggestions::AddSymbol(const std::string& name, SuggestionKind kind, const std::string& detail) {
    SuggestionItem item;
    item.label = name;
    item.insertText = name;
    item.detail = detail;
    item.kind = kind;
    symbols.push_back(item);
}

void SemanticSuggestions::ClearSymbols() {
    symbols.clear();
}

bool SemanticSuggestions::ShouldTrigger(char typedChar, const SuggestionContext& context) const {
    // Trigger on alphanumeric characters
    if (std::isalnum(static_cast<unsigned char>(typedChar))) {
        return context.currentWord.length() >= static_cast<size_t>(minPrefixLength);
    }
    
    // Trigger on member access operators
    if (typedChar == '.') return true;
    if (context.afterArrow || context.afterDoubleColon) return true;
    
    return false;
}

std::vector<SuggestionItem> SemanticSuggestions::GetSuggestions(const SuggestionContext& context) {
    std::vector<SuggestionItem> results;
    
    const std::string& query = context.currentWord;
    
    // Don't suggest if query is too short (unless after special operators)
    if (query.length() < static_cast<size_t>(minPrefixLength) && 
        !context.afterDot && !context.afterArrow && !context.afterDoubleColon) {
        return results;
    }
    
    // Add candidates from all sources
    addCandidates(results, keywords, SuggestionKind::Keyword, query);
    addCandidates(results, types, SuggestionKind::Type, query);
    addCandidates(results, builtinFunctions, SuggestionKind::Function, query);
    addCandidates(results, documentWords, SuggestionKind::Variable, query);
    
    // Add snippets
    for (const auto& snippet : snippets) {
        if (matchesQuery(snippet.label, query)) {
            SuggestionItem item = snippet;
            item.score = calculateMatchScore(snippet.label, query);
            // Boost snippet score slightly
            item.score += 5;
            results.push_back(item);
        }
    }
    
    // Add custom symbols
    for (const auto& symbol : symbols) {
        if (matchesQuery(symbol.label, query)) {
            SuggestionItem item = symbol;
            item.score = calculateMatchScore(symbol.label, query);
            results.push_back(item);
        }
    }
    
    // Remove duplicates (prefer higher-ranked kind)
    std::unordered_map<std::string, size_t> seen;
    std::vector<SuggestionItem> unique;
    for (const auto& item : results) {
        auto it = seen.find(item.label);
        if (it == seen.end()) {
            seen[item.label] = unique.size();
            unique.push_back(item);
        } else {
            // Keep the one with higher score or better kind
            if (item.score > unique[it->second].score) {
                unique[it->second] = item;
            }
        }
    }
    results = std::move(unique);
    
    // Remove the exact match of what's being typed (if it exists)
    results.erase(
        std::remove_if(results.begin(), results.end(), 
            [&query, this](const SuggestionItem& item) {
                std::string itemLower = caseSensitive ? item.label : toLower(item.label);
                std::string queryLower = caseSensitive ? query : toLower(query);
                return itemLower == queryLower;
            }),
        results.end()
    );
    
    // Sort by score (descending), then alphabetically
    std::sort(results.begin(), results.end(), [](const SuggestionItem& a, const SuggestionItem& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.label < b.label;
    });
    
    // Limit results
    if (results.size() > static_cast<size_t>(maxSuggestions)) {
        results.resize(maxSuggestions);
    }
    
    return results;
}

void SemanticSuggestions::addCandidates(std::vector<SuggestionItem>& results,
                                         const std::unordered_set<std::string>& source,
                                         SuggestionKind kind,
                                         const std::string& query) {
    for (const auto& word : source) {
        if (matchesQuery(word, query)) {
            SuggestionItem item;
            item.label = word;
            item.insertText = word;
            item.kind = kind;
            item.score = calculateMatchScore(word, query);
            
            // Boost score for keywords and types
            if (kind == SuggestionKind::Keyword) item.score += 10;
            if (kind == SuggestionKind::Type) item.score += 8;
            if (kind == SuggestionKind::Function) item.score += 6;
            
            results.push_back(item);
        }
    }
}

bool SemanticSuggestions::matchesQuery(const std::string& candidate, const std::string& query) const {
    if (query.empty()) return true;
    
    if (fuzzyMatching) {
        int score;
        return fuzzyMatch(candidate, query, score);
    } else {
        return prefixMatch(candidate, query);
    }
}

int SemanticSuggestions::calculateMatchScore(const std::string& candidate, const std::string& query) const {
    if (query.empty()) return 0;
    
    std::string candLower = caseSensitive ? candidate : toLower(candidate);
    std::string queryLower = caseSensitive ? query : toLower(query);
    
    int score = 0;
    
    // Exact prefix match gets highest score
    if (candLower.find(queryLower) == 0) {
        score += 100;
        // Bonus for exact length match
        if (candLower.length() == queryLower.length()) {
            score += 50;
        }
        // Bonus for case-sensitive prefix match
        if (candidate.find(query) == 0) {
            score += 20;
        }
    }
    // Substring match
    else if (candLower.find(queryLower) != std::string::npos) {
        score += 50;
    }
    // Fuzzy match scoring
    else if (fuzzyMatching) {
        int fuzzyScore;
        if (fuzzyMatch(candidate, query, fuzzyScore)) {
            score += fuzzyScore;
        }
    }
    
    // Penalize very long candidates slightly
    if (candidate.length() > query.length() + 10) {
        score -= static_cast<int>(candidate.length() - query.length() - 10);
    }
    
    // Bonus for camelCase/snake_case word boundary matches
    size_t queryIdx = 0;
    bool atBoundary = true;
    for (size_t i = 0; i < candidate.size() && queryIdx < query.size(); ++i) {
        char c = caseSensitive ? candidate[i] : std::tolower(candidate[i]);
        char q = caseSensitive ? query[queryIdx] : std::tolower(query[queryIdx]);
        
        bool isBoundary = (i == 0) || 
                          (candidate[i] == '_') || 
                          (std::isupper(candidate[i]) && (i == 0 || !std::isupper(candidate[i-1])));
        
        if (c == q) {
            if (isBoundary) {
                score += 5; // Bonus for matching at word boundaries
            }
            queryIdx++;
            atBoundary = false;
        } else if (candidate[i] == '_') {
            atBoundary = true;
        }
    }
    
    return score;
}

bool SemanticSuggestions::fuzzyMatch(const std::string& candidate, const std::string& query, int& outScore) const {
    if (query.empty()) {
        outScore = 0;
        return true;
    }
    
    if (query.length() > candidate.length()) {
        outScore = 0;
        return false;
    }
    
    std::string candLower = caseSensitive ? candidate : toLower(candidate);
    std::string queryLower = caseSensitive ? query : toLower(query);
    
    // Check if all query characters exist in candidate in order
    size_t candIdx = 0;
    size_t queryIdx = 0;
    int score = 0;
    int consecutiveBonus = 0;
    size_t lastMatchIdx = 0;
    
    while (candIdx < candLower.size() && queryIdx < queryLower.size()) {
        if (candLower[candIdx] == queryLower[queryIdx]) {
            // Match found
            score += 10;
            
            // Bonus for consecutive matches
            if (queryIdx > 0 && candIdx == lastMatchIdx + 1) {
                consecutiveBonus += 5;
            }
            
            // Bonus for matching at start
            if (candIdx == 0) {
                score += 15;
            }
            
            // Bonus for matching after separator
            if (candIdx > 0 && (candidate[candIdx - 1] == '_' || candidate[candIdx - 1] == '-')) {
                score += 10;
            }
            
            // Bonus for camelCase boundary
            if (candIdx > 0 && std::isupper(candidate[candIdx]) && std::islower(candidate[candIdx - 1])) {
                score += 10;
            }
            
            lastMatchIdx = candIdx;
            queryIdx++;
        }
        candIdx++;
    }
    
    if (queryIdx == queryLower.size()) {
        outScore = score + consecutiveBonus;
        // Penalize if matches are spread far apart
        outScore -= static_cast<int>(lastMatchIdx - queryLower.size()) / 2;
        return true;
    }
    
    outScore = 0;
    return false;
}

bool SemanticSuggestions::prefixMatch(const std::string& candidate, const std::string& query) const {
    if (query.length() > candidate.length()) return false;
    
    std::string candLower = caseSensitive ? candidate : toLower(candidate);
    std::string queryLower = caseSensitive ? query : toLower(query);
    
    return candLower.find(queryLower) == 0;
}

bool SemanticSuggestions::substringMatch(const std::string& candidate, const std::string& query) const {
    if (query.length() > candidate.length()) return false;
    
    std::string candLower = caseSensitive ? candidate : toLower(candidate);
    std::string queryLower = caseSensitive ? query : toLower(query);
    
    return candLower.find(queryLower) != std::string::npos;
}

std::string SemanticSuggestions::toLower(const std::string& str) const {
    std::string result = str;
    for (char& c : result) {
        c = std::tolower(static_cast<unsigned char>(c));
    }
    return result;
}

bool SemanticSuggestions::isWordChar(char c) const {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

const char* SemanticSuggestions::GetKindIcon(SuggestionKind kind) {
    switch (kind) {
        case SuggestionKind::Keyword:    return ICON_FA_KEY;
        case SuggestionKind::Type:       return ICON_FA_CUBE;
        case SuggestionKind::Function:   return ICON_FA_CODE;
        case SuggestionKind::Variable:   return ICON_FA_V;
        case SuggestionKind::Snippet:    return ICON_FA_PUZZLE_PIECE;
        case SuggestionKind::Class:      return ICON_FA_C;
        case SuggestionKind::Interface:  return ICON_FA_I;
        case SuggestionKind::Module:     return ICON_FA_BOX;
        case SuggestionKind::Property:   return ICON_FA_WRENCH;
        case SuggestionKind::Method:     return ICON_FA_M;
        case SuggestionKind::Field:      return ICON_FA_F;
        case SuggestionKind::Constant:   return ICON_FA_LOCK;
        case SuggestionKind::Enum:       return ICON_FA_LIST;
        case SuggestionKind::EnumMember: return ICON_FA_LIST_OL;
        case SuggestionKind::Operator:   return ICON_FA_PLUS_MINUS;
        case SuggestionKind::Text:       
        default:                         return ICON_FA_FONT;
    }
}

ImVec4 SemanticSuggestions::GetKindColor(SuggestionKind kind) {
    switch (kind) {
        case SuggestionKind::Keyword:    return ImVec4(0.78f, 0.44f, 0.86f, 1.0f);  // Purple
        case SuggestionKind::Type:       return ImVec4(0.31f, 0.69f, 0.78f, 1.0f);  // Cyan
        case SuggestionKind::Function:   return ImVec4(0.86f, 0.82f, 0.53f, 1.0f);  // Yellow
        case SuggestionKind::Variable:   return ImVec4(0.61f, 0.82f, 0.98f, 1.0f);  // Light blue
        case SuggestionKind::Snippet:    return ImVec4(0.98f, 0.61f, 0.61f, 1.0f);  // Light red
        case SuggestionKind::Class:      return ImVec4(0.98f, 0.78f, 0.31f, 1.0f);  // Orange
        case SuggestionKind::Interface:  return ImVec4(0.31f, 0.98f, 0.78f, 1.0f);  // Teal
        case SuggestionKind::Module:     return ImVec4(0.78f, 0.98f, 0.31f, 1.0f);  // Lime
        case SuggestionKind::Property:   return ImVec4(0.61f, 0.61f, 0.98f, 1.0f);  // Lavender
        case SuggestionKind::Method:     return ImVec4(0.86f, 0.82f, 0.53f, 1.0f);  // Yellow (same as function)
        case SuggestionKind::Field:      return ImVec4(0.61f, 0.82f, 0.98f, 1.0f);  // Light blue (same as variable)
        case SuggestionKind::Constant:   return ImVec4(0.31f, 0.69f, 0.78f, 1.0f);  // Cyan
        case SuggestionKind::Enum:       return ImVec4(0.98f, 0.78f, 0.31f, 1.0f);  // Orange
        case SuggestionKind::EnumMember: return ImVec4(0.78f, 0.61f, 0.31f, 1.0f);  // Brown
        case SuggestionKind::Operator:   return ImVec4(0.86f, 0.86f, 0.86f, 1.0f);  // Gray
        case SuggestionKind::Text:       
        default:                         return ImVec4(0.86f, 0.86f, 0.86f, 1.0f);  // Gray
    }
}

const char* SemanticSuggestions::GetKindName(SuggestionKind kind) {
    switch (kind) {
        case SuggestionKind::Keyword:    return "Keyword";
        case SuggestionKind::Type:       return "Type";
        case SuggestionKind::Function:   return "Function";
        case SuggestionKind::Variable:   return "Variable";
        case SuggestionKind::Snippet:    return "Snippet";
        case SuggestionKind::Class:      return "Class";
        case SuggestionKind::Interface:  return "Interface";
        case SuggestionKind::Module:     return "Module";
        case SuggestionKind::Property:   return "Property";
        case SuggestionKind::Method:     return "Method";
        case SuggestionKind::Field:      return "Field";
        case SuggestionKind::Constant:   return "Constant";
        case SuggestionKind::Enum:       return "Enum";
        case SuggestionKind::EnumMember: return "Enum Member";
        case SuggestionKind::Operator:   return "Operator";
        case SuggestionKind::Text:       
        default:                         return "Text";
    }
}

} // namespace doriax::editor
