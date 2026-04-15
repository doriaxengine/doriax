#include "OutputWindow.h"
#include "external/IconsFontAwesome6.h"
#include "util/UIUtils.h"
#include "App.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <chrono>

#include "imgui_internal.h"

static float getElapsedSeconds() {
    static auto startTime = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float>(now - startTime).count();
}

using namespace doriax::editor;

static ImVec4 GetLogTypeColor(LogType type) {
    // Tune to taste
    switch (type) {
        case LogType::Info:    return ImVec4(0.90f, 0.90f, 0.95f, 1.00f); // light gray-blue
        case LogType::Warning: return ImVec4(1.00f, 0.85f, 0.40f, 1.00f); // amber
        case LogType::Error:   return ImVec4(1.00f, 0.45f, 0.45f, 1.00f); // red
        case LogType::Success: return ImVec4(0.55f, 1.00f, 0.55f, 1.00f); // green
        case LogType::Build:   return ImVec4(0.60f, 0.85f, 1.00f, 1.00f); // sky
        default:               return ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    }
}

OutputWindow::OutputWindow() {
    needsRebuild = false;
    menuWidth = 0;
    selectionStart = -1;
    selectionEnd = -1;
    hasStoredSelection = false;
    isSelecting = false;
    autoScroll = true; // default: stick to bottom
    autoScrollLockedButton = autoScroll;
    lastScrollY = 0.0f;      // initialize scroll tracking
    for (int i = 0; i < 5; i++) {
        typeFilters[i] = true;
    }
    searchMatchCase = false; // default: case-insensitive
    hasNotification = false;
    isWindowVisible = false;
    clear();
}

void OutputWindow::clear() {
    buf.clear();
    logs.clear();
    lineOffsets.clear();
    lineOffsets.push_back(0);
    lineTypes.clear();
    lineHardBreak.clear();
    needsRebuild = false;
    selectionStart = selectionEnd = -1;
    hasStoredSelection = false;
    isSelecting = false;
    lastScrollY = 0.0f;
}

void OutputWindow::addLog(LogType type, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char temp[4096];
    vsnprintf(temp, IM_ARRAYSIZE(temp), fmt, args);
    va_end(args);

    addLog(type, std::string(temp));
}

void OutputWindow::addLog(LogType type, const std::string& message) {
    // Store the raw log entry
    LogData logEntry{type, message, getElapsedSeconds()};
    logs.push_back(logEntry);

    // Mark that we need to rebuild
    needsRebuild = true;
    if (!isWindowVisible) {
        hasNotification = true;
    }

    // Basic formatting for initial display (append quickly for incremental feel)
    std::string typeStr;
    switch(type) {
        case LogType::Error: typeStr = "Error"; break;
        case LogType::Success: typeStr = "Success"; break;
        case LogType::Info: typeStr = "Info"; break;
        case LogType::Warning: typeStr = "Warning"; break;
        case LogType::Build: typeStr = "Build"; break;
    }
    std::string formattedMessage = "[" + typeStr + "] " + message + "\n";

    // Append raw for immediate feedback (will be rebuilt and recolored on next frame)
    int oldSize = buf.size();
    buf.append(formattedMessage.c_str());
    for (int newSize = buf.size(); oldSize < newSize; oldSize++)
        if (buf[oldSize] == '\n')
            lineOffsets.push_back(oldSize + 1);
}

static std::string getTypePrefixString(LogType type) {
    switch(type) {
        case LogType::Error: return "[Error] ";
        case LogType::Success: return "[Success] ";
        case LogType::Info: return "[Info] ";
        case LogType::Warning: return "[Warning] ";
        case LogType::Build: return "[Build] ";
        default: return "[Unknown] ";
    }
}

