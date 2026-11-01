// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AiChatWindow.h"

#include "App.h"
#include "AppSettings.h"
#include "Backend.h"
#include "Theme.h"
#include "Base64.h"
#include "ai/EditorActionExecutor.h"
#include "ai/EditorActionRegistry.h"
#include "ai/SecretStore.h"
#include "external/IconsFontAwesome6.h"
#include "util/Clipboard.h"
#include "util/EntityPayload.h"
#include "util/FileDialogs.h"
#include "util/FileUtils.h"
#include "util/Util.h"
#include "window/CodeEditor.h"
#include "window/Widgets.h"
#include "window/widget/InputTextContextMenu.h"
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace doriax::editor {

namespace {

const ImVec4 kUserColor(0.55f, 0.75f, 1.0f, 1.0f);
const ImVec4 kAssistantColor(0.72f, 0.86f, 0.68f, 1.0f);
const ImVec4 kSuccessColor(0.55f, 0.85f, 0.55f, 1.0f);
const ImVec4 kErrorColor(0.95f, 0.55f, 0.55f, 1.0f);
const ImVec4 kCodeColor(0.82f, 0.85f, 0.72f, 1.0f);
constexpr size_t kMaxAttachmentBytes = 10 * 1024 * 1024;
constexpr size_t kMaxTextAttachmentBytes = 1024 * 1024;
// Base64 expands images by roughly one third; 12 MB raw keeps the complete
// JSON request below providers' common 20 MB inline-data limit with headroom.
constexpr size_t kMaxAttachmentTotalBytes = 12 * 1024 * 1024;
constexpr float kAttachmentFontScale = 0.78f;

std::string attachmentChipLabel(const ai::ChatAttachment& attachment) {
    return std::string(attachment.isImage() ? ICON_FA_FILE_IMAGE : ICON_FA_FILE_LINES) +
           "  " + attachment.name + "  " ICON_FA_XMARK;
}

float attachmentChipHeight() {
    return std::floor(ImGui::GetFontSize() * kAttachmentFontScale) + 4.0f;
}

float attachmentRowHeight(const std::vector<ai::ChatAttachment>& attachments,
                          float availableWidth) {
    const float fontSize = std::floor(ImGui::GetFontSize() * kAttachmentFontScale);
    float contentWidth = 0.0f;
    for (const ai::ChatAttachment& attachment : attachments) {
        const std::string label = attachmentChipLabel(attachment);
        contentWidth += ImGui::GetFont()->CalcTextSizeA(
            fontSize, FLT_MAX, 0.0f, label.data(), label.data() + label.size()).x + 10.0f;
        contentWidth += ImGui::GetStyle().ItemSpacing.x;
    }
    return attachmentChipHeight() +
           (contentWidth > availableWidth ? ImGui::GetStyle().ScrollbarSize : 0.0f);
}

// ImGui does not soft-wrap editable multiline inputs. The composer inserts
// generated line breaks for display, tracks them separately from user-entered
// newlines, and reflows them whenever the input width changes.
struct PromptWrapContext {
    float maxLineWidth = 0.0f;
    std::vector<int>* softWraps = nullptr;
    std::string* displayText = nullptr;
    int* mentionDisplayCursor = nullptr;
    int* pendingMentionRawCursor = nullptr;
    AiChatWindow* window = nullptr;
};

struct PromptWrapResult {
    std::string displayText;
    std::vector<int> softWrapDisplayOffsets;
    std::vector<int> softWrapRawOffsets;
};

bool isPromptWrapSpace(char c) {
    return c == ' ' || c == '\t';
}

bool isUtf8Continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
}

int nextCodepointStart(const char* text, int len, int pos) {
    if (pos >= len) {
        return len;
    }
    ++pos;
    while (pos < len && isUtf8Continuation(text[pos])) {
        ++pos;
    }
    return pos;
}

float promptLineWidth(const char* text, int start, int end) {
    if (end <= start) {
        return 0.0f;
    }
    return ImGui::CalcTextSize(text + start, text + end, false).x;
}

int findPromptWrapOffset(const std::string& text, int lineStart, int lineEnd,
                         float maxLineWidth) {
    int bestOffset = lineStart;
    int bestSpaceOffset = -1;
    const int len = static_cast<int>(text.size());
    const char* raw = text.c_str();

    for (int pos = lineStart; pos < lineEnd;) {
        int next = nextCodepointStart(raw, len, pos);
        if (next <= pos || next > lineEnd) {
            next = std::min(pos + 1, lineEnd);
        }

        if (promptLineWidth(raw, lineStart, next) > maxLineWidth) {
            if (bestSpaceOffset > lineStart) {
                return bestSpaceOffset;
            }
            return bestOffset > lineStart ? bestOffset : next;
        }

        if (isPromptWrapSpace(raw[pos])) {
            bestSpaceOffset = next;
        }
        bestOffset = next;
        pos = next;
    }

    return -1;
}

void sanitizePromptSoftWraps(std::vector<int>& softWraps, const std::string& displayText) {
    std::sort(softWraps.begin(), softWraps.end());
    softWraps.erase(std::unique(softWraps.begin(), softWraps.end()), softWraps.end());

    std::vector<int> valid;
    valid.reserve(softWraps.size());
    for (int pos : softWraps) {
        if (pos >= 0 && pos < static_cast<int>(displayText.size()) &&
            displayText[static_cast<size_t>(pos)] == '\n') {
            valid.push_back(pos);
        }
    }
    softWraps.swap(valid);
}

void adjustPromptSoftWrapsForEdit(const std::string& oldDisplayText,
                                  const std::string& currentDisplayText,
                                  std::vector<int>& softWraps) {
    if (oldDisplayText.empty() || oldDisplayText == currentDisplayText) {
        sanitizePromptSoftWraps(softWraps, currentDisplayText);
        return;
    }

    int prefix = 0;
    const int oldLen = static_cast<int>(oldDisplayText.size());
    const int newLen = static_cast<int>(currentDisplayText.size());
    while (prefix < oldLen && prefix < newLen &&
           oldDisplayText[static_cast<size_t>(prefix)] ==
               currentDisplayText[static_cast<size_t>(prefix)]) {
        ++prefix;
    }

    int suffix = 0;
    while (oldLen - suffix > prefix && newLen - suffix > prefix &&
           oldDisplayText[static_cast<size_t>(oldLen - suffix - 1)] ==
               currentDisplayText[static_cast<size_t>(newLen - suffix - 1)]) {
        ++suffix;
    }

    const int oldChangeEnd = oldLen - suffix;
    const int newChangeEnd = newLen - suffix;
    const int delta = (newChangeEnd - prefix) - (oldChangeEnd - prefix);

    std::vector<int> adjusted;
    adjusted.reserve(softWraps.size());
    for (int pos : softWraps) {
        if (pos < prefix) {
            adjusted.push_back(pos);
        } else if (pos >= oldChangeEnd) {
            adjusted.push_back(pos + delta);
        }
    }

    softWraps.swap(adjusted);
    sanitizePromptSoftWraps(softWraps, currentDisplayText);
}

int promptDisplayPosToRawPos(int displayPos, const std::vector<int>& softWraps) {
    int removed = 0;
    for (int wrapPos : softWraps) {
        if (wrapPos >= displayPos) {
            break;
        }
        ++removed;
    }
    return std::max(0, displayPos - removed);
}

int promptRawPosToDisplayPos(int rawPos, const std::vector<int>& softWrapRawOffsets) {
    int inserted = 0;
    for (int wrapRawPos : softWrapRawOffsets) {
        // A wrap at rawPos inserts a soft newline before that raw character, so
        // the caret for rawPos sits after the inserted newline.
        if (wrapRawPos > rawPos) {
            break;
        }
        ++inserted;
    }
    return std::max(0, rawPos + inserted);
}

std::vector<int> softWrapRawOffsetsFromDisplay(const std::vector<int>& softWrapDisplayOffsets) {
    std::vector<int> rawOffsets;
    rawOffsets.reserve(softWrapDisplayOffsets.size());
    for (int displayWrap : softWrapDisplayOffsets) {
        rawOffsets.push_back(promptDisplayPosToRawPos(displayWrap, softWrapDisplayOffsets));
    }
    return rawOffsets;
}

int promptRawCursorToDisplay(int rawCursor, int displayLen, const std::vector<int>& softWraps) {
    const int rawLen = std::max(0, displayLen - static_cast<int>(softWraps.size()));
    return std::clamp(
        promptRawPosToDisplayPos(std::clamp(rawCursor, 0, rawLen),
                                 softWrapRawOffsetsFromDisplay(softWraps)),
        0, displayLen);
}

// Characters allowed inside an unquoted @token (also used while scanning the active query).
bool isMentionTokenChar(unsigned char c) {
    return std::isalnum(c) || c == '_' || c == '-' || c == '/' || c == '.';
}

bool mentionBodyNeedsQuotes(const std::string& body) {
    if (body.empty()) {
        return false;
    }
    for (unsigned char c : body) {
        if (!isMentionTokenChar(c)) {
            return true;
        }
    }
    return false;
}

// Scene/entity names (and paths) may contain spaces; quote those so the token
// stays one mention: @"New Scene"
std::string formatMentionInsert(std::string body) {
    if (!body.empty() && body.front() == '@') {
        body.erase(body.begin());
    }
    // Already formatted as a quoted body ("New Scene") — don't wrap again.
    if (body.size() >= 2 && body.front() == '"' && body.back() == '"') {
        return "@" + body;
    }
    if (!mentionBodyNeedsQuotes(body)) {
        return "@" + body;
    }

    std::string out = "@\"";
    out.reserve(body.size() + 4);
    for (char c : body) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string lowercaseExtension(const fs::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

std::string imageMimeType(const fs::path& path) {
    const std::string extension = lowercaseExtension(path);
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".gif") return "image/gif";
    if (extension == ".webp") return "image/webp";
    return {};
}

// Inverse of imageMimeType, for naming clipboard image attachments.
std::string extensionForImageMime(const std::string& mimeType) {
    if (mimeType == "image/jpeg") return ".jpg";
    if (mimeType == "image/gif") return ".gif";
    if (mimeType == "image/webp") return ".webp";
    return ".png";
}

bool matchesImageType(const std::vector<unsigned char>& data, const std::string& mimeType) {
    if (mimeType == "image/png") {
        static const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        return data.size() >= sizeof(signature) &&
               std::equal(signature, signature + sizeof(signature), data.begin());
    }
    if (mimeType == "image/jpeg") {
        return data.size() >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
    }
    if (mimeType == "image/gif") {
        return data.size() >= 6 &&
               (std::equal(data.begin(), data.begin() + 6, "GIF87a") ||
                std::equal(data.begin(), data.begin() + 6, "GIF89a"));
    }
    if (mimeType == "image/webp") {
        return data.size() >= 12 &&
               std::equal(data.begin(), data.begin() + 4, "RIFF") &&
               std::equal(data.begin() + 8, data.begin() + 12, "WEBP");
    }
    return false;
}

bool validUtf8Text(const std::vector<unsigned char>& data) {
    size_t i = 0;
    while (i < data.size()) {
        const unsigned char c = data[i];
        if (c == 0) return false;
        size_t continuationCount = 0;
        if (c <= 0x7f) {
            ++i;
            continue;
        } else if (c >= 0xc2 && c <= 0xdf) {
            continuationCount = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            continuationCount = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            continuationCount = 3;
        } else {
            return false;
        }
        if (i + continuationCount >= data.size()) return false;
        for (size_t j = 1; j <= continuationCount; ++j) {
            if ((data[i + j] & 0xc0) != 0x80) return false;
        }
        if (continuationCount == 2 &&
            ((c == 0xe0 && data[i + 1] < 0xa0) || (c == 0xed && data[i + 1] >= 0xa0))) {
            return false;
        }
        if (continuationCount == 3 &&
            ((c == 0xf0 && data[i + 1] < 0x90) || (c == 0xf4 && data[i + 1] >= 0x90))) {
            return false;
        }
        i += continuationCount + 1;
    }
    return true;
}

// Mentions are "@token" or @"quoted token" runs at a word boundary.
std::vector<std::pair<int, int>> findMentionSpans(const std::string& text) {
    std::vector<std::pair<int, int>> spans;
    const int n = static_cast<int>(text.size());
    for (int i = 0; i < n; ++i) {
        if (text[static_cast<size_t>(i)] != '@') {
            continue;
        }
        const bool boundary = i == 0 ||
            std::isspace(static_cast<unsigned char>(text[static_cast<size_t>(i - 1)]));
        if (!boundary) {
            continue;
        }

        int end = i + 1;
        if (end < n && text[static_cast<size_t>(end)] == '"') {
            ++end;
            while (end < n) {
                const char c = text[static_cast<size_t>(end)];
                if (c == '\\' && end + 1 < n) {
                    end += 2;
                    continue;
                }
                ++end;
                if (c == '"') {
                    break;
                }
            }
            spans.emplace_back(i, end);
            continue;
        }

        while (end < n && isMentionTokenChar(
                   static_cast<unsigned char>(text[static_cast<size_t>(end)]))) {
            ++end;
        }
        if (end > i + 1) {
            spans.emplace_back(i, end);
        }
    }
    return spans;
}

bool positionInMentionSpan(int position, const std::vector<std::pair<int, int>>& spans) {
    for (const auto& span : spans) {
        if (position >= span.first && position < span.second) {
            return true;
        }
    }
    return false;
}

SelectableTextView::Paragraph mentionColoredParagraph(std::string text, ImU32 textColor,
                                                       ImU32 mentionColor) {
    SelectableTextView::Paragraph paragraph{std::move(text), textColor};
    for (const auto& [begin, end] : findMentionSpans(paragraph.text)) {
        paragraph.colorSpans.push_back({begin, end, mentionColor});
    }
    return paragraph;
}

std::string stripPromptSoftWraps(const char* text, int len,
                                 const std::vector<int>& softWraps) {
    std::string raw;
    raw.reserve(static_cast<size_t>(std::max(0, len)));

    size_t wrapIndex = 0;
    for (int i = 0; i < len; ++i) {
        bool skipSoftWrap = wrapIndex < softWraps.size() && softWraps[wrapIndex] == i &&
                            text[i] == '\n';
        if (skipSoftWrap) {
            ++wrapIndex;
            continue;
        }
        raw.push_back(text[i]);
        while (wrapIndex < softWraps.size() && softWraps[wrapIndex] <= i) {
            ++wrapIndex;
        }
    }
    return raw;
}

std::string stripPromptSoftWrapsRange(const char* text, int start, int end,
                                      const std::vector<int>& softWraps) {
    if (!text || end <= start) {
        return {};
    }

    std::string raw;
    raw.reserve(static_cast<size_t>(end - start));
    auto wrapIt = std::lower_bound(softWraps.begin(), softWraps.end(), start);
    for (int i = start; i < end; ++i) {
        bool skipSoftWrap = wrapIt != softWraps.end() && *wrapIt == i && text[i] == '\n';
        if (skipSoftWrap) {
            ++wrapIt;
            continue;
        }
        raw.push_back(text[i]);
        while (wrapIt != softWraps.end() && *wrapIt <= i) {
            ++wrapIt;
        }
    }
    return raw;
}

std::string copyPromptContextMenuRange(const char* buffer, int start, int end,
                                       void* userData) {
    auto* context = static_cast<PromptWrapContext*>(userData);
    if (!context || !context->softWraps) {
        return std::string(buffer + start, buffer + end);
    }
    return stripPromptSoftWrapsRange(buffer, start, end, *context->softWraps);
}

PromptWrapResult rewrapPromptText(const std::string& rawText, float maxLineWidth,
                                  size_t maxDisplayBytes) {
    PromptWrapResult result;
    result.displayText.reserve(rawText.size());

    if (rawText.empty() || maxLineWidth <= ImGui::GetFontSize() * 4.0f) {
        result.displayText = rawText;
        return result;
    }

    const int len = static_cast<int>(rawText.size());
    int copyPos = 0;
    int hardLineStart = 0;

    while (hardLineStart < len) {
        int hardLineEnd = hardLineStart;
        while (hardLineEnd < len && rawText[static_cast<size_t>(hardLineEnd)] != '\n') {
            ++hardLineEnd;
        }

        int visualLineStart = hardLineStart;
        while (visualLineStart < hardLineEnd) {
            int wrapAt = findPromptWrapOffset(rawText, visualLineStart, hardLineEnd,
                                              maxLineWidth);
            if (wrapAt <= visualLineStart || wrapAt >= hardLineEnd) {
                break;
            }
            if (rawText.size() + result.softWrapDisplayOffsets.size() + 1 > maxDisplayBytes) {
                break;
            }

            result.displayText.append(rawText.data() + copyPos,
                                      static_cast<size_t>(wrapAt - copyPos));
            result.softWrapDisplayOffsets.push_back(
                static_cast<int>(result.displayText.size()));
            result.softWrapRawOffsets.push_back(wrapAt);
            result.displayText.push_back('\n');
            copyPos = wrapAt;
            visualLineStart = wrapAt;
        }

        if (hardLineEnd >= len) {
            break;
        }

        result.displayText.append(rawText.data() + copyPos,
                                  static_cast<size_t>(hardLineEnd + 1 - copyPos));
        copyPos = hardLineEnd + 1;
        hardLineStart = hardLineEnd + 1;
    }

    if (copyPos < len) {
        result.displayText.append(rawText.data() + copyPos,
                                  static_cast<size_t>(len - copyPos));
    }

    return result;
}

bool rewrapPromptBuffer(char* buffer, int bufferSize, float maxLineWidth,
                        std::vector<int>& softWraps, std::string& displayText,
                        int* cursorPos = nullptr, int* selectionStart = nullptr,
                        int* selectionEnd = nullptr, int* textLen = nullptr) {
    if (!buffer || bufferSize <= 0) {
        return false;
    }

    int len = textLen ? *textLen : static_cast<int>(std::strlen(buffer));
    len = std::clamp(len, 0, bufferSize - 1);
    std::string currentDisplay(buffer, buffer + len);
    adjustPromptSoftWrapsForEdit(displayText, currentDisplay, softWraps);

    int rawCursor = cursorPos ? promptDisplayPosToRawPos(*cursorPos, softWraps) : 0;
    int rawSelectionStart = selectionStart ? promptDisplayPosToRawPos(*selectionStart, softWraps) : 0;
    int rawSelectionEnd = selectionEnd ? promptDisplayPosToRawPos(*selectionEnd, softWraps) : 0;

    std::string rawText = stripPromptSoftWraps(currentDisplay.c_str(),
                                               static_cast<int>(currentDisplay.size()),
                                               softWraps);
    PromptWrapResult wrapped = rewrapPromptText(rawText, maxLineWidth,
                                                static_cast<size_t>(bufferSize - 1));

    const bool changed = wrapped.displayText != currentDisplay;
    if (changed) {
        std::memcpy(buffer, wrapped.displayText.c_str(), wrapped.displayText.size());
        buffer[wrapped.displayText.size()] = '\0';
        if (textLen) {
            *textLen = static_cast<int>(wrapped.displayText.size());
        }
        if (cursorPos) {
            *cursorPos = std::clamp(
                promptRawPosToDisplayPos(rawCursor, wrapped.softWrapRawOffsets),
                0, static_cast<int>(wrapped.displayText.size()));
        }
        if (selectionStart) {
            *selectionStart = std::clamp(
                promptRawPosToDisplayPos(rawSelectionStart, wrapped.softWrapRawOffsets),
                0, static_cast<int>(wrapped.displayText.size()));
        }
        if (selectionEnd) {
            *selectionEnd = std::clamp(
                promptRawPosToDisplayPos(rawSelectionEnd, wrapped.softWrapRawOffsets),
                0, static_cast<int>(wrapped.displayText.size()));
        }
    }

    softWraps = std::move(wrapped.softWrapDisplayOffsets);
    displayText = std::move(wrapped.displayText);
    return changed;
}

void rewrapPromptAfterContextMenuEdit(char* buffer, size_t bufferSize,
                                      int* cursorPos, int* selectionStart,
                                      int* selectionEnd, void* userData) {
    auto* context = static_cast<PromptWrapContext*>(userData);
    if (!context || !context->softWraps || !context->displayText) {
        return;
    }

    int textLen = static_cast<int>(std::strlen(buffer));
    rewrapPromptBuffer(buffer, static_cast<int>(bufferSize), context->maxLineWidth,
                       *context->softWraps, *context->displayText,
                       cursorPos, selectionStart, selectionEnd, &textLen);
}

int promptWrapCallback(ImGuiInputTextCallbackData* data) {
    auto* context = static_cast<PromptWrapContext*>(data->UserData);
    if (!context || !context->softWraps || !context->displayText) {
        return 0;
    }

    if (data->EventFlag != ImGuiInputTextFlags_CallbackEdit &&
        data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
        return 0;
    }

    // Keep the post-mention caret pinned to a raw offset while soft-wraps settle.
    const bool pinMentionCaret = context->pendingMentionRawCursor &&
                                 *context->pendingMentionRawCursor >= 0;

    if (rewrapPromptBuffer(data->Buf, data->BufSize, context->maxLineWidth,
                           *context->softWraps, *context->displayText,
                           &data->CursorPos, &data->SelectionStart,
                           &data->SelectionEnd, &data->BufTextLen)) {
        data->BufDirty = true;
    }

    if (pinMentionCaret) {
        const int displayCursor = promptRawCursorToDisplay(
            *context->pendingMentionRawCursor, data->BufTextLen, *context->softWraps);
        data->CursorPos = displayCursor;
        data->SelectionStart = displayCursor;
        data->SelectionEnd = displayCursor;
        if (context->mentionDisplayCursor) {
            *context->mentionDisplayCursor = displayCursor;
        }
    } else if (context->mentionDisplayCursor) {
        *context->mentionDisplayCursor = data->CursorPos;
    }

    // Multiline InputText uses Up/Down for caret motion, so steal those keys
    // while the @ picker is open and keep the caret put.
    if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways && context->window) {
        const int savedCursor = data->CursorPos;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) &&
            context->window->handleMentionHistoryKey(ImGuiKey_UpArrow)) {
            data->CursorPos = savedCursor;
            data->SelectionStart = savedCursor;
            data->SelectionEnd = savedCursor;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) &&
                   context->window->handleMentionHistoryKey(ImGuiKey_DownArrow)) {
            data->CursorPos = savedCursor;
            data->SelectionStart = savedCursor;
            data->SelectionEnd = savedCursor;
        }
        if (!pinMentionCaret && context->mentionDisplayCursor) {
            *context->mentionDisplayCursor = data->CursorPos;
        }
    }

    return 0;
}