void OutputWindow::rebuildBuffer(float wrapWidth) {
    if (!ImGui::GetCurrentContext() || !ImGui::GetCurrentWindow()) {
        return;
    }

    // Fallback if caller didn't provide a sane width
    if (wrapWidth <= 0.0f) {
        float w = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        wrapWidth = ImMax(1.0f, w);
    }

    buf.clear();
    lineOffsets.clear();
    lineOffsets.push_back(0);
    lineTypes.clear();
    lineHardBreak.clear();

    ImFont* font = ImGui::GetFont();
    float fontSize = ImGui::GetFontSize();

    for (size_t logIndex = 0; logIndex < logs.size(); ++logIndex) {
        const auto& log = logs[logIndex];
        std::string prefix = getTypePrefixString(log.type);
        float prefixWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, prefix.c_str()).x;

        bool typeAllowed = false;
        switch(log.type) {
            case LogType::Info: typeAllowed = typeFilters[0]; break;
            case LogType::Warning: typeAllowed = typeFilters[1]; break;
            case LogType::Error: typeAllowed = typeFilters[2]; break;
            case LogType::Success: typeAllowed = typeFilters[3]; break;
            case LogType::Build: typeAllowed = typeFilters[4]; break;
        }

        // Empty message: just print the prefix as its own line
        if (log.message.empty()) {
            if (typeAllowed && passTextFilter((prefix + "\n").c_str())) {
                buf.append(prefix.c_str());
                bool appendedNewline = (logIndex < logs.size() - 1);
                if (appendedNewline) {
                    buf.append("\n");
                }
                lineOffsets.push_back(buf.size());
                lineTypes.push_back(log.type);
                lineHardBreak.push_back(appendedNewline ? 1 : 0);
            }
            continue;
        }

        std::string indentation(prefix.length(), ' ');
        float indentWidth = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, indentation.c_str()).x;

        std::string currentLine = prefix;
        float currentLineWidth = prefixWidth;
        bool isFirstLine = true;

        const char* s = log.message.data();
        const char* e = s + log.message.size();

        while (s < e) {
            unsigned int cp = 0;
            int c_len = ImTextCharFromUtf8(&cp, s, e);
            if (c_len <= 0) {
                // Fallback: treat as a single byte if invalid
                cp = (unsigned char)*s;
                c_len = 1;
            }

            // Normalize CRLF: skip '\r'
            if (cp == '\r') {
                s += c_len;
                continue;
            }

            if (!isFirstLine && currentLine.empty()) {
                currentLine = indentation;
                currentLineWidth = indentWidth;
            }

            if (cp == '\n') {
                if (typeAllowed && passTextFilter(currentLine.c_str())) {
                    buf.append(currentLine.c_str());
                    bool addNewline = ((s + c_len) < e) || (logIndex < logs.size() - 1);
                    if (addNewline) buf.append("\n");
                    lineOffsets.push_back(buf.size());
                    lineTypes.push_back(log.type);
                    lineHardBreak.push_back(addNewline ? 1 : 0); // hard break from original newline
                }
                currentLine.clear();
                currentLineWidth = 0.0f;
                isFirstLine = false;
                s += c_len;
                continue;
            }

            // Measure this UTF-8 codepoint width
            float cw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s, s + c_len).x;

            // Wrap before adding this codepoint if it overflows
            if (currentLineWidth + cw > wrapWidth && !currentLine.empty()) {
                if (typeAllowed && passTextFilter(currentLine.c_str())) {
                    buf.append(currentLine.c_str());
                    buf.append("\n");
                    lineOffsets.push_back(buf.size());
                    lineTypes.push_back(log.type);
                    lineHardBreak.push_back(0); // soft wrap
                }
                currentLine = indentation;
                currentLine.append(s, c_len);
                currentLineWidth = indentWidth + cw;
                isFirstLine = false;
            } else {
                currentLine.append(s, c_len);
                currentLineWidth += cw;
            }

            s += c_len;
        }

        if (!currentLine.empty()) {
            if (typeAllowed && passTextFilter(currentLine.c_str())) {
                buf.append(currentLine.c_str());
                bool appendNewlineBetweenLogs = (logIndex < logs.size() - 1);
                if (appendNewlineBetweenLogs) {
                    buf.append("\n");
                }
                lineOffsets.push_back(buf.size());
                lineTypes.push_back(log.type);
                lineHardBreak.push_back(appendNewlineBetweenLogs ? 1 : 0); // hard break between logs
            }
        }
    }

    // Reset selection if buffer changed drastically (optional; keep it if you want)
    if (selectionStart >= buf.size() || selectionEnd > buf.size()) {
        selectionStart = selectionEnd = -1;
        hasStoredSelection = false;
        isSelecting = false;
    }
}