const char* kApprovalLabels[] = {"Preview then approve", "Auto-run read-only", "Full agent"};
const ai::ApprovalMode kApprovalValues[] = {
    ai::ApprovalMode::PreviewThenApprove,
    ai::ApprovalMode::SafeAutoRun,
    ai::ApprovalMode::FullAgent
};
constexpr int kApprovalCount = IM_ARRAYSIZE(kApprovalValues);

int approvalToIndex(ai::ApprovalMode mode) {
    for (int i = 0; i < kApprovalCount; ++i) {
        if (kApprovalValues[i] == mode) return i;
    }
    return 0;
}

const char* approvalDisplayName(ai::ApprovalMode mode) {
    return kApprovalLabels[approvalToIndex(mode)];
}

std::string modelDisplayName(const ai::Settings& settings, const ai::ModelCatalog& catalog) {
    const std::string account = ai::accountKey(settings);
    if (ai::SecretStore::configuredAccounts(settings).empty()) {
        return "No model";
    }
    if (ai::accountRequiresApiKey(settings.provider) && !ai::SecretStore::hasApiKey(account)) {
        return "Select model";
    }

    std::string current = settings.model.empty()
        ? ai::defaultModelForProvider(settings.provider)
        : settings.model;
    if (current.empty()) {
        return "Select model";
    }
    for (const auto& model : catalog.models(account)) {
        if (current == model.id) {
            return model.label;
        }
    }
    return ai::ModelCatalog::humanizeModelId(current);
}

std::string compactTitleText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool lastSpace = false;
    for (unsigned char c : text) {
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            if (!lastSpace && !out.empty()) {
                out.push_back(' ');
                lastSpace = true;
            }
        } else {
            out.push_back(static_cast<char>(c));
            lastSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string toolFailureDetail(const std::string& content) {
    const size_t lineEnd = content.find_first_of("\r\n");
    std::string detail = content.substr(0, lineEnd);
    constexpr const char* kErrorPrefix = "Error: ";
    if (detail.rfind(kErrorPrefix, 0) == 0) {
        detail.erase(0, std::strlen(kErrorPrefix));
    }
    detail = compactTitleText(detail);

    constexpr size_t kMaxDetailBytes = 240;
    if (detail.size() > kMaxDetailBytes) {
        size_t end = kMaxDetailBytes;
        while (end > 0 && isUtf8Continuation(detail[end])) {
            --end;
        }
        detail.resize(end);
        detail += "...";
    }
    return detail;
}

std::string conversationTitleFromMessages(const std::vector<ai::ChatMessage>& messages) {
    for (const auto& message : messages) {
        if (message.role != ai::ChatRole::User) {
            continue;
        }
        std::string title = compactTitleText(message.content);
        if (title.empty() && !message.attachments.empty()) {
            title = message.attachments.front().name;
        }
        if (title.empty()) {
            continue;
        }
        constexpr size_t kMaxTitleChars = 72;
        if (title.size() > kMaxTitleChars) {
            title.resize(kMaxTitleChars);
            title += "...";
        }
        return title;
    }
    return messages.empty() ? "New conversation" : "AI conversation";
}

std::string elideToWidth(std::string text, float width) {
    if (width <= 0.0f) {
        return {};
    }
    if (ImGui::CalcTextSize(text.c_str()).x <= width) {
        return text;
    }
    const std::string suffix = "...";
    while (!text.empty()) {
        text.pop_back();
        std::string candidate = text + suffix;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= width) {
            return candidate;
        }
    }
    return suffix;
}

int64_t currentEpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void constrainNextPopupToViewport(float minWidth, float maxWidth, float maxHeightFraction) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float viewportMaxWidth = std::max(120.0f, viewport->WorkSize.x - 16.0f);
    float popupMaxWidth = std::min(maxWidth, viewportMaxWidth);
    float popupMinWidth = std::min(minWidth, popupMaxWidth);
    float popupMaxHeight = std::max(ImGui::GetFrameHeightWithSpacing() * 8.0f,
                                    viewport->WorkSize.y * maxHeightFraction);

    ImGui::SetNextWindowSizeConstraints(ImVec2(popupMinWidth, 0.0f),
                                        ImVec2(popupMaxWidth, popupMaxHeight));
}

void keepCurrentWindowInsideViewport() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();
    ImVec2 workMin = viewport->WorkPos;
    ImVec2 workMax(viewport->WorkPos.x + viewport->WorkSize.x,
                   viewport->WorkPos.y + viewport->WorkSize.y);

    if (pos.x + size.x > workMax.x) pos.x = workMax.x - size.x;
    if (pos.y + size.y > workMax.y) pos.y = workMax.y - size.y;
    if (pos.x < workMin.x) pos.x = workMin.x;
    if (pos.y < workMin.y) pos.y = workMin.y;

    ImGui::SetWindowPos(pos, ImGuiCond_Always);
}

std::string compactRelativeTime(int64_t updatedAt, int64_t now) {
    if (updatedAt <= 0) {
        return "";
    }

    int64_t seconds = std::max<int64_t>(0, now - updatedAt);
    if (seconds < 60) return "now";

    int64_t minutes = seconds / 60;
    if (minutes < 60) return std::to_string(minutes) + "m";

    int64_t hours = minutes / 60;
    if (hours < 24) return std::to_string(hours) + "h";

    int64_t days = hours / 24;
    if (days < 7) return std::to_string(days) + "d";

    int64_t weeks = days / 7;
    if (weeks < 5) return std::to_string(weeks) + "w";

    int64_t months = days / 30;
    if (months < 12) return std::to_string(std::max<int64_t>(1, months)) + "mo";

    int64_t years = days / 365;
    return std::to_string(std::max<int64_t>(1, years)) + "y";
}