// Ensure index is at a UTF-8 code-point boundary inside [0, buf.size()]
int OutputWindow::clampIndexToCodepointBoundary(int idx) const {
    idx = std::max(0, std::min(idx, (int)buf.size()));
    // Move backward until at boundary (0xxxxxxx or 11xxxxxx start)
    while (idx > 0) {
        unsigned char c = (unsigned char)buf[idx];
        if ((c & 0xC0) != 0x80) break; // not a continuation byte
        idx--;
    }
    return idx;
}

bool OutputWindow::isWordChar(unsigned int cp) {
    // Consider letters, digits, underscore as word chars. Others are delimiters.
    if (cp == '_') return true;
    if (cp >= '0' && cp <= '9') return true;
    if (cp >= 'A' && cp <= 'Z') return true;
    if (cp >= 'a' && cp <= 'z') return true;
    // Basic non-ASCII letters could be treated as word chars. Heuristic:
    if (cp > 127 && (ImCharIsBlankW((ImWchar)cp) == false)) return true;
    return false;
}

void OutputWindow::selectWordAt(int bufferIndex) {
    if (buf.empty()) return;
    int n = (int)buf.size();
    int idx = clampIndexToCodepointBoundary(bufferIndex);
    if (idx >= n) idx = n - 1;
    if (idx < 0) return;

    // Determine codepoint at idx (if idx points to end of a character, move back one)
    int cp_start = idx;
    if (cp_start >= n) cp_start = n - 1;
    cp_start = clampIndexToCodepointBoundary(cp_start);

    // Decode codepoint at cp_start
    const char* begin = buf.begin();
    const char* s = begin + cp_start;
    const char* e = begin + n;
    unsigned int cp = 0;
    int len = ImTextCharFromUtf8(&cp, s, e);
    if (len <= 0) {
        selectionStart = cp_start;
        selectionEnd = std::min(cp_start + 1, n);
        hasStoredSelection = selectionEnd > selectionStart;
        return;
    }

    bool wordChar = isWordChar(cp);

    // Expand left
    int left = cp_start;
    while (left > 0) {
        int prev = left;
        do { prev--; } while (prev > 0 && ((unsigned char)buf[prev] & 0xC0) == 0x80);
        unsigned int cp2 = 0;
        int l2 = ImTextCharFromUtf8(&cp2, begin + prev, begin + n);
        if (l2 <= 0) break;
        if (isWordChar(cp2) != wordChar) break;
        left = prev;
    }

    // Expand right
    int right = cp_start + len;
    while (right < n) {
        unsigned int cp2 = 0;
        int l2 = ImTextCharFromUtf8(&cp2, begin + right, begin + n);
        if (l2 <= 0) break;
        if (isWordChar(cp2) != wordChar) break;
        right += l2;
    }

    selectionStart = left;
    selectionEnd = right;
    hasStoredSelection = selectionEnd > selectionStart;
}

void OutputWindow::selectLineAt(int bufferIndex) {
    if (buf.empty()) return;
    int n = (int)buf.size();
    int idx = std::max(0, std::min(bufferIndex, n));

    // Find start of line (after previous '\n' or 0)
    int start = idx;
    while (start > 0 && buf[start - 1] != '\n') start--;

    // Find end of line (position of '\n' or n)
    int end = idx;
    while (end < n && buf[end] != '\n') end++;

    selectionStart = start;
    selectionEnd = end; // exclude '\n' to match common editors
    hasStoredSelection = selectionEnd > selectionStart;
}