// Light Markdown pass for assistant text: turns "- "/"* "/"+ " bullets into a
// bullet glyph and renders fenced ``` code blocks ``` in a monospace font. Other
// Markdown (bold, headings, inline code) is left as plain text on purpose.
void appendMarkdownParagraphs(const std::string& content, ImU32 textColor, ImU32 codeColor,
                              ImFont* codeFont,
                              std::vector<SelectableTextView::Paragraph>& out) {
    const size_t n = content.size();
    size_t pos = 0;
    bool inCode = false;

    while (pos <= n) {
        size_t nl = content.find('\n', pos);
        std::string line = content.substr(pos, (nl == std::string::npos ? n : nl) - pos);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        size_t firstNonSpace = line.find_first_not_of(" \t");
        bool isFence = (firstNonSpace != std::string::npos &&
                        line.compare(firstNonSpace, 3, "```") == 0);

        if (isFence) {
            inCode = !inCode; // swallow the fence line itself
        } else if (inCode) {
            out.push_back({line, codeColor, codeFont});
        } else {
            // Base indent so list items sit in from the message text. Existing
            // leading whitespace (nested lists) is preserved on top of it.
            static const std::string kListIndent = "    ";
            size_t i = (firstNonSpace == std::string::npos) ? line.size() : firstNonSpace;

            bool bullet = (i < line.size()) &&
                          (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
                          (i + 1 < line.size()) && (line[i + 1] == ' ' || line[i + 1] == '\t');

            bool ordered = false;
            if (!bullet && i < line.size() && line[i] >= '0' && line[i] <= '9') {
                size_t j = i;
                while (j < line.size() && line[j] >= '0' && line[j] <= '9') ++j;
                ordered = (j < line.size()) && (line[j] == '.' || line[j] == ')') &&
                          (j + 1 < line.size()) && (line[j + 1] == ' ' || line[j + 1] == '\t');
            }

            if (bullet) {
                size_t rest = i + 1;
                while (rest < line.size() && (line[rest] == ' ' || line[rest] == '\t')) ++rest;
                out.push_back({kListIndent + line.substr(0, i) + "\xE2\x80\xA2 " + line.substr(rest),
                               textColor, nullptr});
            } else if (ordered) {
                out.push_back({kListIndent + line, textColor, nullptr});
            } else {
                out.push_back({line, textColor, nullptr});
            }
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

} // namespace

AiChatWindow::AiChatWindow(Project* project, ResourcesWindow* resourcesWindow)
    : project(project)
    , resourcesWindow(resourcesWindow)
    , conversationStore(project) {
    service.setSettings(AppSettings::getAiSettings());
    prefetchModels();
}

void AiChatWindow::setWakeCallback(std::function<void()> callback) {
    service.setWakeCallback(std::move(callback));
}

void AiChatWindow::reloadSettings() {
    service.setSettings(AppSettings::getAiSettings());
}

void AiChatWindow::shutdown() {
    service.shutdown();
}

void AiChatWindow::prefetchModels() {
    // Kick off a background /models fetch for every configured account at app
    // open, so the picker is already populated by the time it is opened.
    const ai::Settings settings = AppSettings::getAiSettings();
    for (const ai::ProviderAccount& account : ai::SecretStore::configuredAccounts(settings)) {
        modelCatalog.refresh(account, ai::SecretStore::getApiKey(account.id));
    }
}

const std::vector<ai::ChatMessage>& AiChatWindow::cachedMessages() {
    const uint64_t revision = service.getRevision();
    if (revision != messagesCacheRevision) {
        messagesCache = service.getMessages();
        messagesCacheRevision = revision;
    }
    return messagesCache;
}

void AiChatWindow::update() {
    if (!syncedInitialSettings) {
        service.setSettings(AppSettings::getAiSettings());
        syncedInitialSettings = true;
    }
    loadLatestConversationForCurrentProject();

    // Drive the agent loop even when the panel is closed/collapsed or the OS
    // window is minimized: auto-run eligible proposals, continue completed
    // tool results, and wake rate-limit retries when their delay expires.
    if (!service.isBusy()) {
        autoRunProposals();
    }
    service.update();
    if (!service.isBusy()) {
        persistConversation();
    }
    updateMessageNotification();
}

void AiChatWindow::show() {
    // Always pump first so in-flight requests keep progressing when the tab is
    // hidden, collapsed, or the editor window is not focused.
    update();

    if (!windowOpen) {
        isWindowVisible = false;
        return;
    }

    if (focusRequested) {
        ImGui::SetNextWindowFocus();
        focusRequested = false;
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (hasNotification) {
        App::pushTabNotificationStyle();
        windowFlags |= ImGuiWindowFlags_UnsavedDocument;
    }

    if (!ImGui::Begin(WINDOW_NAME, &windowOpen, windowFlags)) {
        if (hasNotification) App::popTabNotificationStyle();
        windowFocused = false;
        isWindowVisible = false;
        ImGui::End();
        return;
    }
    if (hasNotification) App::popTabNotificationStyle();

    isWindowVisible = true;
    hasNotification = false;
    windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    drawHeader();
    ImGui::Separator();

    const ImGuiStyle& style = ImGui::GetStyle();
    float inputHeight = ImGui::GetTextLineHeight() * 3.0f + style.FramePadding.y * 2.0f;
    float controlsHeight = ImGui::GetFrameHeight();
    float attachmentsHeight = pendingAttachments.empty()
        ? 0.0f
        : attachmentRowHeight(pendingAttachments, ImGui::GetContentRegionAvail().x) +
          style.ItemSpacing.y;
    float composerHeight = inputHeight + controlsHeight + attachmentsHeight +
                           style.ItemSpacing.y + 2.0f;

    std::vector<ai::ActionProposal> proposals = service.getProposals();
    int pending = 0;
    for (const auto& proposal : proposals) {
        if (!proposal.executed) ++pending;
    }

    float availY = ImGui::GetContentRegionAvail().y;
    float trayHeight = 0.0f;
    if (pending > 0) {
        float cardHeight = ImGui::GetFrameHeightWithSpacing()
                         + ImGui::GetTextLineHeightWithSpacing() * 2.0f + 14.0f;
        trayHeight = std::min(static_cast<float>(pending), 3.0f) * cardHeight;
        trayHeight = std::min(trayHeight, availY * 0.5f);
    }

    float transcriptHeight = availY - composerHeight - trayHeight
                           - (trayHeight > 0.0f ? style.ItemSpacing.y : 0.0f);
    if (transcriptHeight < 60.0f) {
        transcriptHeight = 60.0f;
    }

    drawTranscript(transcriptHeight);
    if (pending > 0) {
        drawProposals(proposals, trayHeight);
    }
    drawComposer(inputHeight);

    ImGui::End();
}

void AiChatWindow::setOpen(bool open) {
    if (open && !windowOpen) {
        focusRequested = true;
    }
    if (!open) {
        isWindowVisible = false;
        windowFocused = false;
    }
    windowOpen = open;
}

bool AiChatWindow::isOpen() const {
    return windowOpen;
}

bool AiChatWindow::isFocused() const {
    return windowFocused;
}

void AiChatWindow::drawHeader() {
    const std::vector<ai::ChatMessage>& messages = cachedMessages();
    const ImGuiStyle& style = ImGui::GetStyle();

    float height = ImGui::GetFrameHeight();
    ImVec2 buttonSize(height, height);
    float rightEdge = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    float buttonsWidth = height * 3.0f + style.ItemSpacing.x * 2.0f;
    float titleWidth = std::max(20.0f, ImGui::GetContentRegionAvail().x - buttonsWidth - style.ItemSpacing.x);

    ImGui::AlignTextToFramePadding();
    std::string title = elideToWidth(std::string(ICON_FA_MESSAGE "  ") +
                                     conversationTitleFromMessages(messages), titleWidth);
    ImGui::TextDisabled("%s", title.c_str());

    ImGui::SameLine();
    ImGui::SetCursorPosX(rightEdge - buttonsWidth);
    ImGui::BeginDisabled(service.isBusy());
    if (Widgets::iconButton("##AiNewChat", ICON_FA_PLUS, buttonSize)) {
        startNewChat();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("New chat");

    ImGui::SameLine();
    if (Widgets::iconButton("##AiHistory", ICON_FA_CLOCK_ROTATE_LEFT, buttonSize)) {
        ImGui::OpenPopup("AI History##AiHistoryPopup");
    }
    ImGui::SetItemTooltip("History");
    drawHistoryPopup();

    ImGui::SameLine();
    if (Widgets::iconButton("##AiSettings", ICON_FA_GEAR, buttonSize)) {
        Backend::getApp().openEditorSettings(EditorSettingsWindow::Tab::AI, true);
    }
    ImGui::SetItemTooltip("AI settings");
}

void AiChatWindow::drawHistoryPopup() {
    if (!ImGui::BeginPopup("AI History##AiHistoryPopup")) {
        pendingDeleteId.clear();
        return;
    }

    if (ImGui::IsWindowAppearing()) {
        historyCache = conversationStore.list();
        pendingDeleteId.clear();
    }

    ImGui::TextDisabled(ICON_FA_COMMENTS "  Conversations");
    ImGui::Separator();

    if (historyCache.empty()) {
        ImGui::TextDisabled("No saved conversations yet");
        ImGui::EndPopup();
        return;
    }

    const bool busy = service.isBusy();
    float trashWidth = ImGui::GetFrameHeight();
    float ageWidth = ImGui::CalcTextSize("999mo").x;
    float rowWidth = 360.0f;
    int64_t now = currentEpochSeconds();
    std::string toDelete;

    for (const auto& meta : historyCache) {
        ImGui::PushID(meta.id.c_str());
        bool isCurrent = meta.id == currentConversationId;

        const ImGuiStyle& style = ImGui::GetStyle();
        float titleWidth = rowWidth - trashWidth - ageWidth - style.ItemSpacing.x * 2.0f;
        std::string label = elideToWidth(meta.title.empty() ? "Conversation" : meta.title,
                                         titleWidth);
        ImGui::BeginDisabled(busy);
        if (ImGui::Selectable(label.c_str(), isCurrent,
                              ImGuiSelectableFlags_AllowOverlap, ImVec2(titleWidth, 0))) {
            loadConversation(meta.id);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        if (busy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Wait for the current reply to finish");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", compactRelativeTime(meta.updatedAt, now).c_str());
        ImGui::SameLine();

        // Deleting the active conversation while a reply is in flight is blocked:
        // the in-memory chat cannot be cleared then, and the next persist would
        // silently resurrect the file under a new id.
        const bool deleteBlocked = busy && isCurrent;
        const bool armed = pendingDeleteId == meta.id;
        ImGui::BeginDisabled(deleteBlocked);
        if (armed) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
        if (Widgets::iconButton("##DeleteConversation", ICON_FA_TRASH,
                                ImVec2(trashWidth, ImGui::GetFrameHeight()))) {
            if (armed) {
                toDelete = meta.id;
            } else {
                pendingDeleteId = meta.id;
            }
        }
        if (armed) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (deleteBlocked) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Wait for the current reply to finish");
            }
        } else {
            ImGui::SetItemTooltip(armed ? "Click again to permanently delete"
                                        : "Delete conversation");
        }
        ImGui::PopID();
    }

    if (!toDelete.empty()) {
        conversationStore.remove(toDelete);
        pendingDeleteId.clear();
        historyCache = conversationStore.list();
        if (toDelete == currentConversationId) {
            service.clearConversation();
            currentConversationId.clear();
            lastSavedMessageCount = 0;
            lastObservedMessageCount = 0;
            hasNotification = false;
        }
    }

    ImGui::EndPopup();
}

void AiChatWindow::drawTranscript(float height) {
    const std::vector<ai::ChatMessage>& messages = cachedMessages();
    bool busy = service.isBusy();

    if (messages.empty() && !busy) {
        ImGui::BeginChild("##AiEmpty", ImVec2(0, height), 0, ImGuiWindowFlags_NoNav);
        ImGui::Spacing();
        ImGui::TextDisabled("Ask about your project, scene, or resources,");
        ImGui::TextDisabled("or request an editor action such as");
        ImGui::TextDisabled("\"add a point light above the player\".");
        if (ai::SecretStore::configuredAccounts(service.getSettings()).empty()) {
            ImGui::Spacing();
            if (ImGui::Button(ICON_FA_KEY " Set API key")) {
                Backend::getApp().openEditorSettings(EditorSettingsWindow::Tab::AI, true);
            }
        }
        ImGui::EndChild();
        return;
    }

    const ImU32 userCol = ImGui::GetColorU32(kUserColor);
    const ImU32 assistantCol = ImGui::GetColorU32(kAssistantColor);
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dimCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 okCol = ImGui::GetColorU32(kSuccessColor);
    const ImU32 errCol = ImGui::GetColorU32(kErrorColor);
    const ImU32 codeCol = ImGui::GetColorU32(kCodeColor);
    const ImU32 mentionCol = ImGui::GetColorU32(ImGuiCol_TextLink);
    ImFont* codeFont = App::getCodeFont();

    std::vector<SelectableTextView::Paragraph> paragraphs;
    bool first = true;
    auto spacer = [&]() {
        if (!first) paragraphs.push_back({"", textCol});
        first = false;
    };

    // Divider shown when a user message was sent with a different model than the
    // one before it (never before the conversation's first message).
    std::string lastModel;

    for (const auto& message : messages) {
        switch (message.role) {
            case ai::ChatRole::System:
                break;
            case ai::ChatRole::User:
                if (!message.model.empty()) {
                    if (!lastModel.empty() && lastModel != message.model) {
                        spacer();
                        paragraphs.push_back({ai::ModelCatalog::humanizeModelId(message.model),
                                              dimCol, nullptr, true});
                    }
                    lastModel = message.model;
                }
                spacer();
                paragraphs.push_back({"You", userCol});
                if (!message.content.empty()) {
                    paragraphs.push_back(mentionColoredParagraph(message.content, textCol,
                                                                  mentionCol));
                }
                for (const ai::ChatAttachment& attachment : message.attachments) {
                    paragraphs.push_back({
                        std::string(attachment.isImage() ? ICON_FA_FILE_IMAGE
                                                         : ICON_FA_FILE_LINES) +
                            "  " + attachment.name,
                        dimCol
                    });
                }
                break;
            case ai::ChatRole::Assistant:
                if (!message.content.empty()) {
                    spacer();
                    paragraphs.push_back({"Assistant", assistantCol});
                    appendMarkdownParagraphs(message.content, textCol, codeCol, codeFont, paragraphs);
                }
                break;
            case ai::ChatRole::Tool: {
                spacer();
                std::string label = message.toolDescription;
                if (label.empty() && !message.toolName.empty()) {
                    label = ai::EditorActionRegistry::describe(message.toolName, ai::Json::object());
                }
                if (label.empty()) {
                    label = "Action";
                }
                std::string header = std::string(message.toolSuccess ? ICON_FA_CIRCLE_CHECK
                                                                      : ICON_FA_CIRCLE_XMARK)
                                   + "  " + label;
                paragraphs.push_back({header, message.toolSuccess ? okCol : errCol});
                if (!message.toolSuccess) {
                    const std::string detail = toolFailureDetail(message.content);
                    if (!detail.empty()) {
                        paragraphs.push_back({detail, errCol});
                    }
                }
                break;
            }
        }
    }

    if (busy) {
        spacer();
        paragraphs.push_back({
            std::string(ICON_FA_SPINNER) + "  " + service.getActivityText(),
            dimCol
        });
    }

    // Tighten transcript line spacing only; keep default ItemSpacing for the
    // right-click menu (same pattern as OutputWindow).
    const ImGuiStyle& style = ImGui::GetStyle();
    const float transcriptLineSpacing = std::max(2.0f, style.ItemSpacing.y * 0.4f);
    transcript.draw("##AiTranscript", ImVec2(0, height), paragraphs, scrollToBottom,
                    transcriptLineSpacing);
    scrollToBottom = false;
}

void AiChatWindow::drawProposals(const std::vector<ai::ActionProposal>& proposals, float height) {
    ImGui::BeginChild("##AiProposals", ImVec2(0, height), ImGuiChildFlags_Borders);

    for (const auto& proposal : proposals) {
        if (proposal.executed) {
            continue;
        }
        ImGui::PushID(static_cast<int>(proposal.id));
        ImGui::TextWrapped(ICON_FA_WRENCH "  %s", proposal.description.c_str());

        if (proposal.executing) {
            ImGui::TextDisabled("Running...");
        } else {
            if (ImGui::Button(ICON_FA_CHECK " Approve")) {
                executeProposal(proposal.id);
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK " Dismiss")) {
                service.removeProposal(proposal.id);
                scrollToBottom = true;
            }
        }
        ImGui::Spacing();
        ImGui::PopID();
    }

    ImGui::EndChild();
}

void AiChatWindow::drawPendingAttachments() {
    if (pendingAttachments.empty()) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float rowHeight = attachmentRowHeight(
        pendingAttachments, ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##AiAttachments", ImVec2(0.0f, rowHeight), false,
                      ImGuiWindowFlags_HorizontalScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    ImFont* attachmentFont = ImGui::GetFont();
    const float attachmentFontSize =
        std::floor(ImGui::GetFontSize() * kAttachmentFontScale);
    const float chipHeight = attachmentChipHeight();
    // PushFont takes an unscaled size
    ImGui::PushFont(attachmentFont,
                    attachmentFontSize * ImGui::GetStyle().FontSizeBase / ImGui::GetFontSize());

    int removeIndex = -1;
    for (int i = 0; i < static_cast<int>(pendingAttachments.size()); ++i) {
        if (i > 0) ImGui::SameLine();
        const ai::ChatAttachment& attachment = pendingAttachments[static_cast<size_t>(i)];
        const std::string label = attachmentChipLabel(attachment);
        const ImVec2 textSize = ImGui::CalcTextSize(label.data(), label.data() + label.size());
        const ImVec2 size(textSize.x + 10.0f, chipHeight);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##Attachment", size)) {
            removeIndex = i;
        }
        const ImU32 background = ImGui::GetColorU32(
            ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                                  background, style.FrameRounding);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + style.FramePadding.x,
                   pos.y + (size.y - textSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text), label.data(), label.data() + label.size());
        ImGui::SetItemTooltip("Remove %s", attachment.name.c_str());
        ImGui::PopID();
    }

    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (removeIndex >= 0) {
        pendingAttachments.erase(pendingAttachments.begin() + removeIndex);
    }
}

void AiChatWindow::drawComposer(float inputHeight) {
    ImGui::Separator();

    drawPendingAttachments();

    const ImGuiStyle& style = ImGui::GetStyle();
    float buttonWidth = ImGui::GetFrameHeight();
    float inputWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x -
        buttonWidth * 2.0f - style.ItemSpacing.x);

    bool submitted = false;
    if (refocusInput) {
        ImGui::SetKeyboardFocusHere();
        refocusInput = false;
    }

    // Resolve the input id before drawing so we can claim Enter/Tab while the
    // @ picker is open (otherwise multiline InputText validates and clears focus).
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const ImGuiID inputId = window->GetID("##AiInput");
    mentionInputId = inputId;
    const bool attachmentPaste = handleAttachmentPaste(inputId);

    if (mention.open && !mention.items.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            applySelectedMention();
            ImGui::SetKeyOwner(ImGuiKey_Enter, ImGuiKeyOwner_Any, ImGuiInputFlags_LockThisFrame);
            ImGui::SetKeyOwner(ImGuiKey_KeypadEnter, ImGuiKeyOwner_Any, ImGuiInputFlags_LockThisFrame);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            applySelectedMention();
            ImGui::SetKeyOwner(ImGuiKey_Tab, ImGuiKeyOwner_Any, ImGuiInputFlags_LockThisFrame);
        }
    }

    PromptWrapContext wrapContext{
        std::max(0.0f, inputWidth - style.FramePadding.x * 2.0f),
        &inputSoftWraps,
        &inputDisplayText,
        &mentionDisplayCursor,
        &pendingMentionRawCursor,
        this
    };
    // Hide native InputText glyphs so we can redraw with mention coloring. The
    // prompt already communicates focus through its caret, so omit ImGui's
    // navigation rectangle around the otherwise flush composer controls.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    if (ImGui::InputTextMultiline("##AiInput", inputBuffer.data(), inputBuffer.size(),
                                  ImVec2(inputWidth, inputHeight),
                                  ImGuiInputTextFlags_EnterReturnsTrue |
                                  ImGuiInputTextFlags_CtrlEnterForNewLine |
                                  ImGuiInputTextFlags_NoHorizontalScroll |
                                  ImGuiInputTextFlags_CallbackEdit |
                                  ImGuiInputTextFlags_CallbackAlways |
                                  (attachmentPaste ? ImGuiInputTextFlags_ReadOnly : 0),
                                  promptWrapCallback, &wrapContext)) {
        // Enter submits only when the mention popup is closed; otherwise it
        // would steal the key from applySelectedMention.
        if (!mention.open && pendingMentionRawCursor < 0) {
            submitted = true;
        }
    }
    ImGui::PopStyleColor(2);
    bool inputActive = ImGui::IsItemActive();
    bool inputFocused = ImGui::IsItemFocused();
    ImVec2 inputMin = ImGui::GetItemRectMin();
    ImVec2 inputMax = ImGui::GetItemRectMax();

    handlePromptDrop(wrapContext.maxLineWidth);

    // Restore caret after an @ insertion using the raw offset so soft-wrap
    // reflows during refocus don't leave the caret mid-glyph.
    if (pendingMentionRawCursor >= 0) {
        if (!inputActive) {
            refocusInput = true;
        }
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(inputId)) {
            const int displayLength = state->WantReloadUserBuf
                ? static_cast<int>(inputDisplayText.size())
                : state->TextLen;
            const int cursor = promptRawCursorToDisplay(
                pendingMentionRawCursor, displayLength, inputSoftWraps);
            if (state->WantReloadUserBuf) {
                state->ReloadSelectionStart = cursor;
                state->ReloadSelectionEnd = cursor;
            } else if (inputActive) {
                state->SetSelection(cursor, cursor);
                state->CursorAnimReset();
                state->CursorFollow = true;
                pendingMentionRawCursor = -1;
                mentionDisplayCursor = cursor;
            }
        }
    }

    drawPromptMentionOverlay(inputMin, inputMax, inputId);

    InputTextContextMenu::Options inputMenuOptions;
    inputMenuOptions.userData = &wrapContext;
    inputMenuOptions.copyRange = copyPromptContextMenuRange;
    inputMenuOptions.afterEdit = rewrapPromptAfterContextMenuEdit;
    InputTextContextMenu::drawForLastItem(inputBuffer.data(), inputBuffer.size(),
                                          inputMenuOptions);

    if (!inputActive && !submitted && pendingMentionRawCursor < 0) {
        rewrapPromptBuffer(inputBuffer.data(), static_cast<int>(inputBuffer.size()),
                           wrapContext.maxLineWidth, inputSoftWraps, inputDisplayText);
    }

    if (inputFocused || inputActive || mention.open || pendingMentionRawCursor >= 0) {
        updateMentionFromInput(mentionDisplayCursor, inputMin.x, inputMin.y, inputMax.y,
                               style.FramePadding.x, wrapContext.maxLineWidth);
        if (handleMentionKeys()) {
            submitted = false;
        }
    } else {
        closeMentionPopup();
    }

    if (submitted) {
        submitMessage();
        submitted = false;
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_FrameBg]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_FrameBgHovered]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_FrameBgActive]);
    if (Widgets::iconButton("##AiAttach", ICON_FA_PAPERCLIP,
                            ImVec2(buttonWidth, inputHeight))) {
        attachExternalFiles(FileDialogs::openFileDialogMultiple());
    }
    ImGui::PopStyleColor(3);
    ImGui::SetItemTooltip("Attach external files (or paste copied files with Ctrl+V)");

    ImGui::SameLine();
    if (service.isBusy()) {
        if (Widgets::iconButton("##AiStop", ICON_FA_STOP, ImVec2(buttonWidth, inputHeight))) {
            service.cancel();
        }
        ImGui::SetItemTooltip("Stop");
    } else {
        std::string promptText = stripPromptSoftWraps(
            inputBuffer.data(), static_cast<int>(std::strlen(inputBuffer.data())),
            inputSoftWraps);
        bool hasContent = !promptText.empty() || !pendingAttachments.empty();
        std::string disabledReason;
        bool canSend = hasContent && canSendMessage(&disabledReason);
        ImGui::BeginDisabled(!canSend);
        if (Widgets::iconButton("##AiSend", ICON_FA_PAPER_PLANE, ImVec2(buttonWidth, inputHeight))) {
            submitted = true;
        }
        ImGui::EndDisabled();
        if (hasContent && !disabledReason.empty()) {
            ImGui::SetItemTooltip("%s", disabledReason.c_str());
        } else {
            ImGui::SetItemTooltip("Send  (Enter - Ctrl+Enter for newline)");
        }
    }

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - style.ItemSpacing.y);
    drawComposerControls();
    drawMentionPopup();

    if (submitted) {
        submitMessage();
    }
}

void AiChatWindow::closeMentionPopup() {
    mention = MentionState{};
}

void AiChatWindow::drawPromptMentionOverlay(ImVec2 inputMin, ImVec2 inputMax, ImGuiID inputId) {
    if (inputDisplayText.empty()) {
        inputDisplayText.assign(inputBuffer.data());
    }
    const char* text = inputDisplayText.c_str();
    const int textLen = static_cast<int>(inputDisplayText.size());
    if (textLen <= 0) {
        return;
    }

    const std::vector<std::pair<int, int>> spans = findMentionSpans(
        stripPromptSoftWraps(text, textLen, inputSoftWraps));

    const ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 scroll(0.0f, 0.0f);
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(inputId)) {
        scroll = state->Scroll;
    }

    // Match InputTextEx multiline origin: frame + border + FramePadding.
    const float border = style.FrameBorderSize;
    const ImVec2 textOrigin(inputMin.x + border + style.FramePadding.x,
                            inputMin.y + border + style.FramePadding.y);
    const float lineHeight = ImGui::GetFontSize();
    const float lineBaseX = textOrigin.x - scroll.x;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(
        ImVec2(inputMin.x + border, inputMin.y + border),
        ImVec2(inputMax.x - border, inputMax.y - border),
        true);

    const ImU32 normalColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 mentionColor = ImGui::GetColorU32(ImGuiCol_TextLink);

    int i = 0;
    int lineStart = 0;
    float lineY = textOrigin.y - scroll.y;
    while (i < textLen) {
        if (text[i] == '\n') {
            ++i;
            lineStart = i;
            lineY += lineHeight;
            continue;
        }

        const int runStart = i;
        const bool isMention = positionInMentionSpan(
            promptDisplayPosToRawPos(runStart, inputSoftWraps), spans);
        while (i < textLen && text[i] != '\n') {
            if (positionInMentionSpan(promptDisplayPosToRawPos(i, inputSoftWraps), spans) !=
                isMention) {
                break;
            }
            ++i;
        }

        // Measure from line start (InputText caret math). Advancing a truncated
        // pen per run drifts left across multiple @mentions.
        const float runX = lineBaseX + ImGui::CalcTextSize(text + lineStart, text + runStart).x;
        draw->AddText(ImVec2(IM_TRUNC(runX), IM_TRUNC(lineY)),
                      isMention ? mentionColor : normalColor,
                      text + runStart, text + i);
    }

    draw->PopClipRect();
}

namespace {

bool mentionMatch(const std::string& candidate, const std::string& query, int& outScore) {
    outScore = 0;
    if (query.empty()) {
        outScore = 1;
        return true;
    }

    auto toLower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };

    const std::string cand = toLower(candidate);
    const std::string q = toLower(query);
    if (q.size() > cand.size()) {
        return false;
    }
    if (cand.compare(0, q.size(), q) == 0) {
        outScore = 2000;
        return true;
    }

    size_t found = cand.find(q);
    if (found != std::string::npos) {
        outScore = 1200 - static_cast<int>(std::min<size_t>(found, 100));
        return true;
    }

    size_t qi = 0;
    size_t last = std::string::npos;
    for (size_t i = 0; i < cand.size() && qi < q.size(); ++i) {
        if (cand[i] != q[qi]) continue;
        bool consecutive = last != std::string::npos && i == last + 1;
        bool boundary = i == 0 || candidate[i - 1] == '/' || candidate[i - 1] == '_' ||
                        candidate[i - 1] == '-' || candidate[i - 1] == ' ' ||
                        (std::isupper(static_cast<unsigned char>(candidate[i])) &&
                         !std::isupper(static_cast<unsigned char>(candidate[i - 1])));
        if (qi == 0 && i != 0 && !boundary) break;
        if (!consecutive && !boundary && qi > 0) continue;
        last = i;
        ++qi;
    }
    if (qi == q.size()) {
        outScore = 800;
        return true;
    }
    return false;
}

} // namespace

const char* AiChatWindow::mentionKindIcon(MentionKind kind) {
    switch (kind) {
        case MentionKind::Selection: return ICON_FA_CROSSHAIRS;
        case MentionKind::Project: return ICON_FA_FOLDER;
        case MentionKind::Scene: return ICON_FA_CLAPPERBOARD;
        case MentionKind::Entity: return ICON_FA_CUBE;
        case MentionKind::OpenFile: return ICON_FA_CODE;
        case MentionKind::File: return ICON_FA_FILE;
    }
    return ICON_FA_AT;
}

const char* AiChatWindow::mentionKindLabel(MentionKind kind) {
    switch (kind) {
        case MentionKind::Selection: return "Selection";
        case MentionKind::Project: return "Project";
        case MentionKind::Scene: return "Scene";
        case MentionKind::Entity: return "Entity";
        case MentionKind::OpenFile: return "Open file";
        case MentionKind::File: return "File";
    }
    return "Mention";
}

int AiChatWindow::mentionKindBoost(MentionKind kind) {
    switch (kind) {
        case MentionKind::Selection: return 80;
        case MentionKind::Project: return 70;
        case MentionKind::Scene: return 40;
        case MentionKind::Entity: return 30;
        case MentionKind::OpenFile: return 50;
        case MentionKind::File: return 10;
    }
    return 0;
}