// Case-aware filter pass that mirrors ImGuiTextFilter semantics when possible
bool OutputWindow::passTextFilter(const char* text) const {
    if (!filter.IsActive())
        return true;

    // Default (case-insensitive) -> use Dear ImGui implementation
    if (!searchMatchCase)
        return filter.PassFilter(text);

    // Case-sensitive path:
    // Use built tokens (filter.Build() must be called when InputBuf changes)
    const char* text_begin = text;
    const char* text_end   = text + std::strlen(text);

    int include_count = 0;
    bool include_matched = false;

    for (int i = 0; i < filter.Filters.Size; ++i) {
        const ImGuiTextFilter::ImGuiTextRange& r = filter.Filters[i];
        if (r.empty()) continue;

        const char* f_begin = r.b;
        const char* f_end   = r.e;

        bool is_exclude = (*f_begin == '-');
        if (is_exclude) {
            f_begin++;
            if (f_begin >= f_end) continue; // empty exclude
        } else {
            include_count++;
        }

        // Case-sensitive substring search
        const char* found = std::search(text_begin, text_end, f_begin, f_end);
        if (is_exclude) {
            if (found != text_end) return false; // excluded token present
        } else {
            if (found != text_end) include_matched = true;
        }
    }

    if (include_count > 0)
        return include_matched; // require at least one include to match

    return true; // only excludes were specified and none matched
}