void AiChatWindow::collectMentionCandidates(std::vector<MentionItem>& out) const {
    out.clear();
    if (!project) {
        return;
    }

    auto addItem = [&](MentionKind kind, std::string label, std::string insertText,
                       std::string detail) {
        if (label.empty() || insertText.empty()) {
            return;
        }
        out.push_back({kind, std::move(label), std::move(insertText), std::move(detail), 0});
    };

    addItem(MentionKind::Project, "project", "@project", "Current project");

    SceneProject* selectedScene = project->getSelectedScene();
    if (selectedScene) {
        std::vector<Entity> selected = project->getSelectedEntities(selectedScene->id);
        if (!selected.empty()) {
            std::string detail = selected.size() == 1
                ? selectedScene->scene->getEntityName(selected.front())
                : std::to_string(selected.size()) + " entities";
            addItem(MentionKind::Selection, "selection", "@selection", detail);
        }
    }

    for (const SceneProject& sceneProject : project->getScenes()) {
        std::string detail = sceneProject.filepath.empty()
            ? "Scene"
            : sceneProject.filepath.generic_string();
        addItem(MentionKind::Scene, sceneProject.name,
                formatMentionInsert(sceneProject.name), detail);
    }

    if (selectedScene && selectedScene->scene) {
        for (Entity entity : selectedScene->entities) {
            std::string name = selectedScene->scene->getEntityName(entity);
            if (name.empty()) {
                name = "Entity " + std::to_string(static_cast<unsigned>(entity));
            }
            addItem(MentionKind::Entity, name, formatMentionInsert(name),
                    "Entity #" + std::to_string(static_cast<unsigned>(entity)));
        }
    }

    if (CodeEditor* codeEditor = Backend::getApp().getCodeEditor()) {
        for (const fs::path& path : codeEditor->getOpenPaths()) {
            std::string rel = path.generic_string();
            addItem(MentionKind::OpenFile, rel, formatMentionInsert(rel), "Open in editor");
        }
    }

    const fs::path projectPath = project->getProjectPath();
    if (!projectPath.empty() && fs::exists(projectPath)) {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(projectPath, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (ec) break;
            const fs::path& path = it->path();
            if (!FileUtils::isNarrowPath(path)) {
                continue;
            }
            if (path.filename().string().rfind('.', 0) == 0) {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (path.filename() == ".doriax" || path.filename() == "build") {
                if (it->is_directory(ec)) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (!it->is_regular_file(ec)) {
                continue;
            }

            const std::string pathStr = path.string();
            const bool useful =
                Util::isSceneFile(pathStr) || Util::isScriptFile(pathStr) ||
                Util::isLuaFile(pathStr) || Util::isImageFile(pathStr) ||
                Util::isMaterialFile(pathStr) || Util::isModelFile(pathStr) ||
                Util::isShaderFile(pathStr) || Util::isAudioFile(pathStr) ||
                Util::isFontFile(pathStr) || Util::isBundleFile(pathStr);
            if (!useful) {
                continue;
            }

            fs::path rel = fs::relative(path, projectPath, ec);
            if (ec || rel.empty()) {
                continue;
            }
            std::string relStr = rel.generic_string();
            addItem(MentionKind::File, relStr, formatMentionInsert(relStr), "Project file");
            if (out.size() > 2500) {
                break;
            }
        }
    }
}

void AiChatWindow::refreshMentionItems() {
    std::vector<MentionItem> candidates;
    collectMentionCandidates(candidates);

    mention.items.clear();
    for (MentionItem& item : candidates) {
        int score = 0;
        if (!mentionMatch(item.label, mention.query, score) &&
            !mentionMatch(item.insertText, mention.query, score) &&
            !mentionMatch(item.detail, mention.query, score)) {
            continue;
        }
        item.score = score + mentionKindBoost(item.kind);
        mention.items.push_back(std::move(item));
    }

    std::sort(mention.items.begin(), mention.items.end(),
              [](const MentionItem& a, const MentionItem& b) {
                  if (a.score != b.score) return a.score > b.score;
                  if (a.kind != b.kind) return static_cast<int>(a.kind) < static_cast<int>(b.kind);
                  return a.label < b.label;
              });

    constexpr size_t kMaxVisible = 40;
    if (mention.items.size() > kMaxVisible) {
        mention.items.resize(kMaxVisible);
    }

    if (mention.selectedIndex >= static_cast<int>(mention.items.size())) {
        mention.selectedIndex = std::max(0, static_cast<int>(mention.items.size()) - 1);
    }
    mention.scrollToSelected = true;
}

void AiChatWindow::updateMentionFromInput(int displayCursor, float inputMinX, float inputMinY,
                                          float inputMaxY, float framePaddingX, float maxLineWidth) {
    const std::string raw = stripPromptSoftWraps(
        inputBuffer.data(), static_cast<int>(std::strlen(inputBuffer.data())), inputSoftWraps);
    const int rawCursor = promptDisplayPosToRawPos(displayCursor, inputSoftWraps);
    const int clampedCursor = std::clamp(rawCursor, 0, static_cast<int>(raw.size()));

    int atPos = -1;
    for (int i = clampedCursor - 1; i >= 0; --i) {
        const unsigned char c = static_cast<unsigned char>(raw[static_cast<size_t>(i)]);
        if (c == '@') {
            const bool boundary = i == 0 ||
                std::isspace(static_cast<unsigned char>(raw[static_cast<size_t>(i - 1)]));
            if (boundary) {
                atPos = i;
            }
            break;
        }
        if (!isMentionTokenChar(c)) {
            break;
        }
    }

    if (atPos < 0) {
        closeMentionPopup();
        return;
    }

    const std::string query = raw.substr(static_cast<size_t>(atPos + 1),
                                         static_cast<size_t>(clampedCursor - atPos - 1));
    const bool queryChanged = !mention.open || mention.query != query || mention.atRawPos != atPos;
    mention.open = true;
    mention.atRawPos = atPos;
    mention.queryRawEnd = clampedCursor;
    mention.query = query;
    mention.inputItemMinY = inputMinY;
    mention.inputItemMaxY = inputMaxY;
    mention.maxLineWidth = maxLineWidth;

    int lineStart = 0;
    for (int i = clampedCursor - 1; i >= 0; --i) {
        if (raw[static_cast<size_t>(i)] == '\n') {
            lineStart = i + 1;
            break;
        }
    }
    const std::string linePrefix = raw.substr(static_cast<size_t>(lineStart),
                                              static_cast<size_t>(clampedCursor - lineStart));
    const float caretX = inputMinX + framePaddingX + ImGui::CalcTextSize(linePrefix.c_str()).x;
    mention.popupPos = ImVec2(caretX, inputMaxY + Theme::dpi(2.0f));

    if (queryChanged || mention.items.empty()) {
        refreshMentionItems();
        mention.selectedIndex = 0;
    }
}

bool AiChatWindow::applySelectedMention() {
    if (!mention.open || mention.items.empty() ||
        mention.selectedIndex < 0 ||
        mention.selectedIndex >= static_cast<int>(mention.items.size())) {
        return false;
    }

    const MentionItem item = mention.items[static_cast<size_t>(mention.selectedIndex)];
    std::string raw = stripPromptSoftWraps(
        inputBuffer.data(), static_cast<int>(std::strlen(inputBuffer.data())), inputSoftWraps);

    const int atPos = mention.atRawPos;
    const int endPos = mention.queryRawEnd;
    if (atPos < 0 || endPos < atPos || endPos > static_cast<int>(raw.size())) {
        closeMentionPopup();
        return false;
    }

    std::string insert = formatMentionInsert(item.insertText);
    if (endPos >= static_cast<int>(raw.size()) ||
        !std::isspace(static_cast<unsigned char>(raw[static_cast<size_t>(endPos)]))) {
        insert.push_back(' ');
    }

    const float maxLineWidth = mention.maxLineWidth > 0.0f
        ? mention.maxLineWidth
        : ImGui::GetFontSize() * 8.0f;
    const bool inserted = replacePromptRange(atPos, endPos, insert, maxLineWidth);
    closeMentionPopup();
    return inserted;
}

bool AiChatWindow::replacePromptRange(int rawStart, int rawEnd,
                                      const std::string& replacement,
                                      float maxLineWidth) {
    std::string raw = stripPromptSoftWraps(
        inputBuffer.data(), static_cast<int>(std::strlen(inputBuffer.data())), inputSoftWraps);
    if (rawStart < 0 || rawEnd < rawStart || rawEnd > static_cast<int>(raw.size())) {
        return false;
    }

    const size_t replacedSize = static_cast<size_t>(rawEnd - rawStart);
    if (raw.size() - replacedSize + replacement.size() >= inputBuffer.size()) {
        return false;
    }

    raw.replace(static_cast<size_t>(rawStart), replacedSize, replacement);
    const int rawCursor = rawStart + static_cast<int>(replacement.size());

    PromptWrapResult wrapped = rewrapPromptText(raw, maxLineWidth, inputBuffer.size() - 1);
    if (wrapped.displayText.size() >= inputBuffer.size()) {
        return false;
    }

    std::memcpy(inputBuffer.data(), wrapped.displayText.data(), wrapped.displayText.size());
    inputBuffer[wrapped.displayText.size()] = '\0';
    inputSoftWraps = std::move(wrapped.softWrapDisplayOffsets);
    inputDisplayText = wrapped.displayText;

    const int displayCursor = std::clamp(
        promptRawPosToDisplayPos(rawCursor, wrapped.softWrapRawOffsets),
        0, static_cast<int>(inputDisplayText.size()));

    // InputText keeps a private copy while active; reload it and park the caret
    // after the inserted text via the raw offset (stable across soft-wrap).
    pendingMentionRawCursor = rawCursor;
    mentionDisplayCursor = displayCursor;
    refocusInput = true;
    if (mentionInputId != 0) {
        if (ImGuiInputTextState* state = ImGui::GetInputTextState(mentionInputId)) {
            state->WantReloadUserBuf = true;
            state->ReloadSelectionStart = displayCursor;
            state->ReloadSelectionEnd = displayCursor;
        }
    }
    return true;
}

bool AiChatWindow::insertDroppedMentions(const std::vector<std::string>& mentionBodies,
                                         float maxLineWidth) {
    if (mentionBodies.empty()) {
        return false;
    }

    std::string raw = stripPromptSoftWraps(
        inputBuffer.data(), static_cast<int>(std::strlen(inputBuffer.data())), inputSoftWraps);
    const int rawCursor = std::clamp(
        pendingMentionRawCursor >= 0
            ? pendingMentionRawCursor
            : promptDisplayPosToRawPos(mentionDisplayCursor, inputSoftWraps),
        0, static_cast<int>(raw.size()));

    std::string insert;
    for (const std::string& body : mentionBodies) {
        if (body.empty()) {
            continue;
        }
        if (!insert.empty()) {
            insert.push_back(' ');
        }
        insert += formatMentionInsert(body);
    }
    if (insert.empty()) {
        return false;
    }

    if (rawCursor > 0 &&
        !std::isspace(static_cast<unsigned char>(raw[static_cast<size_t>(rawCursor - 1)]))) {
        insert.insert(insert.begin(), ' ');
    }
    if (rawCursor >= static_cast<int>(raw.size()) ||
        !std::isspace(static_cast<unsigned char>(raw[static_cast<size_t>(rawCursor)]))) {
        insert.push_back(' ');
    }

    const bool inserted = replacePromptRange(rawCursor, rawCursor, insert, maxLineWidth);
    if (inserted) {
        closeMentionPopup();
    }
    return inserted;
}

bool AiChatWindow::attachmentAlreadyPending(const std::string& name,
                                            const std::string& data) const {
    for (const ai::ChatAttachment& attachment : pendingAttachments) {
        if (attachment.name == name && attachment.data == data) {
            return true;
        }
    }
    return false;
}

// Checks the per-message budget in raw (pre-base64) bytes; images are stored
// as base64, so their raw size is recovered from the encoded length. Exact
// for the padded output Base64::encode produces.
bool AiChatWindow::attachmentBudgetAllows(size_t newRawBytes) const {
    auto rawBytesFromBase64 = [](const std::string& base64) {
        size_t padding = 0;
        if (!base64.empty() && base64.back() == '=') ++padding;
        if (base64.size() >= 2 && base64[base64.size() - 2] == '=') ++padding;
        return base64.size() / 4 * 3 - padding;
    };
    size_t totalBytes = newRawBytes;
    for (const ai::ChatAttachment& attachment : pendingAttachments) {
        totalBytes += attachment.isImage() ? rawBytesFromBase64(attachment.data)
                                           : attachment.data.size();
    }
    if (totalBytes > kMaxAttachmentTotalBytes) {
        Backend::getApp().registerAlert("Too many attachments",
            "Attachments in one message are limited to 12 MB total.");
        return false;
    }
    return true;
}

bool AiChatWindow::attachImageBytes(std::string name, std::string mimeType,
                                    std::vector<unsigned char> data) {
    if (name.empty() || data.empty() || !ai::isSupportedImageMime(mimeType)) {
        return false;
    }
    if (data.size() > kMaxAttachmentBytes) {
        Backend::getApp().registerAlert("Attachment too large",
            "Each attachment must be 10 MB or smaller.");
        return false;
    }
    if (!matchesImageType(data, mimeType)) {
        Backend::getApp().registerAlert("Invalid image",
            "The clipboard or file data is not a valid supported image.");
        return false;
    }

    std::string base64Data = Base64::encode(data.data(), data.size());
    if (attachmentAlreadyPending(name, base64Data)) {
        return true;
    }
    if (!attachmentBudgetAllows(data.size())) {
        return false;
    }
    pendingAttachments.push_back(
        {std::move(name), std::move(mimeType), std::move(base64Data)});
    return true;
}

bool AiChatWindow::attachImageBase64(std::string name, std::string mimeType,
                                     const std::string& base64Data) {
    if (base64Data.empty()) {
        return false;
    }
    // Reject before decoding: a valid encode of kMaxAttachmentBytes is at
    // most 4/3 of the raw size plus padding.
    if (base64Data.size() > (kMaxAttachmentBytes * 4 / 3 + 8)) {
        Backend::getApp().registerAlert("Attachment too large",
            "Each attachment must be 10 MB or smaller.");
        return false;
    }
    return attachImageBytes(std::move(name), std::move(mimeType),
                            Base64::decode(base64Data));
}

bool AiChatWindow::attachExternalFile(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return false;
    }
    const uintmax_t fileSize = fs::file_size(path, ec);
    if (ec || fileSize == 0) {
        Backend::getApp().registerAlert("Empty attachment",
            "Empty files cannot be attached.");
        return false;
    }
    if (fileSize > kMaxAttachmentBytes) {
        Backend::getApp().registerAlert("Attachment too large",
            "Each attachment must be 10 MB or smaller.");
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::vector<unsigned char> data(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) return false;

    const std::string name = FileUtils::pathToUtf8(path.filename());
    const std::vector<unsigned char> nameBytes(name.begin(), name.end());
    if (!validUtf8Text(nameBytes)) {
        Backend::getApp().registerAlert("Unsupported attachment",
            "Attachment filenames must be valid UTF-8.");
        return false;
    }
    const std::string mimeType = imageMimeType(path);
    if (!mimeType.empty()) {
        return attachImageBytes(name, mimeType, std::move(data));
    }
    if (data.size() > kMaxTextAttachmentBytes) {
        Backend::getApp().registerAlert("Attachment too large",
            "Text attachments must be 1 MB or smaller.");
        return false;
    }
    if (!validUtf8Text(data)) {
        Backend::getApp().registerAlert("Unsupported attachment",
            "Only UTF-8 text files and PNG, JPEG, GIF, or WebP images can be attached.");
        return false;
    }

    std::string text(data.begin(), data.end());
    if (attachmentAlreadyPending(name, text)) {
        return true;
    }
    if (!attachmentBudgetAllows(text.size())) {
        return false;
    }
    pendingAttachments.push_back({name, "text/plain", std::move(text)});
    return true;
}

void AiChatWindow::attachExternalFiles(const std::vector<std::string>& paths) {
    for (const std::string& path : paths) {
        attachExternalFile(FileUtils::pathFromUtf8(path));
    }
}

bool AiChatWindow::handleAttachmentPaste(ImGuiID inputId) {
    const ImGuiIO& io = ImGui::GetIO();
    const bool pasteChordDown = ImGui::GetActiveID() == inputId &&
                                (io.KeyCtrl || io.KeySuper) &&
                                ImGui::IsKeyDown(ImGuiKey_V);
    if (!pasteChordDown) {
        attachmentPasteHeld = false;
        return false;
    }
    // Key-repeat frames: InputText's own paste shortcut repeats, so while a
    // Ctrl+V that started as an attachment paste stays held, keep suppressing
    // the text paste — otherwise repeats would spill the raw path text into
    // the prompt right after the file was attached.
    if (!ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        return attachmentPasteHeld;
    }
    attachmentPasteHeld = false;

    // One native probe answers files-or-image; the text heuristic only fills
    // in for platforms without a native file-list clipboard.
    ClipboardContent clipboardContent = readClipboardContent();
    std::vector<std::string> paths = std::move(clipboardContent.filePaths);
    if (paths.empty()) {
        paths = clipboardTextToFilePaths(ImGui::GetClipboardText());
    }
    if (!paths.empty()) {
        attachExternalFiles(paths);
        attachmentPasteHeld = true;
        return true;
    }

    if (!clipboardContent.image.data.empty()) {
        attachImageBytes(
            "clipboard-image" + extensionForImageMime(clipboardContent.image.mimeType),
            clipboardContent.image.mimeType, std::move(clipboardContent.image.data));
        // Recognized image content is never pasted as text, even when the
        // attach itself was rejected (an alert already explained why).
        attachmentPasteHeld = true;
        return true;
    }

    const char* clipboard = ImGui::GetClipboardText();
    if (!clipboard) return false;
    const std::string dataUrl(clipboard);
    if (dataUrl.rfind("data:image/", 0) != 0) return false;
    const size_t separator = dataUrl.find(";base64,");
    if (separator == std::string::npos) return false;

    const std::string mimeType = dataUrl.substr(5, separator - 5);
    attachImageBase64("clipboard-image" + extensionForImageMime(mimeType), mimeType,
                      dataUrl.substr(separator + 8));
    // Same rule as above: never dump a megabyte data: URL into the prompt.
    attachmentPasteHeld = true;
    return true;
}

void AiChatWindow::handlePromptDrop(float maxLineWidth) {
    if (!project || !ImGui::BeginDragDropTarget()) {
        return;
    }

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("resource_files")) {
        if (payload->IsDelivery()) {
            std::vector<std::string> mentions;
            const fs::path projectPath = project->getProjectPath();
            if (!projectPath.empty()) {
                for (const std::string& droppedPath : Util::getStringsFromPayload(payload)) {
                    std::error_code ec;
                    const fs::path path(droppedPath);
                    if (!fs::is_regular_file(path, ec) || ec) {
                        continue;
                    }

                    ec.clear();
                    fs::path relative = fs::relative(path, projectPath, ec);
                    if (ec || relative.empty() ||
                        (relative.begin() != relative.end() && *relative.begin() == "..")) {
                        continue;
                    }
                    mentions.push_back(relative.generic_string());
                }
            }
            insertDroppedMentions(mentions, maxLineWidth);
        }
    }

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("external_files")) {
        if (payload->IsDelivery()) {
            attachExternalFiles(Util::getStringsFromPayload(payload));
        }
    }

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("entity")) {
        if (payload->IsDelivery() && payload->DataSize >= sizeof(EntityPayload)) {
            EntityPayload entityPayload{};
            std::memcpy(&entityPayload, payload->Data, sizeof(entityPayload));
            SceneProject* sceneProject = entityPayload.entitySceneId != 0
                ? project->getScene(entityPayload.entitySceneId)
                : project->getSelectedScene();
            if (sceneProject && sceneProject->scene && entityPayload.entity != NULL_ENTITY &&
                sceneProject->scene->isEntityCreated(entityPayload.entity)) {
                std::vector<Entity> entities{entityPayload.entity};
                const SceneProject* selectedScene = project->getSelectedScene();
                if (selectedScene && selectedScene->id == sceneProject->id) {
                    std::vector<Entity> selected =
                        project->getSelectedEntities(sceneProject->id);
                    if (std::find(selected.begin(), selected.end(), entityPayload.entity) !=
                        selected.end()) {
                        entities = Project::getTopLevelEntities(sceneProject->scene, selected);
                    }
                }

                std::vector<std::string> mentions;
                mentions.reserve(entities.size());
                for (Entity entity : entities) {
                    if (!sceneProject->scene->isEntityCreated(entity)) {
                        continue;
                    }
                    std::string name = sceneProject->scene->getEntityName(entity);
                    if (name.empty()) {
                        name = "Entity " + std::to_string(static_cast<unsigned>(entity));
                    }
                    mentions.push_back(std::move(name));
                }
                insertDroppedMentions(mentions, maxLineWidth);
            }
        }
    }

    ImGui::EndDragDropTarget();
}