void OutputWindow::show() {
    if (hasNotification) {
        App::pushTabNotificationStyle();
    }
    if (!ImGui::Begin(OutputWindow::WINDOW_NAME, nullptr, hasNotification ? ImGuiWindowFlags_UnsavedDocument : 0)) {
        if (hasNotification) App::popTabNotificationStyle();
        isWindowVisible = false;
        ImGui::End();
        return;
    }
    if (hasNotification) App::popTabNotificationStyle();

    isWindowVisible = true;
    hasNotification = false;

    // Calculate dimensions for the vertical menu and main content
    menuWidth = ImGui::CalcTextSize(ICON_FA_LOCK).x + ImGui::GetStyle().ItemSpacing.x * 2 + ImGui::GetStyle().FramePadding.x * 2;
    const ImVec2 windowSize = ImGui::GetContentRegionAvail();

    // Begin vertical menu
    ImGui::BeginChild("VerticalMenu", ImVec2(menuWidth, windowSize.y), true);

    // Auto-scroll lock button
    const bool highlightSB = autoScrollLockedButton && autoScroll;
    if (highlightSB) {
        ImGui::PushStyleColor(ImGuiCol_Button, App::ThemeColors::ButtonActivated);
    }
    float w = ImGui::CalcTextSize(ICON_FA_LOCK).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    if (ImGui::Button(autoScrollLockedButton ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN, ImVec2(w, 0.0f))) {
        autoScrollLockedButton = !autoScrollLockedButton;
        if (autoScrollLockedButton) {
            autoScroll = true; // Enable auto-scroll when locking
        }
    }
    if (highlightSB) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(autoScrollLockedButton ? "Disable Auto-scroll" : "Enable Auto-scroll");
    }

    // Filter button (filter icon)
    bool anyFilterDisabled = false;
    for (int i = 0; i < 5; i++) {
        if (!typeFilters[i]) {
            anyFilterDisabled = true;
            break;
        }
    }
    if (anyFilterDisabled || filter.IsActive()) {
        ImGui::PushStyleColor(ImGuiCol_Button, App::ThemeColors::ButtonActivated);
    }
    if (ImGui::Button(ICON_FA_FILTER)) {
        ImGui::OpenPopup("FilterPopup");
    }
    if (anyFilterDisabled || filter.IsActive()) {
        ImGui::PopStyleColor();
    }
    ImGui::SetNextWindowSize(ImVec2(200.0f, 0.0f));
    if (ImGui::BeginPopup("FilterPopup")) {
        bool filterChanged = false;

        // Type filters
        const char* filterNames[] = {"Info", "Warning", "Error", "Success", "Build"};
        for (int i = 0; i < 5; i++) {
            if (ImGui::Checkbox(filterNames[i], &typeFilters[i])) {
                filterChanged = true;
            }
        }

        ImGui::Separator();

        // ESC clears filter
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && ImGui::IsWindowFocused()) {
            filter.InputBuf[0] = '\0';
            filter.Build(); // IMPORTANT: rebuild internal tokens
            needsRebuild = true;
        }

        bool prevMatchCase = searchMatchCase;
        if (UIUtils::searchInput("##output_filter_input", "Filter...", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf), false, &searchMatchCase)) {
            filter.Build(); // IMPORTANT: rebuild internal tokens
            needsRebuild = true;
        }
        if (prevMatchCase != searchMatchCase) {
            needsRebuild = true; // case mode changed
        }

        if (filterChanged) {
            needsRebuild = true;
        }

        ImGui::EndPopup();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Filter Logs");
    }

    // Clear button (trash icon)
    if (ImGui::Button(ICON_FA_TRASH)) {
        clear();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear output");
    }

    ImGui::EndChild();

    // Main content area
    ImGui::SameLine(menuWidth);

    // Custom colored, selectable log viewer
    ImGui::BeginChild("##output", ImVec2(0, 0), false,
                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Compute effective wrap width inside this child (use content width directly)
    static float lastWrapW = -1.0f;
    float content_w = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
    float wrapW = ImMax(1.0f, content_w);

    // If logs/filters changed or width changed, rebuild
    if (needsRebuild || (lastWrapW < 0.0f) || (fabsf(lastWrapW - wrapW) > 0.5f)) {
        rebuildBuffer(wrapW);
        needsRebuild = false;
        lastWrapW = wrapW;
    }

    // Measurements (must come before using totalLines)
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize();
    const float lineBoxHeight = ImGui::GetTextLineHeight(); // text box height
    const float lineAdvance  = lineBoxHeight + ImGui::GetStyle().ItemSpacing.y; // exact row-to-row advance
    const int totalLines = (lineOffsets.Size > 0) ? (lineOffsets.Size - 1) : 0;
    if ((int)lineTypes.size() != totalLines) {
        rebuildBuffer(wrapW);
    }

    // Keyboard shortcuts
    const bool winFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const ImGuiIO& io = ImGui::GetIO();
    if (winFocused) {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            selectionStart = 0;
            selectionEnd = buf.size();
            hasStoredSelection = (selectionEnd > selectionStart);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            if (hasStoredSelection) {
                int a = std::min(selectionStart, selectionEnd);
                int b = std::max(selectionStart, selectionEnd);
                a = std::max(0, a);
                b = std::min((int)buf.size(), b);
                if (b > a) {
                    // Build copy text stripping soft-wrap newlines AND their leading indentation
                    auto lineStartIndexFn = [&](int line)->int { return (line >= 0 && line < lineOffsets.Size) ? lineOffsets[line] : 0; };
                    auto lineEndIndexExclFn = [&](int line)->int {
                        if (line < 0 || line + 1 >= lineOffsets.Size) return (int)buf.size();
                        int end = lineOffsets[line + 1];
                        if (end > 0 && end <= buf.size() && buf[end - 1] == '\n') end -= 1;
                        return end;
                    };
                    auto idxToLine = [&](int idx)->int {
                        int l = 0, r = ((lineOffsets.Size > 0) ? lineOffsets.Size - 2 : -1), ans = 0;
                        while (l <= r) {
                            int m = (l + r) >> 1;
                            if (lineOffsets[m] <= idx) { ans = m; l = m + 1; }
                            else r = m - 1;
                        }
                        int tl = (lineOffsets.Size > 0) ? (lineOffsets.Size - 1) : 0;
                        return std::max(0, std::min(ans, tl > 0 ? tl - 1 : 0));
                    };

                    std::string out;
                    out.reserve(b - a);
                    if (totalLines > 0) {
                        int startLine = idxToLine(a);
                        int endLine   = idxToLine(std::max(a, b - 1));
                        for (int i = startLine; i <= endLine; ++i) {
                            int ls = lineStartIndexFn(i);
                            int le = lineEndIndexExclFn(i);

                            int s0 = std::max(a, ls);
                            int e0 = std::min(b, le);

                            // If this line continues from a soft wrap, drop the leading indentation we injected
                            bool prevSoft = (i > 0) && (i - 1) < (int)lineHardBreak.size() && (lineHardBreak[i - 1] == 0);
                            if (prevSoft) {
                                int indentMax = (int)getTypePrefixString(
                                    (i >= 0 && i < (int)lineTypes.size()) ? lineTypes[i] : LogType::Info
                                ).size();
                                int indentActual = 0;
                                int scanEnd = std::min(le, ls + indentMax);
                                while (ls + indentActual < scanEnd && buf[ls + indentActual] == ' ')
                                    indentActual++;
                                int trimTo = ls + indentActual;
                                if (e0 > trimTo) s0 = std::max(s0, trimTo);
                                else            s0 = e0; // selection fully inside indent -> nothing to add
                            }

                            if (e0 > s0) out.append(buf.begin() + s0, buf.begin() + e0);

                            // If selection includes this line's newline and it is a hard break, append '\n'
                            if (i + 1 < lineOffsets.Size) {
                                int nextStart = lineOffsets[i + 1];
                                bool hadNewlineChar = (nextStart > 0 && nextStart <= buf.size() && buf[nextStart - 1] == '\n');
                                int nlPos = hadNewlineChar ? (nextStart - 1) : -1;
                                if (hadNewlineChar && i < (int)lineHardBreak.size() && lineHardBreak[i] && nlPos >= a && nlPos < b) {
                                    out.push_back('\n');
                                }
                            }
                        }
                    }
                    ImGui::SetClipboardText(out.c_str());
                }
            }
        }
    }

    // Helper lambdas
    auto lineStartIndex = [&](int line)->int { return (line >= 0 && line < lineOffsets.Size) ? lineOffsets[line] : 0; };
    auto lineEndIndexExcl = [&](int line)->int {
        if (line < 0 || line + 1 >= lineOffsets.Size) return buf.size();
        int end = lineOffsets[line + 1];
        if (end > 0 && end <= buf.size() && buf[end - 1] == '\n') end -= 1;
        return end;
    };

    // Prepare hover info for this frame (determined inside clipper while drawing)
    int   hoveredLine   = -1;
    float hoveredLocalX = 0.0f;

    // Draw with clipping (fast for large logs)
    ImGuiListClipper clipper;
    clipper.Begin(totalLines, lineAdvance);

    const ImU32 selBgCol = ImGui::GetColorU32(ImGui::GetStyle().Colors[ImGuiCol_TextSelectedBg]);

    int selA = selectionStart, selB = selectionEnd;
    if (selA > selB) std::swap(selA, selB);

    while (clipper.Step()) {
        for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line) {
            int ls = lineStartIndex(line);
            int le = lineEndIndexExcl(line);

            ImVec2 linePos = ImGui::GetCursorScreenPos();

            // Hit-test for this visual row (use full row advance so spacing clicks count)
            ImRect lineHitRect(
                ImVec2(linePos.x, linePos.y),
                ImVec2(linePos.x + wrapW, linePos.y + lineAdvance)
            );
            if (hoveredLine == -1 && ImGui::IsMouseHoveringRect(lineHitRect.Min, lineHitRect.Max)) {
                hoveredLine   = line;
                hoveredLocalX = ImGui::GetMousePos().x - linePos.x;
            }

            // Selection highlight for the portion of this line that is selected
            if (hasStoredSelection && selA >= 0 && selB >= 0 && selB > selA) {
                int hlStart = std::max(ls, selA);
                int hlEnd   = std::min(le, selB);
                if (hlStart < hlEnd) {
                    const char* s0 = buf.begin() + ls;
                    const char* s1 = buf.begin() + hlStart;
                    const char* s2 = buf.begin() + hlEnd;

                    float x0 = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s0, s1).x;
                    float x1 = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, s1, s2).x;

                    ImGui::GetWindowDrawList()->AddRectFilled(
                        ImVec2(linePos.x + x0, linePos.y),
                        ImVec2(linePos.x + x0 + x1, linePos.y + lineBoxHeight),
                        selBgCol
                    );
                }
            }

            // Colored text by type
            ImVec4 color = (line >= 0 && line < (int)lineTypes.size()) ? GetLogTypeColor(lineTypes[line])
                                                                       : ImVec4(1,1,1,1);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (ls < le) {
                ImGui::TextUnformatted(buf.begin() + ls, buf.begin() + le);
            } else {
                ImGui::TextUnformatted(""); // empty line
            }
            ImGui::PopStyleColor();
        }
    }
    clipper.End();

    // Map (line, localX) to buffer index (character-precise)
    auto indexFromLineAndLocalX = [&](int line, float localX)->int {
        if (line < 0 || totalLines <= 0) return 0;
        int ls = lineStartIndex(line);
        int le = lineEndIndexExcl(line);
        if (ls >= le) return ls;

        const char* s = buf.begin() + ls;
        const char* e = buf.begin() + le;

        float x = 0.0f;
        int idx = ls;
        for (const char* p = s; p < e; ) {
            unsigned int c;
            int c_len = ImTextCharFromUtf8(&c, p, e);
            if (c_len <= 0) break;

            float cw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, p, p + c_len).x;
            if (localX < x + cw * 0.5f)
                return idx;

            x   += cw;
            p   += c_len;
            idx += c_len;
        }
        return le; // past end of line
    };

    // Handle mouse selection using the hovered row computed from actual drawn rectangles
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
        if (hoveredLine >= 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        }

        // If cursor didn't hover a drawn row (e.g., outside vertically), derive nearest row from Y
        int   lineForMouse = hoveredLine;
        float xForMouse    = hoveredLocalX;
        if (lineForMouse < 0 && totalLines > 0) {
            ImVec2 innerMin(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x,
                            ImGui::GetWindowPos().y + ImGui::GetWindowContentRegionMin().y);
            float localY = ImGui::GetMousePos().y - innerMin.y + ImGui::GetScrollY();
            lineForMouse = (int)std::floor(localY / lineAdvance);
            lineForMouse = std::max(0, std::min(lineForMouse, totalLines - 1));
            // Snap X to start/end when outside horizontally
            xForMouse = (ImGui::GetMousePos().x < innerMin.x) ? 0.0f : 1e9f;
        }

        // Multi-click detection via Dear ImGui
        // 1 click: start drag-selection
        // 2 clicks: word selection
        // 3 clicks: line selection
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int idx = indexFromLineAndLocalX(lineForMouse, xForMouse);
            int clicks = ImGui::GetIO().MouseClickedCount[ImGuiMouseButton_Left];

            if (clicks >= 3) {
                selectLineAt(idx);
                isSelecting = false;
            } else if (clicks == 2) {
                selectWordAt(idx);
                isSelecting = false;
            } else {
                selectionStart = selectionEnd = idx;
                hasStoredSelection = true;
                isSelecting = true;
            }
        } else if (isSelecting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            int idx = indexFromLineAndLocalX(lineForMouse, xForMouse);
            selectionEnd = idx;
            hasStoredSelection = (selectionStart != selectionEnd);
        } else if (isSelecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            isSelecting = false;
        }
    }

    // Context menu for copying selected text
    if (ImGui::BeginPopupContextWindow("##output_context", ImGuiPopupFlags_MouseButtonRight)) {
        int a = std::min(selectionStart, selectionEnd);
        int b = std::max(selectionStart, selectionEnd);
        a = std::max(0, a);
        b = std::min((int)buf.size(), b);
        bool canCopy = hasStoredSelection && (b > a);

        if (ImGui::MenuItem(ICON_FA_COPY "  Copy", NULL, false, canCopy)) {
            if (canCopy) {
                // Build copy text stripping soft-wrap newlines AND their leading indentation
                auto lineStartIndexFn = [&](int line)->int { return (line >= 0 && line < lineOffsets.Size) ? lineOffsets[line] : 0; };
                auto lineEndIndexExclFn = [&](int line)->int {
                    if (line < 0 || line + 1 >= lineOffsets.Size) return (int)buf.size();
                    int end = lineOffsets[line + 1];
                    if (end > 0 && end <= buf.size() && buf[end - 1] == '\n') end -= 1;
                    return end;
                };
                auto idxToLine = [&](int idx)->int {
                    int l = 0, r = ((lineOffsets.Size > 0) ? lineOffsets.Size - 2 : -1), ans = 0;
                    while (l <= r) {
                        int m = (l + r) >> 1;
                        if (lineOffsets[m] <= idx) { ans = m; l = m + 1; }
                        else r = m - 1;
                    }
                    int tl = (lineOffsets.Size > 0) ? (lineOffsets.Size - 1) : 0;
                    return std::max(0, std::min(ans, tl > 0 ? tl - 1 : 0));
                };

                std::string out;
                out.reserve(b - a);
                if (totalLines > 0) {
                    int startLine = idxToLine(a);
                    int endLine   = idxToLine(std::max(a, b - 1));
                    for (int i = startLine; i <= endLine; ++i) {
                        int ls = lineStartIndexFn(i);
                        int le = lineEndIndexExclFn(i);

                        int s0 = std::max(a, ls);
                        int e0 = std::min(b, le);

                        // If this line continues from a soft wrap, drop the leading indentation we injected
                        bool prevSoft = (i > 0) && (i - 1) < (int)lineHardBreak.size() && (lineHardBreak[i - 1] == 0);
                        if (prevSoft) {
                            int indentMax = (int)getTypePrefixString(
                                (i >= 0 && i < (int)lineTypes.size()) ? lineTypes[i] : LogType::Info
                            ).size();
                            int indentActual = 0;
                            int scanEnd = std::min(le, ls + indentMax);
                            while (ls + indentActual < scanEnd && buf[ls + indentActual] == ' ')
                                indentActual++;
                            int trimTo = ls + indentActual;
                            if (e0 > trimTo) s0 = std::max(s0, trimTo);
                            else            s0 = e0;
                        }

                        if (e0 > s0) out.append(buf.begin() + s0, buf.begin() + e0);

                        bool hadNewlineChar = false;
                        int nlPos = -1;
                        if (i + 1 < lineOffsets.Size) {
                            int nextStart = lineOffsets[i + 1];
                            hadNewlineChar = (nextStart > 0 && nextStart <= buf.size() && buf[nextStart - 1] == '\n');
                            nlPos = hadNewlineChar ? (nextStart - 1) : -1;
                        }
                        if (hadNewlineChar && i < (int)lineHardBreak.size() && lineHardBreak[i] && nlPos >= a && nlPos < b) {
                            out.push_back('\n');
                        }
                    }
                }
                ImGui::SetClipboardText(out.c_str());
            }
        }
        if (ImGui::MenuItem("Select All")) {
            selectionStart = 0;
            selectionEnd = buf.size();
            hasStoredSelection = selectionEnd > selectionStart;
        }
        ImGui::EndPopup();
    }

    // Detect manual scrolling and disable auto-scroll if user scrolled away from bottom
    float currentScrollY = ImGui::GetScrollY();
    float maxScrollY = ImGui::GetScrollMaxY();

    // Check if user manually scrolled
    if (!isSelecting && fabsf(currentScrollY - lastScrollY) > 0.1f) {
        if (currentScrollY < maxScrollY - 1.0f) {
            // User manually scrolled away from bottom, disable auto-scroll
            autoScroll = false;
        } else if (currentScrollY >= maxScrollY - 1.0f) {
            // User manually scrolled back to bottom, re-enable auto-scroll
            autoScroll = true;
        }
    }

    // Auto-scroll: pin to bottom if locked (avoid fighting user drag-selection)
    if (autoScroll && autoScrollLockedButton && !isSelecting) {
        ImGui::SetScrollY(maxScrollY);
        currentScrollY = maxScrollY; // update current position after auto-scroll
    }

    // Store current scroll position for next frame comparison
    lastScrollY = currentScrollY;

    ImGui::EndChild();
    ImGui::End();
}