bool AiChatWindow::handleMentionHistoryKey(ImGuiKey key) {
    if (!mention.open || mention.items.empty()) {
        return false;
    }
    if (key == ImGuiKey_UpArrow) {
        mention.selectedIndex = std::max(0, mention.selectedIndex - 1);
        mention.scrollToSelected = true;
        return true;
    }
    if (key == ImGuiKey_DownArrow) {
        mention.selectedIndex = std::min(static_cast<int>(mention.items.size()) - 1,
                                         mention.selectedIndex + 1);
        mention.scrollToSelected = true;
        return true;
    }
    return false;
}

bool AiChatWindow::handleMentionKeys() {
    if (!mention.open) {
        return false;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeMentionPopup();
        return true;
    }
    // Enter/Tab are claimed before InputText while the picker is open.
    return false;
}

void AiChatWindow::drawMentionPopup() {
    if (!mention.open) {
        return;
    }

    const float popupWidth = Theme::dpi(360.0f);
    const float itemHeight = ImGui::GetTextLineHeight() + Theme::dpi(6.0f);
    const float maxVisible = 8.0f;
    const float popupHeight = mention.items.empty()
        ? itemHeight + Theme::dpi(12.0f)
        : std::min(maxVisible, static_cast<float>(mention.items.size())) * itemHeight + Theme::dpi(8.0f);

    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    if (viewport == nullptr) {
        viewport = ImGui::GetMainViewport();
    }
    ImVec2 pos = mention.popupPos;
    if (pos.x + popupWidth > viewport->WorkPos.x + viewport->WorkSize.x) {
        pos.x = viewport->WorkPos.x + viewport->WorkSize.x - popupWidth - Theme::dpi(8.0f);
    }
    if (pos.x < viewport->WorkPos.x + Theme::dpi(8.0f)) {
        pos.x = viewport->WorkPos.x + Theme::dpi(8.0f);
    }
    if (pos.y + popupHeight > viewport->WorkPos.y + viewport->WorkSize.y) {
        pos.y = mention.inputItemMinY - popupHeight - Theme::dpi(2.0f);
    }

    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(4.0f, 4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Theme::dpi(ImVec2(4.0f, 0.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Theme::dpi(4.0f));

    if (ImGui::Begin("##AiMentionPopup", nullptr, flags)) {
        if (mention.items.empty()) {
            ImGui::TextDisabled("No matches for @%s", mention.query.c_str());
        } else {
            for (int i = 0; i < static_cast<int>(mention.items.size()); ++i) {
                const MentionItem& item = mention.items[static_cast<size_t>(i)];
                const bool selected = i == mention.selectedIndex;
                ImGui::PushID(i);

                if (selected && mention.scrollToSelected) {
                    ImGui::SetScrollHereY();
                    mention.scrollToSelected = false;
                }

                if (ImGui::Selectable("##mention", selected, ImGuiSelectableFlags_None,
                                      ImVec2(0, itemHeight))) {
                    mention.selectedIndex = i;
                    applySelectedMention();
                    ImGui::PopID();
                    break;
                }

                ImVec2 rowMin = ImGui::GetItemRectMin();
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const char* icon = mentionKindIcon(item.kind);
                const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
                const float textY = rowMin.y + (itemHeight - ImGui::GetTextLineHeight()) * 0.5f;
                draw->AddText(ImVec2(rowMin.x + Theme::dpi(6.0f), textY), iconColor, icon);

                const float iconWidth = ImGui::CalcTextSize(icon).x + Theme::dpi(10.0f);
                draw->AddText(ImVec2(rowMin.x + iconWidth, textY), textColor, item.label.c_str());

                std::string kind = mentionKindLabel(item.kind);
                if (!item.detail.empty() && item.detail != item.label) {
                    kind += " · " + item.detail;
                }
                const ImVec2 kindSize = ImGui::CalcTextSize(kind.c_str());
                const float kindX = rowMin.x + ImGui::GetItemRectSize().x - kindSize.x - Theme::dpi(8.0f);
                if (kindX > rowMin.x + iconWidth + ImGui::CalcTextSize(item.label.c_str()).x + Theme::dpi(12.0f)) {
                    draw->AddText(ImVec2(kindX, textY), iconColor, kind.c_str());
                }

                ImGui::PopID();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void AiChatWindow::drawComposerControls() {
    const ImGuiStyle& style = ImGui::GetStyle();

    ai::Settings settings = service.getSettings();
    const bool busy = service.isBusy();

    float avail = ImGui::GetContentRegionAvail().x;
    float buttonWidth = ImGui::GetFrameHeight();
    float editGap = std::max(4.0f, style.ItemInnerSpacing.x);
    std::string modelText = modelDisplayName(settings, modelCatalog);
    float preferredModelWidth = ImGui::CalcTextSize(modelText.c_str()).x + buttonWidth + editGap;
    float modelWidth = std::min(std::max(70.0f, preferredModelWidth),
                                std::max(70.0f, avail * 0.62f));
    if (avail > 84.0f) {
        modelWidth = std::min(modelWidth, avail - 44.0f);
    }
    float approvalWidth = avail - modelWidth - style.ItemSpacing.x;
    approvalWidth = std::max(40.0f, approvalWidth);
    modelWidth = std::max(40.0f, modelWidth);
    float rowStartX = ImGui::GetCursorPosX();
    float rowRightX = rowStartX + avail;

    ImGui::BeginDisabled(busy);

    const ImVec4* approvalColor = settings.approvalMode == ai::ApprovalMode::FullAgent
        ? &Theme::Colors::DisabledGreenText
        : nullptr;
    drawEditableSettingLabel(approvalDisplayName(settings.approvalMode), approvalWidth,
                             "##AiApprovalEdit", "AI Approval##AiApprovalPopup",
                             approvalColor);

    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(rowStartX, rowRightX - modelWidth));
    drawEditableSettingLabel(modelText, modelWidth,
                             "##AiModelEdit", "AI Model##AiModelPopup");

    ImGui::EndDisabled();

    drawModelPopup();
    drawCustomModelPopup();
    drawApprovalPopup();
}

void AiChatWindow::drawEditableSettingLabel(const std::string& value, float width,
                                           const char* editId, const char* popupId,
                                           const ImVec4* textColor) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const char* icon = ICON_FA_PEN_TO_SQUARE;
    float frameHeight = ImGui::GetFrameHeight();
    float iconWidth = ImGui::CalcTextSize(icon).x;
    float gap = std::max(Theme::dpi(4.0f), style.ItemInnerSpacing.x);
    float textWidth = std::max(1.0f, width - iconWidth - gap);

    std::string display = elideToWidth(value, textWidth);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton(editId, ImVec2(width, frameHeight))) {
        ImGui::OpenPopup(popupId);
    }

    ImU32 valueColor = textColor
        ? ImGui::GetColorU32(*textColor)
        : ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ImU32 iconColor = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    ImVec2 textSize = ImGui::CalcTextSize(display.c_str());
    float textY = pos.y + (frameHeight - textSize.y) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x, textY), valueColor, display.c_str());

    float iconX = std::min(pos.x + textSize.x + gap, pos.x + width - iconWidth);
    ImVec2 iconSize = ImGui::CalcTextSize(icon);
    float iconY = pos.y + (frameHeight - iconSize.y) * 0.5f;
    ImGui::GetWindowDrawList()->AddText(ImVec2(iconX, iconY), iconColor, icon);
}

void AiChatWindow::drawModelPopup() {
    constrainNextPopupToViewport(260.0f, 420.0f, 0.62f);
    if (!ImGui::BeginPopup("AI Model##AiModelPopup")) {
        return;
    }

    ai::Settings settings = service.getSettings();
    std::vector<ai::ProviderAccount> accounts = ai::SecretStore::configuredAccounts(settings);

    if (accounts.empty()) {
        ImGui::TextDisabled("No providers configured");
        if (ImGui::Selectable("Add an API key...")) {
            Backend::getApp().openEditorSettings(EditorSettingsWindow::Tab::AI, true);
            ImGui::CloseCurrentPopup();
        }
        keepCurrentWindowInsideViewport();
        ImGui::EndPopup();
        return;
    }

    std::string current = settings.model.empty()
        ? ai::defaultModelForProvider(settings.provider)
        : settings.model;
    const std::string currentAccount = ai::accountKey(settings);

    // Only configured accounts are listed; their models are fetched live
    // (nothing is shown until the fetch lands).
    for (size_t index = 0; index < accounts.size(); ++index) {
        const ai::ProviderAccount& account = accounts[index];
        if (modelCatalog.needsFetch(account)) {
            modelCatalog.refresh(account, ai::SecretStore::getApiKey(account.id));
        }

        if (index > 0) {
            ImGui::Spacing();
        }
        // Two endpoints can serve the same model id, so scope the ids per account.
        ImGui::PushID(account.id.c_str());
        ImGui::TextDisabled("%s", account.label.c_str());
        ImGui::Separator();

        std::vector<ai::ModelInfo> models = modelCatalog.models(account.id);
        for (const auto& model : models) {
            bool selected = (account.id == currentAccount) && (current == model.id);
            ImGui::PushID(model.id.c_str());
            if (ImGui::Selectable(model.label.c_str(), selected)) {
                ai::Settings next = settings;
                next.provider = account.provider;
                next.endpointId = account.endpointId;
                next.model = model.id;
                applySettings(next);
                ImGui::CloseCurrentPopup();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        if (models.empty()) {
            ImGui::TextDisabled("  %s", modelCatalog.status(account.id) == ai::CatalogStatus::Loading
                                            ? "Loading..."
                                            : "No models available");
        }
        // Per account: an endpoint whose /models failed is only reachable here.
        if (ImGui::Selectable("Custom model...")) {
            std::snprintf(customModelBuffer.data(), customModelBuffer.size(), "%s",
                          account.id == currentAccount ? current.c_str() : "");
            customModelProvider = account.provider;
            customModelEndpointId = account.endpointId;
            customModelPopupRequested = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Selectable(ICON_FA_ROTATE "  Refresh models")) {
        for (const ai::ProviderAccount& account : accounts) {
            modelCatalog.refresh(account, ai::SecretStore::getApiKey(account.id));
        }
    }

    keepCurrentWindowInsideViewport();
    ImGui::EndPopup();
}

void AiChatWindow::drawCustomModelPopup() {
    if (customModelPopupRequested) {
        ImGui::OpenPopup("Custom model##AiCustomModelPopup");
        customModelPopupRequested = false;
    }

    if (!ImGui::BeginPopupModal("Custom model##AiCustomModelPopup", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        return;
    }

    ImGui::SetNextItemWidth(320.0f);
    ImGui::InputText("##AiCustomModelInput", customModelBuffer.data(), customModelBuffer.size());

    ImGui::BeginDisabled(customModelBuffer[0] == '\0');
    if (ImGui::Button("Use", ImVec2(90, 0))) {
        ai::Settings next = service.getSettings();
        next.provider = customModelProvider;
        next.endpointId = customModelEndpointId;
        next.model = customModelBuffer.data();
        applySettings(next);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90, 0))) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void AiChatWindow::drawApprovalPopup() {
    if (ImGui::BeginPopup("AI Approval##AiApprovalPopup")) {
        ai::Settings settings = service.getSettings();
        int approvalIndex = approvalToIndex(settings.approvalMode);
        for (int i = 0; i < kApprovalCount; ++i) {
            bool selected = i == approvalIndex;
            if (ImGui::Selectable(kApprovalLabels[i], selected)) {
                settings.approvalMode = kApprovalValues[i];
                applySettings(settings);
                ImGui::CloseCurrentPopup();
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndPopup();
    }
}

void AiChatWindow::applySettings(const ai::Settings& settings) {
    ai::Settings next = settings;
    if (next.model.empty()) {
        next.model = ai::defaultModelForProvider(next.provider);
    }
    AppSettings::setAiSettings(next);
    service.setSettings(next);
}

bool AiChatWindow::canSendMessage(std::string* reason) const {
    ai::Settings settings = service.getSettings();
    if (ai::SecretStore::configuredAccounts(settings).empty()) {
        if (reason) *reason = "Add an API key before sending.";
        return false;
    }
    if (ai::accountRequiresApiKey(settings.provider) &&
        !ai::SecretStore::hasApiKey(ai::accountKey(settings))) {
        if (reason) *reason = "Select a configured model/provider before sending.";
        return false;
    }
    if (settings.provider == ai::ProviderId::OpenAICompatible &&
        ai::activeEndpointUrl(settings).empty()) {
        if (reason) *reason = "Set this endpoint's URL in AI settings before sending.";
        return false;
    }
    if (settings.model.empty()) {
        if (reason) *reason = "Select a model before sending.";
        return false;
    }
    if (!ai::providerSupportsImages(settings.provider) &&
        std::any_of(pendingAttachments.begin(), pendingAttachments.end(),
                    [](const ai::ChatAttachment& attachment) { return attachment.isImage(); })) {
        if (reason) *reason = "Select a vision-capable provider to send images.";
        return false;
    }
    return true;
}

void AiChatWindow::submitMessage() {
    std::string promptText = stripPromptSoftWraps(
        inputBuffer.data(), static_cast<int>(std::strlen(inputBuffer.data())),
        inputSoftWraps);
    if (service.isBusy() || (promptText.empty() && pendingAttachments.empty()) ||
        !canSendMessage()) {
        return;
    }
    // Moved, not copied: attachments can be many MB. sendUserMessage only
    // consumes them when it accepts the message, so a refused send (e.g. a
    // busy race) leaves the pending list intact.
    if (service.sendUserMessage(promptText, std::move(pendingAttachments))) {
        inputBuffer.fill('\0');
        inputSoftWraps.clear();
        inputDisplayText.clear();
        pendingAttachments.clear();
        closeMentionPopup();
        scrollToBottom = true;
        refocusInput = true;
    }
}

void AiChatWindow::executeProposal(uint64_t proposalId) {
    ai::EditorActionExecutor executor(project, resourcesWindow, &service.getHttpClient());
    service.executeProposal(proposalId, executor);
    scrollToBottom = true;
}

void AiChatWindow::autoRunProposals() {
    ai::ApprovalMode mode = service.getSettings().approvalMode;
    if (mode == ai::ApprovalMode::PreviewThenApprove) {
        return;
    }

    // One per frame cost a ~100 ms idle tick per batched tool. Drain the batch here,
    // budgeted so one slow action cannot freeze the UI for the whole round.
    const auto budget = std::chrono::milliseconds(50);
    const auto started = std::chrono::steady_clock::now();

    while (true) {
        uint64_t nextId = 0;
        for (const auto& proposal : service.getProposals()) {
            if (proposal.executed || proposal.executing) {
                continue;
            }
            bool eligible = (mode == ai::ApprovalMode::FullAgent) ||
                            (mode == ai::ApprovalMode::SafeAutoRun && proposal.readOnly);
            if (eligible) {
                if (ai::EditorActionExecutor::isBlockedBySave(project, proposal.toolName)) {
                    return; // runs after the scene save
                }
                nextId = proposal.id;
                break;
            }
        }
        if (nextId == 0) {
            return;
        }

        executeProposal(nextId);

        if (std::chrono::steady_clock::now() - started >= budget) {
            return; // the rest of the batch runs next frame
        }
    }
}

void AiChatWindow::updateMessageNotification() {
    const std::vector<ai::ChatMessage>& messages = cachedMessages();
    if (messages.size() < lastObservedMessageCount) {
        lastObservedMessageCount = messages.size();
        return;
    }

    bool receivedIncomingMessage = false;
    for (size_t i = lastObservedMessageCount; i < messages.size(); ++i) {
        if (messages[i].role == ai::ChatRole::Assistant || messages[i].role == ai::ChatRole::Tool) {
            receivedIncomingMessage = true;
            break;
        }
    }

    if (receivedIncomingMessage && !isWindowVisible) {
        hasNotification = true;
    }
    lastObservedMessageCount = messages.size();
}

void AiChatWindow::startNewChat() {
    if (service.isBusy()) {
        return;
    }
    // The previous conversation was already persisted by the per-frame pump.
    service.clearConversation();
    currentConversationId.clear();
    lastSavedMessageCount = 0;
    lastObservedMessageCount = 0;
    hasNotification = false;
    inputBuffer.fill('\0');
    inputSoftWraps.clear();
    inputDisplayText.clear();
    pendingAttachments.clear();
    scrollToBottom = false;
    // Mark the current path as handled so a fresh temp project at a reused path
    // does not auto-restore the previous chat on the next frame.
    autoLoadedProjectPath = project ? project->getProjectPath().string() : std::string();
    autoLoadedLatestConversation = true;
}

void AiChatWindow::loadLatestConversationForCurrentProject() {
    if (!project || service.isBusy()) {
        return;
    }

    const std::string projectPath = project->getProjectPath().string();
    if (projectPath != autoLoadedProjectPath) {
        autoLoadedProjectPath = projectPath;
        autoLoadedLatestConversation = false;
        service.clearConversation();
        currentConversationId.clear();
        lastSavedMessageCount = 0;
        lastObservedMessageCount = 0;
        hasNotification = false;
        scrollToBottom = false;
        inputBuffer.fill('\0');
        inputSoftWraps.clear();
        inputDisplayText.clear();
        pendingAttachments.clear();
    }

    if (projectPath.empty() || autoLoadedLatestConversation) {
        return;
    }

    autoLoadedLatestConversation = true;
    std::vector<ai::ConversationMeta> conversations = conversationStore.list();
    if (!conversations.empty()) {
        loadConversation(conversations.front().id);
    }
}

void AiChatWindow::persistConversation() {
    // Without a project path the store would resolve to a CWD-relative
    // ".doriax" folder; the auto-loader has the matching guard.
    if (!project || project->getProjectPath().empty()) {
        return;
    }
    const std::vector<ai::ChatMessage>& messages = cachedMessages();
    if (messages.size() == lastSavedMessageCount) {
        return; // nothing new since the last save
    }
    if (messages.empty()) {
        lastSavedMessageCount = 0;
        return;
    }
    if (currentConversationId.empty()) {
        currentConversationId = ai::ConversationStore::newId();
    }
    conversationStore.save(currentConversationId, conversationTitleFromMessages(messages), messages);
    lastSavedMessageCount = messages.size();
}

void AiChatWindow::loadConversation(const std::string& id) {
    if (service.isBusy()) {
        return;
    }
    std::vector<ai::ChatMessage> messages;
    if (!conversationStore.load(id, messages)) {
        return;
    }
    service.loadConversation(std::move(messages));
    currentConversationId = id;
    lastSavedMessageCount = cachedMessages().size();
    lastObservedMessageCount = lastSavedMessageCount;
    hasNotification = false;
    inputBuffer.fill('\0');
    inputSoftWraps.clear();
    inputDisplayText.clear();
    pendingAttachments.clear();
    scrollToBottom = true;
}

} // namespace doriax::editor
