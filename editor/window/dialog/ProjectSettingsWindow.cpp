// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ProjectSettingsWindow.h"
#include "util/FileDialogs.h"
#include "Backend.h"
#include "window/Widgets.h"
#include "Theme.h"
#include "external/IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace doriax::editor {

static const char* scalingModeNames[] = { "Fit Width", "Fit Height", "Letterbox", "Crop", "Stretch", "Native" };
static const Scaling scalingModeValues[] = { Scaling::FITWIDTH, Scaling::FITHEIGHT, Scaling::LETTERBOX, Scaling::CROP, Scaling::STRETCH, Scaling::NATIVE };
static const int scalingModeCount = sizeof(scalingModeValues) / sizeof(scalingModeValues[0]);

static const char* textureStrategyNames[] = { "Fit", "Resize", "None" };
static const TextureStrategy textureStrategyValues[] = { TextureStrategy::FIT, TextureStrategy::RESIZE, TextureStrategy::NONE };
static const int textureStrategyCount = sizeof(textureStrategyValues) / sizeof(textureStrategyValues[0]);

static const char* windowModeNames[] = { "Windowed", "Maximized", "Fullscreen" };
static const WindowMode windowModeValues[] = { WindowMode::WINDOWED, WindowMode::MAXIMIZED, WindowMode::FULLSCREEN };
static const int windowModeCount = sizeof(windowModeValues) / sizeof(windowModeValues[0]);

static const char* cxxStandardNames[] = { "C++17", "C++20", "C++23" };
static const int cxxStandardCount = sizeof(cxxStandardNames) / sizeof(cxxStandardNames[0]);
static_assert(std::size(cxxStandardNames) == std::size(cxxStandards));

static const char* androidOrientationNames[] = { "Unspecified", "Portrait", "Landscape", "Sensor Portrait", "Sensor Landscape", "Full Sensor" };
static const AndroidOrientation androidOrientationValues[] = {
    AndroidOrientation::Unspecified,
    AndroidOrientation::Portrait,
    AndroidOrientation::Landscape,
    AndroidOrientation::SensorPortrait,
    AndroidOrientation::SensorLandscape,
    AndroidOrientation::FullSensor
};
static const int androidOrientationCount = sizeof(androidOrientationValues) / sizeof(androidOrientationValues[0]);

struct AndroidPermissionInfo {
    const char* key;
    const char* manifestName;
};

static const AndroidPermissionInfo androidPermissionInfos[] = {
    { "internet", "INTERNET" },
    { "access_network_state", "ACCESS_NETWORK_STATE" },
    { "access_wifi_state", "ACCESS_WIFI_STATE" },
    { "change_network_state", "CHANGE_NETWORK_STATE" },
    { "change_wifi_state", "CHANGE_WIFI_STATE" },
    { "vibrate", "VIBRATE" },
    { "wake_lock", "WAKE_LOCK" },
    { "post_notifications", "POST_NOTIFICATIONS" },
    { "camera", "CAMERA" },
    { "record_audio", "RECORD_AUDIO" },
    { "access_coarse_location", "ACCESS_COARSE_LOCATION" },
    { "access_fine_location", "ACCESS_FINE_LOCATION" },
    { "access_location_extra_commands", "ACCESS_LOCATION_EXTRA_COMMANDS" },
    { "access_media_location", "ACCESS_MEDIA_LOCATION" },
    { "read_external_storage", "READ_EXTERNAL_STORAGE" },
    { "write_external_storage", "WRITE_EXTERNAL_STORAGE" },
    { "manage_external_storage", "MANAGE_EXTERNAL_STORAGE" },
    { "read_media_audio", "READ_MEDIA_AUDIO" },
    { "read_media_images", "READ_MEDIA_IMAGES" },
    { "read_media_video", "READ_MEDIA_VIDEO" },
    { "read_media_visual_user_selected", "READ_MEDIA_VISUAL_USER_SELECTED" },
    { "bluetooth", "BLUETOOTH" },
    { "bluetooth_admin", "BLUETOOTH_ADMIN" },
    { "bluetooth_connect", "BLUETOOTH_CONNECT" },
    { "bluetooth_scan", "BLUETOOTH_SCAN" },
    { "nfc", "NFC" },
    { "transmit_ir", "TRANSMIT_IR" },
    { "use_biometric", "USE_BIOMETRIC" },
    { "use_fingerprint", "USE_FINGERPRINT" },
    { "read_contacts", "READ_CONTACTS" },
    { "write_contacts", "WRITE_CONTACTS" },
    { "get_accounts", "GET_ACCOUNTS" },
    { "read_calendar", "READ_CALENDAR" },
    { "write_calendar", "WRITE_CALENDAR" },
    { "read_call_log", "READ_CALL_LOG" },
    { "write_call_log", "WRITE_CALL_LOG" },
    { "read_phone_state", "READ_PHONE_STATE" },
    { "call_phone", "CALL_PHONE" },
    { "read_sms", "READ_SMS" },
    { "write_sms", "WRITE_SMS" },
    { "send_sms", "SEND_SMS" },
    { "receive_sms", "RECEIVE_SMS" },
    { "receive_mms", "RECEIVE_MMS" },
    { "receive_wap_push", "RECEIVE_WAP_PUSH" },
    { "receive_boot_completed", "RECEIVE_BOOT_COMPLETED" },
    { "kill_background_processes", "KILL_BACKGROUND_PROCESSES" },
    { "modify_audio_settings", "MODIFY_AUDIO_SETTINGS" },
    { "set_wallpaper", "SET_WALLPAPER" },
    { "set_wallpaper_hints", "SET_WALLPAPER_HINTS" },
    { "write_settings", "WRITE_SETTINGS" },
};

static const int androidPermissionCount = sizeof(androidPermissionInfos) / sizeof(androidPermissionInfos[0]);

static constexpr float dialogWidth = 750.0f;
static constexpr float dialogHeight = 600.0f;
static constexpr float settingsLabelWidth = 200.0f;
static constexpr float settingsPanelPadding = 12.0f;
static constexpr float settingsButtonWidth = 120.0f;
static constexpr ImGuiWindowFlags noScrollFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

static int findScalingIndex(Scaling mode) {
    for (int i = 0; i < scalingModeCount; i++) {
        if (scalingModeValues[i] == mode) return i;
    }
    return 0;
}

static int findTextureStrategyIndex(TextureStrategy strategy) {
    for (int i = 0; i < textureStrategyCount; i++) {
        if (textureStrategyValues[i] == strategy) return i;
    }
    return 0;
}

static int findWindowModeIndex(WindowMode mode) {
    for (int i = 0; i < windowModeCount; i++) {
        if (windowModeValues[i] == mode) return i;
    }
    return 0;
}

static int findCxxStandardIndex(int standard) {
    for (int i = 0; i < cxxStandardCount; i++) {
        if (cxxStandards[i] == standard) return i;
    }
    return 0;
}

static int findAndroidOrientationIndex(AndroidOrientation orientation) {
    for (int i = 0; i < androidOrientationCount; i++) {
        if (androidOrientationValues[i] == orientation) return i;
    }
    return 0;
}

// Opening and accepting the dialog must not truncate untouched persisted text.
template <size_t N>
static void applyTextBuffer(std::string& value, const char (&buffer)[N]) {
    if (value.compare(0, N - 1, buffer) != 0) {
        value = buffer;
    }
}

static bool isIdentifierPartValid(const std::string& value, size_t start, size_t end) {
    if (start >= end) return false;
    unsigned char first = static_cast<unsigned char>(value[start]);
    if (!std::isalpha(first) && value[start] != '_') return false;

    for (size_t i = start + 1; i < end; i++) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (!std::isalnum(c) && value[i] != '_') return false;
    }
    return true;
}

static bool isAppleIdentifierPartValid(const std::string& value, size_t start, size_t end) {
    if (start >= end || value[start] == '-' || value[end - 1] == '-') return false;

    for (size_t i = start; i < end; i++) {
        unsigned char c = static_cast<unsigned char>(value[i]);
        if (!std::isalnum(c) && value[i] != '-') return false;
    }
    return true;
}

static bool isSharedIdentifierPartValid(const std::string& value, size_t start, size_t end) {
    if (start >= end || !std::isalpha(static_cast<unsigned char>(value[start]))) return false;

    for (size_t i = start; i < end; i++) {
        if (!std::isalnum(static_cast<unsigned char>(value[i]))) return false;
    }
    return true;
}

using IdentifierPartValidator = bool (*)(const std::string&, size_t, size_t);

static bool isDottedIdentifierValid(const std::string& value, IdentifierPartValidator partValid) {
    if (value.empty() || value.back() == '.') return false;
    size_t start = 0;
    int parts = 0;
    while (start < value.size()) {
        size_t dot = value.find('.', start);
        size_t end = dot == std::string::npos ? value.size() : dot;
        if (!partValid(value, start, end)) return false;
        parts++;
        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return parts >= 2;
}

static bool isJavaPackageNameValid(const std::string& value) {
    return isDottedIdentifierValid(value, isIdentifierPartValid);
}

// Apple allows the hyphens Android rejects, and rejects the underscores it allows.
static bool isAppleBundleIdentifierValid(const std::string& value) {
    return isDottedIdentifierValid(value, isAppleIdentifierPartValid);
}

// The shared id reaches both, so only what Apple and Android accept in common.
static bool isSharedIdentifierValid(const std::string& value) {
    return isDottedIdentifierValid(value, isSharedIdentifierPartValid);
}

// One to maxParts numeric parts, none above maxPart. Windows pads shorter values
// to the four its VERSIONINFO resource needs; Apple takes at most three.
static bool isNumericVersionValid(const std::string& value, int maxParts, unsigned int maxPart) {
    if (value.empty()) return false;

    size_t start = 0;
    int parts = 0;
    bool consumedAll = false;
    while (start <= value.size() && parts < maxParts) {
        size_t dot = value.find('.', start);
        size_t end = dot == std::string::npos ? value.size() : dot;
        if (start >= end) return false;

        unsigned long long part = 0;
        for (size_t i = start; i < end; i++) {
            unsigned char c = static_cast<unsigned char>(value[i]);
            if (!std::isdigit(c)) return false;
            part = part * 10 + static_cast<unsigned>(value[i] - '0');
            if (part > maxPart) return false;
        }

        parts++;
        if (dot == std::string::npos) {
            consumedAll = true;
            break;
        }
        start = dot + 1;
    }

    return parts >= 1 && consumedAll;
}

// The shared version reaches Apple too, so it takes the stricter limit.
static bool isAppleVersionValid(const std::string& value) {
    return isNumericVersionValid(value, 3, 65535);
}

// Build numbers are commonly date stamps, so only the plist part count applies.
static bool isAppleBuildValid(const std::string& value) {
    return isNumericVersionValid(value, 3, 0xFFFFFFFFu);
}

static bool isWindowsVersionValid(const std::string& value) {
    return isNumericVersionValid(value, 4, 65535);
}

// A prefix like "12abc", a zero or an out-of-range value yields 0, read as inherit.
static unsigned int parseAndroidVersionCode(const std::string& value) {
    if (value.empty() || value.size() > 10) return 0;

    unsigned long long parsed = 0;
    for (char c : value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
        parsed = parsed * 10 + static_cast<unsigned>(c - '0');
    }

    if (parsed < 1 || parsed > 2100000000ull) return 0;  // Google Play's ceiling
    return static_cast<unsigned int>(parsed);
}

static bool isAndroidVersionCodeValid(const std::string& value) {
    return parseAndroidVersionCode(value) > 0;
}

// Typing the hint into an inheriting field keeps it inheriting; an override the
// user had already set stays put even when the shared value catches up with it.
template <size_t N>
static void applyOverride(std::string& value, const char (&buffer)[N], const std::string& inherited) {
    const bool wasInheriting = value.empty();
    applyTextBuffer(value, buffer);
    if (wasInheriting && value == inherited) value.clear();
}

template <size_t N>
static void applyOverride(std::string& value, const char (&buffer)[N], const std::string& inherited, bool (*isValid)(const std::string&)) {
    applyOverride(value, buffer, inherited);
    if (!value.empty() && !isValid(value)) value.clear();
}

// Apple caps its plist versions at three parts, so trim instead of dropping.
template <size_t N>
static void applyAppleVersion(std::string& value, const char (&buffer)[N], const std::string& inherited, bool (*isValid)(const std::string&)) {
    applyOverride(value, buffer, inherited);
    if (!value.empty() && !isValid(value)) value = Project::toAppleVersion(value);
}

static bool hasAndroidAbiSelection(bool armeabiV7a, bool arm64V8a, bool x86, bool x86_64) {
    return armeabiV7a || arm64V8a || x86 || x86_64;
}

// One scrolling child per tab; sections inside it are a collapsing header
// followed by a table, like the Terrain editor panels.
static bool beginSettingsPanel(const char* panelId) {
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::dpi(ImVec2(settingsPanelPadding, settingsPanelPadding)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.CellPadding.x, Theme::dpi(6.0f)));

    // Allow vertical scrolling only when content exceeds the fixed dialog;
    // ImGui hides the scrollbar while everything fits.
    return ImGui::BeginChild(panelId, ImVec2(0, 0), ImGuiChildFlags_Borders);
}

static void endSettingsPanel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
}

static bool beginSettingsTable(const char* tableId) {
    float labelWidth = std::min(Theme::dpi(settingsLabelWidth), ImGui::GetContentRegionAvail().x * 0.4f);
    if (!ImGui::BeginTable(tableId, 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        return false;
    }
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

template <typename DrawContents>
static void drawSettingsPanel(const char* panelId, DrawContents drawContents) {
    if (beginSettingsPanel(panelId) && beginSettingsTable("##SettingsTable")) {
        drawContents();
        ImGui::EndTable();
    }
    endSettingsPanel();
}

// Section text that belongs outside the row table.
static void drawSectionNote(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

static void helpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Width the value widget gives up so the marker fits beside it.
static float helpMarkerWidth() {
    return ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x;
}

// Returns true when the reset arrow is clicked, like the property rows in Properties.
static bool beginSettingsRow(const char* label, bool defChanged = false, const char* resetTooltip = "Restore default") {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    bool reset = false;
    if (defChanged) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, style.ItemSpacing.y));
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, style.FramePadding.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
        reset = ImGui::Button((ICON_FA_ROTATE_LEFT "##" + std::string(label)).c_str());
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        ImGui::SetItemTooltip("%s", resetTooltip);
    }

    ImGui::TableNextColumn();
    return reset;
}

static void endSettingsRow(const char* tooltip) {
    if (!tooltip) return;
    ImGui::SameLine();
    helpMarker(tooltip);
}

// An empty field inherits, and the hint shows the value it inherits.
template <size_t N>
static void drawOverrideSetting(const char* label, const char* id, char (&buffer)[N], const std::string& inherited, const char* tooltip) {
    if (beginSettingsRow(label, buffer[0] != '\0', "Use the shared value")) {
        buffer[0] = '\0';
    }
    ImGui::SetNextItemWidth(tooltip ? -helpMarkerWidth() : -1.0f);
    ImGui::InputTextWithHint(id, inherited.c_str(), buffer, N);
    endSettingsRow(tooltip);
}

// Shows what the resource carries when the typed value is not already padded.
// Invalid text inherits on apply, so it must not report a padded form here.
template <size_t N>
static void drawWindowsVersionResult(const char (&buffer)[N]) {
    if (buffer[0] == '\0' || !isWindowsVersionValid(buffer)) return;
    const std::string padded = Project::toFourPartVersion(buffer);
    if (padded == buffer) return;
    ImGui::TextDisabled("Exports as %s", padded.c_str());
}

// Only fires on a typed value; an inherited field is validated where it is entered.
template <size_t N>
static void drawOverrideWarning(const char (&buffer)[N], bool (*isValid)(const std::string&), const char* message) {
    if (buffer[0] == '\0' || isValid(buffer)) return;
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "%s", message);
}

static void drawComboSetting(const char* label, const char* id, const char* const* names, int count, int& selectedIndex, int defaultIndex, const char* tooltip = nullptr) {
    if (beginSettingsRow(label, selectedIndex != defaultIndex)) {
        selectedIndex = defaultIndex;
    }
    if (selectedIndex < 0 || selectedIndex >= count) selectedIndex = 0;

    ImGui::SetNextItemWidth(tooltip ? -helpMarkerWidth() : -1.0f);
    if (ImGui::BeginCombo(id, names[selectedIndex])) {
        for (int i = 0; i < count; i++) {
            bool selected = selectedIndex == i;
            if (ImGui::Selectable(names[i], selected)) selectedIndex = i;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    endSettingsRow(tooltip);
}

static void drawIntSetting(const char* label, const char* id, int& value, int defaultValue, int minValue = 1, const char* tooltip = nullptr) {
    if (beginSettingsRow(label, value != defaultValue)) {
        value = defaultValue;
    }
    ImGui::SetNextItemWidth(tooltip ? -helpMarkerWidth() : -1.0f);
    ImGui::InputInt(id, &value);
    value = std::max(value, minValue);
    endSettingsRow(tooltip);
}

static void drawDirectorySetting(
    Project* project,
    const char* label,
    const char* tooltip,
    const char* pathId,
    const char* buttonId,
    fs::path& directory,
    const fs::path& defaultDirectory,
    bool keepAbsolutePathOnError = false
) {
    // An empty directory already means the default root
    if (beginSettingsRow(label, !directory.empty() && directory != defaultDirectory)) {
        directory = defaultDirectory;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    float browseWidth = ImGui::CalcTextSize("Browse").x + style.FramePadding.x * 2.0f;
    float pathWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - browseWidth - style.ItemSpacing.x - (tooltip ? helpMarkerWidth() : 0.0f));

    fs::path displayPath = directory.empty() ? defaultDirectory : directory;
    if (displayPath == ".") displayPath = "<Project root>";
    Widgets::pathDisplay(pathId, displayPath, Vector2(pathWidth, ImGui::GetFrameHeight()));

    ImGui::SameLine();
    bool browse = false;
    if (project) {
        browse = ImGui::Button(buttonId);
    } else {
        ImGui::BeginDisabled();
        ImGui::Button(buttonId);
        ImGui::EndDisabled();
    }
    endSettingsRow(tooltip);

    if (!browse) return;

    std::string selectedPath = FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_ALL, true);
    if (selectedPath.empty()) return;

    std::error_code ec;
    fs::path relativePath = fs::relative(fs::path(selectedPath), project->getProjectPath(), ec);
    if (!ec && !relativePath.empty()) {
        directory = relativePath;
    } else {
        directory = keepAbsolutePathOnError ? fs::path(selectedPath) : defaultDirectory;
    }
}

static void drawScriptDirsSetting(Project* project, std::vector<fs::path>& directories) {
    if (beginSettingsRow("Script Directories", !directories.empty())) {
        directories.clear();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float removeWidth = ImGui::CalcTextSize(ICON_FA_TRASH_CAN).x + style.FramePadding.x * 2.0f;

    size_t removeIndex = directories.size();
    for (size_t i = 0; i < directories.size(); i++) {
        ImGui::PushID(static_cast<int>(i));

        float pathWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - removeWidth - style.ItemSpacing.x);
        Widgets::pathDisplay("##ScriptDir", directories[i], Vector2(pathWidth, ImGui::GetFrameHeight()));

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
        if (ImGui::Button(ICON_FA_TRASH_CAN "##RemoveScriptDir", ImVec2(removeWidth, 0))) {
            removeIndex = i;
        }
        ImGui::PopStyleColor(2);

        ImGui::PopID();
    }

    if (removeIndex < directories.size()) {
        directories.erase(directories.begin() + static_cast<std::ptrdiff_t>(removeIndex));
    }

    bool add = false;
    if (project) {
        add = ImGui::Button(ICON_FA_PLUS " Add##AddScriptDir");
    } else {
        ImGui::BeginDisabled();
        ImGui::Button(ICON_FA_PLUS " Add##AddScriptDir");
        ImGui::EndDisabled();
    }
    endSettingsRow("Extra C++ roots. Each one is an include directory, and the sources under it "
        "are compiled without a script component referencing them.");

    if (!add) return;

    std::string selectedPath = FileDialogs::openFileDialog(project->getProjectPath().string(), FILE_DIALOG_ALL, true);
    if (selectedPath.empty()) return;

    // Only a root inside the project survives the export, and fs::relative reaches
    // a sibling through ".."; anything outside is stored whole and builds locally.
    std::error_code ec;
    fs::path relativePath = fs::relative(fs::path(selectedPath), project->getProjectPath(), ec);
    const bool insideProject = !ec && !relativePath.empty() && *relativePath.begin() != "..";
    fs::path directory = insideProject ? relativePath : fs::path(selectedPath);

    if (std::find(directories.begin(), directories.end(), directory) == directories.end()) {
        directories.push_back(std::move(directory));
    }
}

static void drawProjectFilePathSetting(
    Project* project,
    const char* label,
    const char* tooltip,
    const char* pathId,
    const char* browseId,
    const char* clearId,
    fs::path& imagePath,
    int filter = FILE_DIALOG_IMAGE
) {
    beginSettingsRow(label);

    const ImGuiStyle& style = ImGui::GetStyle();
    float browseWidth = ImGui::CalcTextSize("Browse").x + style.FramePadding.x * 2.0f;
    float clearWidth = ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f;
    float pathWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - browseWidth - clearWidth - style.ItemSpacing.x * 2.0f);

    fs::path displayPath = imagePath.empty() ? fs::path("<None>") : imagePath;
    Widgets::pathDisplay(pathId, displayPath, Vector2(pathWidth, ImGui::GetFrameHeight()));

    ImGui::SameLine();
    if (ImGui::Button(browseId)) {
        std::string defaultPath = project ? project->getProjectPath().string() : std::string();
        std::string selectedPath = FileDialogs::openFileDialog(defaultPath, filter, false);
        if (!selectedPath.empty()) {
            std::error_code ec;
            fs::path relPath = project ? fs::relative(fs::path(selectedPath), project->getProjectPath(), ec) : fs::path();
            if (!project || ec || relPath.empty() || relPath.string().rfind("..", 0) == 0) {
                imagePath = fs::path(selectedPath);
            } else {
                imagePath = relPath;
            }
        }
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(imagePath.empty());
    if (ImGui::Button(clearId)) {
        imagePath.clear();
    }
    ImGui::EndDisabled();
    endSettingsRow(tooltip);
}

static void showDisabledItemTooltip(const std::string& text) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) return;

    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

static void drawScalingPreview(Scaling mode, int canvasWidth, int canvasHeight) {
    if (canvasWidth <= 0 || canvasHeight <= 0) return;

    float canvasAspect = (float)canvasWidth / (float)canvasHeight;
    canvasAspect = std::clamp(canvasAspect, 15.0f / 70.0f, 80.0f / 15.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Base canvas size for preview. The bounded aspect ratio keeps the derived
    // height between 15 and 70 pixels.
    float baseW = std::clamp(50.0f * canvasAspect, 15.0f, 80.0f);
    float baseH = baseW / canvasAspect;

    // Three viewport configurations: reference, wider, taller
    float vpW[3] = { baseW * 1.15f, baseW * 1.8f,  baseW * 0.55f };
    float vpH[3] = { baseH * 1.15f, baseH * 0.7f,  baseH * 1.7f  };

    float spacing = 16.0f;
    float totalW = vpW[0] + vpW[1] + vpW[2] + spacing * 2;
    float maxH = 0;
    for (int i = 0; i < 3; i++) { if (vpH[i] > maxH) maxH = vpH[i]; }

    float availW = ImGui::GetContentRegionAvail().x;
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float offsetX = (availW - totalW) * 0.5f;
    if (offsetX < 0) offsetX = 0;
    float startX = cursor.x + offsetX;
    float startY = cursor.y;

    ImU32 colVpBg        = IM_COL32(215, 228, 243, 255);
    ImU32 colVpBorder    = IM_COL32(155, 165, 180, 255);
    ImU32 colCanvasFill  = IM_COL32(190, 210, 235, 255);
    ImU32 colCanvasBorder= IM_COL32(35, 50, 75, 255);
    ImU32 colTriangle    = IM_COL32(30, 80, 150, 255);
    ImU32 colBlack       = IM_COL32(15, 15, 20, 255);

    float baseTW = baseW * 0.35f;
    float baseTH = baseH * 0.4f;

    float curX = startX;

    for (int i = 0; i < 3; i++) {
        float vw = vpW[i];
        float vh = vpH[i];
        float vx = curX;
        float vy = startY + (maxH - vh) * 0.5f;

        ImVec2 vpMin(vx, vy);
        ImVec2 vpMax(vx + vw, vy + vh);

        // Compute scale factors based on scaling mode
        float scaleX, scaleY;
        switch (mode) {
        case Scaling::FITWIDTH:
            scaleX = scaleY = vw / baseW;
            break;
        case Scaling::FITHEIGHT:
            scaleX = scaleY = vh / baseH;
            break;
        case Scaling::LETTERBOX: {
            float s = (vw / baseW < vh / baseH) ? vw / baseW : vh / baseH;
            scaleX = scaleY = s;
            break;
        }
        case Scaling::CROP: {
            float s = (vw / baseW > vh / baseH) ? vw / baseW : vh / baseH;
            scaleX = scaleY = s;
            break;
        }
        case Scaling::STRETCH:
            scaleX = vw / baseW;
            scaleY = vh / baseH;
            break;
        case Scaling::NATIVE:
        default:
            scaleX = scaleY = 1.0f;
            break;
        }

        float dispW = baseW * scaleX;
        float dispH = baseH * scaleY;

        // Canvas centered in viewport
        float cX = vx + (vw - dispW) * 0.5f;
        float cY = vy + (vh - dispH) * 0.5f;
        ImVec2 cMin(cX, cY);
        ImVec2 cMax(cX + dispW, cY + dispH);

        // Clip drawing to viewport bounds
        dl->PushClipRect(vpMin, vpMax, true);

        // Viewport background
        if (mode == Scaling::LETTERBOX) {
            dl->AddRectFilled(vpMin, vpMax, colBlack);
        } else {
            dl->AddRectFilled(vpMin, vpMax, colVpBg);
        }

        // Canvas fill
        dl->AddRectFilled(cMin, cMax, colCanvasFill);

        // Triangle (content indicator)
        float triW = baseTW * scaleX;
        float triH = baseTH * scaleY;
        float triCX = cX + dispW * 0.5f;
        float triCY = cY + dispH * 0.5f;

        ImVec2 triP1(triCX, triCY - triH * 0.5f);
        ImVec2 triP2(triCX - triW * 0.5f, triCY + triH * 0.5f);
        ImVec2 triP3(triCX + triW * 0.5f, triCY + triH * 0.5f);
        dl->AddTriangleFilled(triP1, triP2, triP3, colTriangle);
        dl->AddTriangle(triP1, triP2, triP3, colCanvasBorder, Theme::dpi(1.0f));

        // Canvas border
        dl->AddRect(cMin, cMax, colCanvasBorder, 0, 0, Theme::dpi(1.5f));

        // Viewport border
        dl->AddRect(vpMin, vpMax, colVpBorder);

        dl->PopClipRect();

        curX += vw + spacing;
    }

    ImGui::Dummy(ImVec2(totalW, maxH));
}

// Same thumbnail flow as Properties::findThumbnail: load the ResourcesWindow-
// generated thumbnail from the pool, requesting async generation when missing.
Texture* ProjectSettingsWindow::findThumbnail(const std::string& path) {
    if (path.empty()) return nullptr;

    std::filesystem::path texPath = path;
    const std::filesystem::path projectPath = m_project->getProjectPath();

    if (texPath.is_relative() && !projectPath.empty()) {
        texPath = projectPath / texPath;
    }
    texPath = texPath.lexically_normal();

    if (!texPath.is_absolute()) return nullptr;

    std::error_code ec;
    if (!std::filesystem::exists(texPath, ec) || ec) {
        return nullptr;
    }

    const std::filesystem::path thumbnailPath = m_project->getThumbnailPath(texPath);
    const std::string thumbPathStr = thumbnailPath.string();

    // Fast path: return from cache if already loaded
    auto thumbIt = m_thumbnailTextures.find(thumbPathStr);
    if (thumbIt != m_thumbnailTextures.end() && !thumbIt->second.empty()) {
        return &thumbIt->second;
    }

    std::error_code thumbEc;
    const bool thumbnailExists = std::filesystem::exists(thumbnailPath, thumbEc) && !thumbEc;

    if (thumbnailExists) {
        TextureData thumbData(thumbnailPath.string().c_str());
        if (thumbData.getData() && thumbData.getSize() > 0) {
            Texture thumbTexture(thumbPathStr, thumbData);
            m_thumbnailTextures[thumbPathStr] = thumbTexture;
            return &m_thumbnailTextures[thumbPathStr];
        }
    }

    // Thumbnail missing or failed to load — request generation
    if (ResourcesWindow* resourcesWindow = Backend::getApp().getResourcesWindow()) {
        resourcesWindow->requestThumbnailGeneration(texPath, thumbnailExists);
    }
    return nullptr;
}

void ProjectSettingsWindow::open(Project* project) {
    m_isOpen = true;
    m_project = project;
    m_projectNameOriginal = project->getName();
    snprintf(m_projectNameBuffer, sizeof(m_projectNameBuffer), "%s", m_projectNameOriginal.c_str());
    m_canvasWidth = project->getCanvasWidth();
    m_canvasHeight = project->getCanvasHeight();
    m_scalingModeIndex = findScalingIndex(project->getScalingMode());
    m_textureStrategyIndex = findTextureStrategyIndex(project->getTextureStrategy());
    m_vsyncEnabled = project->isVSyncEnabled();
    m_windowModeIndex = findWindowModeIndex(project->getWindowMode());
    m_windowWidth = (int)project->getWindowWidth();
    m_windowHeight = (int)project->getWindowHeight();
    m_windowResizable = project->isWindowResizable();
    m_windowTitleOriginal = project->getWindowTitle();
    snprintf(m_windowTitleBuffer, sizeof(m_windowTitleBuffer), "%s", m_windowTitleOriginal.c_str());
    m_windowIcon = project->getWindowIcon();
    m_assetsDir = project->getAssetsDir();
    m_luaDir = project->getLuaDir();
    m_scriptDirs = project->getScriptDirs();
    m_cxxStandardIndex = findCxxStandardIndex(project->getCxxStandard());
    m_physics2DEnabled = project->isPhysics2DEnabled();
    m_physics3DEnabled = project->isPhysics3DEnabled();

    const ApplicationSettings& application = project->getApplicationSettings();
    snprintf(m_applicationNameBuffer, sizeof(m_applicationNameBuffer), "%s", application.name.c_str());
    snprintf(m_applicationIdentifierBuffer, sizeof(m_applicationIdentifierBuffer), "%s", application.identifier.c_str());
    snprintf(m_applicationVersionBuffer, sizeof(m_applicationVersionBuffer), "%s", application.version.c_str());
    m_applicationBuild = static_cast<int>(application.build);

    const WebProjectSettings& web = project->getWebProjectSettings();
    snprintf(m_webApplicationNameBuffer, sizeof(m_webApplicationNameBuffer), "%s", web.applicationName.c_str());
    m_webFavicon = web.favicon;
    m_webCustomHtmlShell = web.customHtmlShell;
    snprintf(m_webHeadIncludeBuffer, sizeof(m_webHeadIncludeBuffer), "%s", web.headInclude.c_str());
    m_webResizeCanvasToWindow = web.resizeCanvasToWindow;
    m_webHideEmscriptenUI = web.hideEmscriptenUI;

    const LinuxProjectSettings& linuxSettings = project->getLinuxProjectSettings();
    snprintf(m_linuxApplicationNameBuffer, sizeof(m_linuxApplicationNameBuffer), "%s", linuxSettings.applicationName.c_str());
    snprintf(m_linuxCommentBuffer, sizeof(m_linuxCommentBuffer), "%s", linuxSettings.comment.c_str());
    snprintf(m_linuxCategoriesBuffer, sizeof(m_linuxCategoriesBuffer), "%s", linuxSettings.categories.c_str());

    const WindowsProjectSettings& windows = project->getWindowsProjectSettings();
    snprintf(m_windowsProductNameBuffer, sizeof(m_windowsProductNameBuffer), "%s", windows.productName.c_str());
    snprintf(m_windowsCompanyNameBuffer, sizeof(m_windowsCompanyNameBuffer), "%s", windows.companyName.c_str());
    snprintf(m_windowsFileVersionBuffer, sizeof(m_windowsFileVersionBuffer), "%s", windows.fileVersion.c_str());
    snprintf(m_windowsProductVersionBuffer, sizeof(m_windowsProductVersionBuffer), "%s", windows.productVersion.c_str());

    const MacOSProjectSettings& macOS = project->getMacOSProjectSettings();
    snprintf(m_macOSApplicationNameBuffer, sizeof(m_macOSApplicationNameBuffer), "%s", macOS.applicationName.c_str());
    snprintf(m_macOSBundleIdentifierBuffer, sizeof(m_macOSBundleIdentifierBuffer), "%s", macOS.bundleIdentifier.c_str());
    snprintf(m_macOSVersionNameBuffer, sizeof(m_macOSVersionNameBuffer), "%s", macOS.versionName.c_str());
    snprintf(m_macOSBuildNumberBuffer, sizeof(m_macOSBuildNumberBuffer), "%s", macOS.buildNumber.c_str());
    m_macOSIcon = macOS.icon;
    m_macOSHighDpi = macOS.highDpi;

    const IOSProjectSettings& ios = project->getIOSProjectSettings();
    snprintf(m_iosApplicationNameBuffer, sizeof(m_iosApplicationNameBuffer), "%s", ios.applicationName.c_str());
    snprintf(m_iosBundleIdentifierBuffer, sizeof(m_iosBundleIdentifierBuffer), "%s", ios.bundleIdentifier.c_str());
    snprintf(m_iosVersionNameBuffer, sizeof(m_iosVersionNameBuffer), "%s", ios.versionName.c_str());
    snprintf(m_iosBuildNumberBuffer, sizeof(m_iosBuildNumberBuffer), "%s", ios.buildNumber.c_str());
    m_iosIcon = ios.icon;
    m_iosHideStatusBar = ios.hideStatusBar;
    m_iosHideHomeIndicator = ios.hideHomeIndicator;
    m_iosSupportsHighRefreshRate = ios.supportsHighRefreshRate;

    const AndroidProjectSettings& android = project->getAndroidProjectSettings();
    snprintf(m_androidApplicationNameBuffer, sizeof(m_androidApplicationNameBuffer), "%s", android.applicationName.c_str());
    snprintf(m_androidPackageNameBuffer, sizeof(m_androidPackageNameBuffer), "%s", android.packageName.c_str());
    snprintf(m_androidVersionNameBuffer, sizeof(m_androidVersionNameBuffer), "%s", android.versionName.c_str());
    m_androidLauncherIcon = android.launcherIcon;
    m_androidAdaptiveIconForeground = android.adaptiveIconForeground;
    m_androidAdaptiveIconBackground = android.adaptiveIconBackground;
    if (android.versionCode > 0) {
        snprintf(m_androidVersionCodeBuffer, sizeof(m_androidVersionCodeBuffer), "%u", android.versionCode);
    } else {
        m_androidVersionCodeBuffer[0] = '\0';
    }
    m_androidMinSdk = static_cast<int>(android.minSdk);
    m_androidTargetSdk = static_cast<int>(android.targetSdk);
    m_androidOrientationIndex = findAndroidOrientationIndex(android.orientation);
    m_androidAbiArmeabiV7a = android.abiArmeabiV7a;
    m_androidAbiArm64V8a = android.abiArm64V8a;
    m_androidAbiX86 = android.abiX86;
    m_androidAbiX86_64 = android.abiX86_64;
    m_androidPermissions = android.permissions;
    m_androidAllowBackup = android.allowBackup;
    m_androidFullscreen = android.fullscreen;
    m_androidKeepScreenOn = android.keepScreenOn;

    m_startSceneId = project->getStartSceneId();
    const SceneProject* startScene = project->getScene(m_startSceneId);
    if (!startScene || startScene->filepath.empty()) {
        m_startSceneId = NULL_PROJECT_SCENE;
        for (const auto& scene : project->getScenes()) {
            if (!scene.filepath.empty()) {
                m_startSceneId = scene.id;
                break;
            }
        }
    }

    m_packNativeResources = project->shouldPackNativeResources();
    m_versionControlMetadata = project->hasVersionControlMetadata();
}

void ProjectSettingsWindow::show() {
    if (!m_isOpen) return;

    ImGui::OpenPopup("Project Settings##ProjectSettingsModal");

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetWorkCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImVec2 size(
        std::min(Theme::dpi(dialogWidth), viewport->WorkSize.x * 0.9f),
        std::min(Theme::dpi(dialogHeight), viewport->WorkSize.y * 0.9f)
    );
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_Modal |
                             ImGuiWindowFlags_NoResize |
                             noScrollFlags;

    bool popupOpen = ImGui::BeginPopupModal("Project Settings##ProjectSettingsModal", &m_isOpen, flags);

    if (popupOpen) {
        if (!m_isOpen) {
            ImGui::CloseCurrentPopup();
        } else {
            drawSettings();
        }
        ImGui::EndPopup();
    }
}

void ProjectSettingsWindow::drawSettings() {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float footerY = ImGui::GetWindowHeight() - style.WindowPadding.y - ImGui::GetFrameHeight();
    float tabRegionHeight = std::max(1.0f, footerY - style.ItemSpacing.y - ImGui::GetCursorPosY());

    ImGui::BeginChild(
        "##ProjectSettingsTabRegion",
        ImVec2(0, tabRegionHeight),
        ImGuiChildFlags_None,
        noScrollFlags
    );

    if (ImGui::BeginTabBar("##ProjectSettingsTabs", ImGuiTabBarFlags_FittingPolicyShrink)) {
        if (ImGui::BeginTabItem("General")) {
            drawGeneralSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Canvas")) {
            drawCanvasSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Window")) {
            drawWindowSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Directories")) {
            drawDirectoriesSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Build")) {
            drawBuildSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Platforms")) {
            drawPlatformsSettings();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(style.WindowPadding.x, footerY - style.ItemSpacing.y));
    ImGui::Separator();

    float footerWidth = std::max(1.0f, ImGui::GetWindowWidth() - style.WindowPadding.x * 2.0f);
    float buttonWidth = std::clamp((footerWidth - style.ItemSpacing.x) * 0.5f, 1.0f, Theme::dpi(settingsButtonWidth));
    float buttonsWidth = buttonWidth * 2.0f + style.ItemSpacing.x;
    float buttonX = style.WindowPadding.x + std::max(0.0f, (footerWidth - buttonsWidth) * 0.5f);
    ImGui::SetCursorPos(ImVec2(buttonX, footerY));

    const bool canSave = hasAndroidAbiSelection(m_androidAbiArmeabiV7a, m_androidAbiArm64V8a, m_androidAbiX86, m_androidAbiX86_64);

    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
        if (applySettings()) {
            m_isOpen = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    if (!canSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Select at least one Android architecture.");
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
        m_isOpen = false;
        ImGui::CloseCurrentPopup();
    }

    // Per-frame lifecycle, matching Properties: the pool keeps the underlying
    // thumbnail textures cached, so re-creating the wrappers next frame is cheap.
    m_thumbnailTextures.clear();
}

std::string ProjectSettingsWindow::inheritedApplicationName() const {
    if (m_applicationNameBuffer[0]) return m_applicationNameBuffer;
    if (m_projectNameBuffer[0]) return m_projectNameBuffer;
    return "Doriax";
}

std::string ProjectSettingsWindow::inheritedApplicationIdentifier() const {
    return m_applicationIdentifierBuffer[0] ? m_applicationIdentifierBuffer : ApplicationSettings{}.identifier;
}

std::string ProjectSettingsWindow::inheritedApplicationVersion() const {
    return m_applicationVersionBuffer[0] ? m_applicationVersionBuffer : ApplicationSettings{}.version;
}

std::string ProjectSettingsWindow::inheritedApplicationBuild() const {
    return std::to_string(std::max(1, m_applicationBuild));
}

void ProjectSettingsWindow::drawGeneralSettings() {
    if (beginSettingsPanel("##GeneralSettingsPanel")) {
        if (ImGui::CollapsingHeader("Project", ImGuiTreeNodeFlags_DefaultOpen) && beginSettingsTable("##ProjectSettingsTable")) {
            beginSettingsRow("Project Name");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##ProjectName", m_projectNameBuffer, sizeof(m_projectNameBuffer));

            beginSettingsRow("Start Scene");

            const auto& scenes = m_project->getScenes();
            const SceneProject* selectedScene = m_project->getScene(m_startSceneId);
            if (!selectedScene || selectedScene->filepath.empty()) {
                selectedScene = nullptr;
                m_startSceneId = NULL_PROJECT_SCENE;
                for (const auto& scene : scenes) {
                    if (!scene.filepath.empty()) {
                        selectedScene = &scene;
                        m_startSceneId = scene.id;
                        break;
                    }
                }
            }

            if (!selectedScene) {
                ImGui::TextDisabled("No saved scenes");
            } else {
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo("##StartScene", selectedScene->name.c_str())) {
                    for (const auto& scene : scenes) {
                        if (scene.filepath.empty()) continue;

                        bool selected = m_startSceneId == scene.id;
                        if (ImGui::Selectable(scene.name.c_str(), selected)) m_startSceneId = scene.id;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }

            if (beginSettingsRow("Version Control Files", m_versionControlMetadata != Project::defaultVersionControlMetadata)) {
                m_versionControlMetadata = Project::defaultVersionControlMetadata;
            }
            ImGui::Checkbox("##VersionControlMetadata", &m_versionControlMetadata);
            endSettingsRow("Keep .gitignore and .gitattributes in the project up to date, so the editor's "
                "working directory, this machine's build settings and each developer's own layout stay out "
                "of the repository. A file the editor did not generate is never replaced.");

            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Application", ImGuiTreeNodeFlags_DefaultOpen) && beginSettingsTable("##ApplicationSettingsTable")) {
            const ApplicationSettings defaults;

            beginSettingsRow("Name");
            ImGui::SetNextItemWidth(-helpMarkerWidth());
            std::string nameHint = m_projectNameBuffer[0] ? m_projectNameBuffer : "Doriax";
            ImGui::InputTextWithHint("##ApplicationName", nameHint.c_str(), m_applicationNameBuffer, sizeof(m_applicationNameBuffer));
            endSettingsRow("Name every platform shows to the player. Empty means project name.");

            if (beginSettingsRow("Identifier", strcmp(m_applicationIdentifierBuffer, defaults.identifier.c_str()) != 0)) {
                snprintf(m_applicationIdentifierBuffer, sizeof(m_applicationIdentifierBuffer), "%s", defaults.identifier.c_str());
            }
            ImGui::SetNextItemWidth(-helpMarkerWidth());
            ImGui::InputText("##ApplicationIdentifier", m_applicationIdentifierBuffer, sizeof(m_applicationIdentifierBuffer));
            endSettingsRow("Reverse-DNS id behind the Apple bundle identifiers and the Android package name.");
            if (!isSharedIdentifierValid(m_applicationIdentifierBuffer)) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Use letters and digits only, for example com.company.game.");
            }

            if (beginSettingsRow("Version", strcmp(m_applicationVersionBuffer, defaults.version.c_str()) != 0)) {
                snprintf(m_applicationVersionBuffer, sizeof(m_applicationVersionBuffer), "%s", defaults.version.c_str());
            }
            ImGui::SetNextItemWidth(-helpMarkerWidth());
            ImGui::InputText("##ApplicationVersion", m_applicationVersionBuffer, sizeof(m_applicationVersionBuffer));
            endSettingsRow("Version shown to the player. Apple takes at most three parts, Windows pads to four.");
            if (!isAppleVersionValid(m_applicationVersionBuffer)) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Use up to three numbers from 0 to 65535, for example 1.0.");
            }

            drawIntSetting("Build", "##ApplicationBuild", m_applicationBuild, static_cast<int>(defaults.build), 1,
                "Apple build number and Android version code. Increase it for every release.");

            ImGui::EndTable();
        }
    }

    endSettingsPanel();
}

void ProjectSettingsWindow::drawCanvasSettings() {
    drawSettingsPanel("##CanvasSettingsPanel", [this]() {
        drawIntSetting("Canvas Width", "##CanvasWidth", m_canvasWidth, (int)Project::defaultCanvasWidth);
        drawIntSetting("Canvas Height", "##CanvasHeight", m_canvasHeight, (int)Project::defaultCanvasHeight);

        drawComboSetting("Scaling Mode", "##ScalingMode", scalingModeNames, scalingModeCount, m_scalingModeIndex, findScalingIndex(Project::defaultScalingMode));
        ImGui::Spacing();
        drawScalingPreview(scalingModeValues[m_scalingModeIndex], m_canvasWidth, m_canvasHeight);

        drawComboSetting("Texture Strategy", "##TextureStrategy", textureStrategyNames, textureStrategyCount, m_textureStrategyIndex, findTextureStrategyIndex(Project::defaultTextureStrategy));
    });
}

void ProjectSettingsWindow::drawWindowSettings() {
    drawSettingsPanel("##WindowSettingsPanel", [this]() {
        drawComboSetting("Window Mode", "##WindowMode", windowModeNames, windowModeCount, m_windowModeIndex,
            findWindowModeIndex(Project::defaultWindowMode),
            "Initial window state of desktop builds. Web and mobile ignore window settings.");

        drawIntSetting("Window Width", "##WindowWidth", m_windowWidth, (int)Project::defaultWindowWidth);
        drawIntSetting("Window Height", "##WindowHeight", m_windowHeight, (int)Project::defaultWindowHeight);

        if (beginSettingsRow("Window Resizable", m_windowResizable != Project::defaultWindowResizable)) {
            m_windowResizable = Project::defaultWindowResizable;
        }
        ImGui::Checkbox("##WindowResizable", &m_windowResizable);
        endSettingsRow("Applies to desktop builds. Exported Windows and macOS builds are always resizable.");

        if (beginSettingsRow("Window Title", strcmp(m_windowTitleBuffer, Project::defaultWindowTitle) != 0)) {
            snprintf(m_windowTitleBuffer, sizeof(m_windowTitleBuffer), "%s", Project::defaultWindowTitle);
        }
        std::string titleHint = m_project->getName().empty() ? "Doriax" : m_project->getName();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##WindowTitle", titleHint.c_str(), m_windowTitleBuffer, sizeof(m_windowTitleBuffer));

        beginSettingsRow("Icon");
        {
            const ImGuiStyle& style = ImGui::GetStyle();
            float browseWidth = ImGui::CalcTextSize("Browse").x + style.FramePadding.x * 2.0f;
            float clearWidth = ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f;
            float pathWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x - browseWidth - clearWidth - style.ItemSpacing.x * 2.0f - helpMarkerWidth());

            fs::path iconDisplay = m_windowIcon.empty() ? fs::path("<None>") : m_windowIcon;
            Widgets::pathDisplay("##WindowIconPath", iconDisplay, Vector2(pathWidth, ImGui::GetFrameHeight()));

            ImGui::SameLine();
            if (ImGui::Button("Browse##windowicon")) {
                std::string defaultPath = m_project->getProjectPath().string();
                std::string selectedPath = FileDialogs::openFileDialog(defaultPath, FILE_DIALOG_IMAGE, false);
                if (!selectedPath.empty()) {
                    // Store project-relative when the file is inside the project
                    // so the setting survives moving the project folder.
                    std::error_code ec;
                    fs::path relPath = fs::relative(fs::path(selectedPath), m_project->getProjectPath(), ec);
                    if (ec || relPath.empty() || relPath.string().rfind("..", 0) == 0) {
                        m_windowIcon = fs::path(selectedPath);
                    } else {
                        m_windowIcon = relPath;
                    }
                }
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(m_windowIcon.empty());
            if (ImGui::Button("Clear##windowicon")) {
                m_windowIcon.clear();
            }
            ImGui::EndDisabled();
            endSettingsRow("Application icon for desktop builds: embedded into the Windows executable and used as the window/taskbar icon. Square PNG recommended (256x256 or larger).");

            if (!m_windowIcon.empty()) {
                if (Texture* thumb = findThumbnail(m_windowIcon.string())) {
                    ImGui::Spacing();

                    const float previewSize = Theme::dpi(64.0f);
                    float thumbWidth = (float)thumb->getWidth();
                    float thumbHeight = (float)thumb->getHeight();
                    float scale = previewSize / std::max(1.0f, std::max(thumbWidth, thumbHeight));
                    ImVec2 imageSize(std::max(1.0f, thumbWidth * scale), std::max(1.0f, thumbHeight * scale));

                    ImVec2 imagePos = ImGui::GetCursorScreenPos();
                    Widgets::image(Backend::getImGuiTexture(thumb->getRender()), imageSize);
                    ImGui::GetWindowDrawList()->AddRect(
                        imagePos, ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
                        ImGui::GetColorU32(ImGuiCol_Border));
                }
            }
        }

        if (beginSettingsRow("VSync", m_vsyncEnabled != Project::defaultVSyncEnabled)) {
            m_vsyncEnabled = Project::defaultVSyncEnabled;
        }
        ImGui::Checkbox("##VSync", &m_vsyncEnabled);
        endSettingsRow("Synchronize Play mode and supported desktop builds to the display refresh rate. "
            "The editor window follows View > Editor VSync. "
            "macOS Metal exports remain synchronized.");
    });
}

void ProjectSettingsWindow::drawDirectoriesSettings() {
    drawSettingsPanel("##DirectoriesSettingsPanel", [this]() {
        if (beginSettingsRow("Native Resource Pack", m_packNativeResources != Project::defaultPackNativeResources)) {
            m_packNativeResources = Project::defaultPackNativeResources;
        }
        ImGui::Checkbox("##PackNativeResources", &m_packNativeResources);
        endSettingsRow("Experimental. Packs exported assets and Lua files into resources.pak for Desktop and Android source exports. "
            "Packed resources are read through Data; direct File handles cannot open them.");

        drawDirectorySetting(
            m_project, "Assets Directory", nullptr, "##AssetsPath", "Browse##assets",
            m_assetsDir, fs::path(Project::defaultAssetsDir)
        );
        drawDirectorySetting(
            m_project, "Lua Directory", nullptr, "##LuaPath", "Browse##lua",
            m_luaDir, fs::path(Project::defaultLuaDir), true
        );
        drawScriptDirsSetting(m_project, m_scriptDirs);
    });
}

void ProjectSettingsWindow::drawBuildSettings() {
    drawSettingsPanel("##BuildSettingsPanel", [this]() {
        if (beginSettingsRow("2D Physics", m_physics2DEnabled != Project::defaultPhysics2DEnabled)) {
            m_physics2DEnabled = Project::defaultPhysics2DEnabled;
        }
        ImGui::Checkbox("##Physics2D", &m_physics2DEnabled);
        endSettingsRow("Box2D-based 2D physics. When off, Box2D is not compiled or linked into exported builds.");

        if (beginSettingsRow("3D Physics", m_physics3DEnabled != Project::defaultPhysics3DEnabled)) {
            m_physics3DEnabled = Project::defaultPhysics3DEnabled;
        }
        ImGui::Checkbox("##Physics3D", &m_physics3DEnabled);
        endSettingsRow("Jolt-based 3D physics. When off, Jolt is not compiled or linked into exported builds.");

        drawComboSetting("C++ Standard", "##CxxStandard", cxxStandardNames, cxxStandardCount, m_cxxStandardIndex,
            findCxxStandardIndex(Project::defaultCxxStandard),
            "Language standard for C++ scripts in Play and exported games. Saved with the project. "
            "Exports also compile the engine with this standard. "
            "Choose a compatible compiler in Editor Settings > Desktop.");
    });
}

void ProjectSettingsWindow::drawPlatformsSettings() {
    // Collapsed by default: identity lives on General, so a section is only
    // what its platform adds on top of it.
    if (beginSettingsPanel("##PlatformsSettingsPanel")) {
        if (ImGui::CollapsingHeader("Web") && beginSettingsTable("##WebSettingsTable")) {
            drawWebSettings();
            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Linux") && beginSettingsTable("##LinuxSettingsTable")) {
            drawLinuxSettings();
            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Windows")) {
            drawSectionNote("The desktop icon is configured on the Window tab and reused for Windows exports.");
            if (beginSettingsTable("##WindowsSettingsTable")) {
                drawWindowsSettings();
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("macOS")) {
            drawSectionNote("Bundle settings apply to Apple projects exported as source code. "
                "Desktop export produces a standalone executable, not an .app bundle.");
            if (beginSettingsTable("##MacOSSettingsTable")) {
                drawMacOSSettings();
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("iOS") && beginSettingsTable("##IOSSettingsTable")) {
            drawIOSSettings();
            ImGui::EndTable();
        }

        if (ImGui::CollapsingHeader("Android") && beginSettingsTable("##AndroidSettingsTable")) {
            drawAndroidSettings();
            ImGui::EndTable();
        }
    }
    endSettingsPanel();
}

void ProjectSettingsWindow::drawWebSettings() {
    drawOverrideSetting("App Name", "##WebAppName", m_webApplicationNameBuffer, inheritedApplicationName(),
        "Title used by the exported web page. Empty means the shared application name.");

    drawProjectFilePathSetting(m_project, "Favicon", "Browser tab icon for web export.", "##WebFaviconPath",
        "Browse##webfavicon", "Clear##webfavicon", m_webFavicon);
    drawProjectFilePathSetting(m_project, "Custom HTML Shell", "Optional web HTML wrapper. The file must contain {{DORIAX_DEFAULT_HTML}} marker.", "##WebHtmlShellPath",
        "Browse##webhtmlshell", "Clear##webhtmlshell", m_webCustomHtmlShell, FILE_DIALOG_ALL);

    beginSettingsRow("Head Include");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextMultiline("##WebHeadInclude", m_webHeadIncludeBuffer, sizeof(m_webHeadIncludeBuffer), ImVec2(-1, Theme::dpi(80.0f)));
    endSettingsRow("HTML inserted into the <head> section by web export when supported.");

    if (beginSettingsRow("Resize Canvas To Window", !m_webResizeCanvasToWindow)) {
        m_webResizeCanvasToWindow = true;
    }
    ImGui::Checkbox("##WebResizeCanvasToWindow", &m_webResizeCanvasToWindow);
    endSettingsRow("Resize the rendering surface to the browser window. Canvas Scaling Mode controls aspect ratio and stretching.");

    if (beginSettingsRow("Hide Emscripten UI", m_webHideEmscriptenUI)) {
        m_webHideEmscriptenUI = false;
    }
    ImGui::Checkbox("##WebHideEmscriptenUI", &m_webHideEmscriptenUI);
    endSettingsRow("Hide the standard logo, status, controls and output console. Runtime scripts remain active. Custom HTML elements are not removed.");
}

void ProjectSettingsWindow::drawLinuxSettings() {
    drawOverrideSetting("App Name", "##LinuxAppName", m_linuxApplicationNameBuffer, inheritedApplicationName(),
        "Linux desktop launcher name. Empty means the shared application name.");

    beginSettingsRow("Comment");
    ImGui::SetNextItemWidth(-helpMarkerWidth());
    ImGui::InputText("##LinuxComment", m_linuxCommentBuffer, sizeof(m_linuxCommentBuffer));
    endSettingsRow("Short description written to the .desktop launcher file.");

    if (beginSettingsRow("Categories", strcmp(m_linuxCategoriesBuffer, LinuxProjectSettings{}.categories.c_str()) != 0)) {
        snprintf(m_linuxCategoriesBuffer, sizeof(m_linuxCategoriesBuffer), "%s", LinuxProjectSettings{}.categories.c_str());
    }
    ImGui::SetNextItemWidth(-helpMarkerWidth());
    ImGui::InputText("##LinuxCategories", m_linuxCategoriesBuffer, sizeof(m_linuxCategoriesBuffer));
    endSettingsRow("Desktop Entry categories. Example: Game;ArcadeGame;");
}

void ProjectSettingsWindow::drawWindowsSettings() {
    drawOverrideSetting("Product Name", "##WindowsProductName", m_windowsProductNameBuffer, inheritedApplicationName(),
        "Windows executable metadata. Empty means the shared application name.");

    beginSettingsRow("Company Name");
    ImGui::SetNextItemWidth(-helpMarkerWidth());
    ImGui::InputText("##WindowsCompanyName", m_windowsCompanyNameBuffer, sizeof(m_windowsCompanyNameBuffer));
    endSettingsRow("Windows executable metadata company name.");

    const std::string inheritedWindowsVersion = Project::toFourPartVersion(inheritedApplicationVersion());

    drawOverrideSetting("File Version", "##WindowsFileVersion", m_windowsFileVersionBuffer, inheritedWindowsVersion,
        "Windows numeric file version. Empty means the shared version, padded to four parts.");
    drawOverrideWarning(m_windowsFileVersionBuffer, isWindowsVersionValid,
        "Use up to four numbers from 0 to 65535, for example 1.0.0.0.");
    drawWindowsVersionResult(m_windowsFileVersionBuffer);

    drawOverrideSetting("Product Version", "##WindowsProductVersion", m_windowsProductVersionBuffer, inheritedWindowsVersion,
        "Windows product version. Empty means the shared version, padded to four parts.");
    drawOverrideWarning(m_windowsProductVersionBuffer, isWindowsVersionValid,
        "Use up to four numbers from 0 to 65535, for example 1.0.0.0.");
    drawWindowsVersionResult(m_windowsProductVersionBuffer);

}

void ProjectSettingsWindow::drawMacOSSettings() {
    drawOverrideSetting("App Name", "##MacOSAppName", m_macOSApplicationNameBuffer, inheritedApplicationName(),
        "macOS bundle display name. Empty means the shared application name.");

    drawOverrideSetting("Bundle Identifier", "##MacOSBundleIdentifier", m_macOSBundleIdentifierBuffer, inheritedApplicationIdentifier(),
        "CFBundleIdentifier. Empty means the shared identifier.");
    drawOverrideWarning(m_macOSBundleIdentifierBuffer, isAppleBundleIdentifierValid,
        "Use letters, digits and hyphens, for example com.company.my-game.");

    drawOverrideSetting("Version Name", "##MacOSVersionName", m_macOSVersionNameBuffer, inheritedApplicationVersion(),
        "CFBundleShortVersionString. Empty means the shared version.");
    drawOverrideWarning(m_macOSVersionNameBuffer, isAppleVersionValid,
        "Apple accepts at most three numbers, for example 1.0.2.");
    drawOverrideSetting("Build Number", "##MacOSBuildNumber", m_macOSBuildNumberBuffer, inheritedApplicationBuild(),
        "CFBundleVersion. Empty means the shared build.");
    drawOverrideWarning(m_macOSBuildNumberBuffer, isAppleBuildValid,
        "Apple accepts at most three numbers, for example 42.");

    drawProjectFilePathSetting(m_project, "Icon", "macOS application icon source image. Export support depends on the generated Xcode project.", "##MacOSIconPath",
        "Browse##macosicon", "Clear##macosicon", m_macOSIcon);

    if (beginSettingsRow("High DPI", !m_macOSHighDpi)) {
        m_macOSHighDpi = true;
    }
    ImGui::Checkbox("##MacOSHighDpi", &m_macOSHighDpi);
    endSettingsRow("Allow high-DPI rendering on macOS.");
}

void ProjectSettingsWindow::drawIOSSettings() {
    drawOverrideSetting("App Name", "##IOSAppName", m_iosApplicationNameBuffer, inheritedApplicationName(),
        "iOS bundle display name. Empty means the shared application name.");

    drawOverrideSetting("Bundle Identifier", "##IOSBundleIdentifier", m_iosBundleIdentifierBuffer, inheritedApplicationIdentifier(),
        "CFBundleIdentifier. Empty means the shared identifier.");
    drawOverrideWarning(m_iosBundleIdentifierBuffer, isAppleBundleIdentifierValid,
        "Use letters, digits and hyphens, for example com.company.my-game.");

    drawOverrideSetting("Version Name", "##IOSVersionName", m_iosVersionNameBuffer, inheritedApplicationVersion(),
        "CFBundleShortVersionString. Empty means the shared version.");
    drawOverrideWarning(m_iosVersionNameBuffer, isAppleVersionValid,
        "Apple accepts at most three numbers, for example 1.0.2.");
    drawOverrideSetting("Build Number", "##IOSBuildNumber", m_iosBuildNumberBuffer, inheritedApplicationBuild(),
        "CFBundleVersion. Empty means the shared build.");
    drawOverrideWarning(m_iosBuildNumberBuffer, isAppleBuildValid,
        "Apple accepts at most three numbers, for example 42.");

    drawProjectFilePathSetting(m_project, "Icon", "iOS application icon source image. Export support depends on the generated Xcode project.", "##IOSIconPath",
        "Browse##iosicon", "Clear##iosicon", m_iosIcon);

    if (beginSettingsRow("Hide Status Bar", !m_iosHideStatusBar)) {
        m_iosHideStatusBar = true;
    }
    ImGui::Checkbox("##IOSHideStatusBar", &m_iosHideStatusBar);
    endSettingsRow("Hide the iOS status bar while the app runs.");

    if (beginSettingsRow("Hide Home Indicator", !m_iosHideHomeIndicator)) {
        m_iosHideHomeIndicator = true;
    }
    ImGui::Checkbox("##IOSHideHomeIndicator", &m_iosHideHomeIndicator);
    endSettingsRow("Request hiding the iOS home indicator.");

    if (beginSettingsRow("High Refresh Rate", !m_iosSupportsHighRefreshRate)) {
        m_iosSupportsHighRefreshRate = true;
    }
    ImGui::Checkbox("##IOSHighRefreshRate", &m_iosSupportsHighRefreshRate);
    endSettingsRow("Allow 120 Hz displays when supported.");
}

void ProjectSettingsWindow::drawAndroidSettings() {
    AndroidProjectSettings defaults;

    drawOverrideSetting("App Name", "##AndroidAppName", m_androidApplicationNameBuffer, inheritedApplicationName(),
        "Android launcher label. Empty means the shared application name.");

    drawOverrideSetting("Package Name", "##AndroidPackageName", m_androidPackageNameBuffer, inheritedApplicationIdentifier(),
        "Android applicationId. Empty means the shared identifier.");
    drawOverrideWarning(m_androidPackageNameBuffer, isJavaPackageNameValid,
        "Use a Java package name, for example com.company.game.");

    drawOverrideSetting("Version Code", "##AndroidVersionCode", m_androidVersionCodeBuffer, inheritedApplicationBuild(),
        "Integer version used by Android and stores. Empty means the shared build.");
    drawOverrideWarning(m_androidVersionCodeBuffer, isAndroidVersionCodeValid,
        "Use a whole number from 1 to 2100000000.");

    drawOverrideSetting("Version Name", "##AndroidVersionName", m_androidVersionNameBuffer, inheritedApplicationVersion(),
        "Visible version string. Empty means the shared version.");

    beginSettingsRow("Launcher Icons");
    ImGui::TextWrapped("PNG recommended. Adaptive icon requires foreground and background images.");
    endSettingsRow("Android launcher icon resources. If adaptive foreground and background are set, the export creates an adaptive icon. Otherwise Launcher Icon is used.");

    drawProjectFilePathSetting(m_project, "Launcher Icon", "Fallback launcher icon. Square PNG recommended.", "##AndroidLauncherIconPath",
        "Browse##androidlaunchericon", "Clear##androidlaunchericon", m_androidLauncherIcon);
    drawProjectFilePathSetting(m_project, "Adaptive Foreground", "Adaptive icon foreground image.", "##AndroidAdaptiveIconForegroundPath",
        "Browse##androidadaptiveforeground", "Clear##androidadaptiveforeground", m_androidAdaptiveIconForeground);
    drawProjectFilePathSetting(m_project, "Adaptive Background", "Adaptive icon background image.", "##AndroidAdaptiveIconBackgroundPath",
        "Browse##androidadaptivebackground", "Clear##androidadaptivebackground", m_androidAdaptiveIconBackground);

    drawIntSetting("Min SDK", "##AndroidMinSdk", m_androidMinSdk, static_cast<int>(defaults.minSdk), 1,
        "Lowest Android API level the exported project supports.");
    drawIntSetting("Target SDK", "##AndroidTargetSdk", m_androidTargetSdk, static_cast<int>(defaults.targetSdk), 1,
        "Android API level the app declares as its target.");
    m_androidTargetSdk = std::max(m_androidTargetSdk, m_androidMinSdk);

    drawComboSetting("Orientation", "##AndroidOrientation", androidOrientationNames, androidOrientationCount,
        m_androidOrientationIndex, findAndroidOrientationIndex(defaults.orientation),
        "Screen orientation requested by the Android activity.");

    beginSettingsRow("Architectures");
    ImGui::Checkbox("armeabi-v7a", &m_androidAbiArmeabiV7a);
    ImGui::Checkbox("arm64-v8a", &m_androidAbiArm64V8a);
    ImGui::Checkbox("x86", &m_androidAbiX86);
    ImGui::Checkbox("x86_64", &m_androidAbiX86_64);
    endSettingsRow("Native CPU architectures included in the APK.");
    if (!hasAndroidAbiSelection(m_androidAbiArmeabiV7a, m_androidAbiArm64V8a, m_androidAbiX86, m_androidAbiX86_64)) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Select at least one architecture.");
    }

    beginSettingsRow("Permissions");
    if (ImGui::SmallButton("None##AndroidPermissionsNone")) {
        m_androidPermissions.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Game Defaults##AndroidPermissionsGameDefaults")) {
        m_androidPermissions.clear();
        m_androidPermissions.insert("internet");
        m_androidPermissions.insert("access_network_state");
        m_androidPermissions.insert("vibrate");
    }

    ImGui::BeginChild("##AndroidPermissionsList", ImVec2(0, Theme::dpi(170.0f)), ImGuiChildFlags_Borders);
    for (int i = 0; i < androidPermissionCount; i++) {
        const AndroidPermissionInfo& permission = androidPermissionInfos[i];
        bool enabled = m_androidPermissions.count(permission.key) > 0;
        std::string label = std::string(permission.manifestName) + "##AndroidPermission_" + permission.key;
        if (ImGui::Checkbox(label.c_str(), &enabled)) {
            if (enabled) {
                m_androidPermissions.insert(permission.key);
            } else {
                m_androidPermissions.erase(permission.key);
            }
        }
    }
    ImGui::EndChild();
    endSettingsRow("Android permissions written to AndroidManifest.xml. Some permissions still require runtime approval in your Android code.");

    if (beginSettingsRow("Application Options", m_androidAllowBackup != defaults.allowBackup || m_androidKeepScreenOn != defaults.keepScreenOn)) {
        m_androidAllowBackup = defaults.allowBackup;
        m_androidKeepScreenOn = defaults.keepScreenOn;
    }

    ImGui::Checkbox("Allow Backup", &m_androidAllowBackup);
    ImGui::SetItemTooltip("Maps to android:allowBackup.");

    ImGui::Checkbox("Keep Screen On", &m_androidKeepScreenOn);
    ImGui::SetItemTooltip("Adds FLAG_KEEP_SCREEN_ON to the Android activity.");
    endSettingsRow("Android application/activity behavior.");

    if (beginSettingsRow("Screen Options", m_androidFullscreen != defaults.fullscreen)) {
        m_androidFullscreen = defaults.fullscreen;
    }

    ImGui::Checkbox("Fullscreen", &m_androidFullscreen);
    ImGui::SetItemTooltip("Uses the fullscreen Android theme and hides system bars in MainActivity.");
    endSettingsRow("Android screen/window behavior.");
}

bool ProjectSettingsWindow::applySettings() {
    if (!hasAndroidAbiSelection(m_androidAbiArmeabiV7a, m_androidAbiArm64V8a, m_androidAbiX86, m_androidAbiX86_64)) {
        return false;
    }
    // The edit buffer truncates long names. Only write back when the value
    // changed, since setName also rebuilds libName and the window title.
    std::string projectName = m_projectNameBuffer;
    if (!projectName.empty() && projectName != m_projectNameOriginal.substr(0, sizeof(m_projectNameBuffer) - 1)) {
        m_project->setName(projectName);
    }

    m_project->setCanvasSize(m_canvasWidth, m_canvasHeight);
    m_project->setScalingMode(scalingModeValues[m_scalingModeIndex]);
    m_project->setTextureStrategy(textureStrategyValues[m_textureStrategyIndex]);
    m_project->setVSyncEnabled(m_vsyncEnabled);
    m_project->setWindowMode(windowModeValues[m_windowModeIndex]);
    m_project->setWindowSize(m_windowWidth, m_windowHeight);
    m_project->setWindowResizable(m_windowResizable);

    // The edit buffer truncates long titles. Preserve an untouched title that
    // is longer than the buffer instead of writing back the truncated value.
    if (m_windowTitleBuffer != m_windowTitleOriginal.substr(0, sizeof(m_windowTitleBuffer) - 1)) {
        m_project->setWindowTitle(m_windowTitleBuffer);
    }
    m_project->setWindowIcon(m_windowIcon);

    ApplicationSettings& application = m_project->getApplicationSettings();
    applyTextBuffer(application.name, m_applicationNameBuffer);
    applyTextBuffer(application.identifier, m_applicationIdentifierBuffer);
    if (!isSharedIdentifierValid(application.identifier)) {
        application.identifier = ApplicationSettings{}.identifier;
    }
    applyTextBuffer(application.version, m_applicationVersionBuffer);
    if (!isAppleVersionValid(application.version)) {
        application.version = Project::toAppleVersion(application.version);  // trim, do not discard
    }
    application.build = static_cast<unsigned int>(std::max(1, m_applicationBuild));

    // Moves the referenced files in and rewrites every reference to the new roots
    m_project->changeAssetRoots(m_assetsDir, m_luaDir);
    m_project->setScriptDirs(m_scriptDirs);
    if (m_cxxStandardIndex < 0 || m_cxxStandardIndex >= cxxStandardCount) {
        m_cxxStandardIndex = 0;
    }
    m_project->setCxxStandard(cxxStandards[m_cxxStandardIndex]);
    m_project->setPhysics2DEnabled(m_physics2DEnabled);
    m_project->setPhysics3DEnabled(m_physics3DEnabled);

    // The project already carries the values applied above
    const std::string inheritedName = m_project->getApplicationName();
    const std::string inheritedIdentifier = m_project->getApplicationIdentifier();
    const std::string inheritedVersion = m_project->getApplicationVersion();
    const std::string inheritedBuild = m_project->getApplicationBuild();

    WebProjectSettings& web = m_project->getWebProjectSettings();
    applyOverride(web.applicationName, m_webApplicationNameBuffer, inheritedName);
    web.favicon = m_webFavicon;
    web.customHtmlShell = m_webCustomHtmlShell;
    applyTextBuffer(web.headInclude, m_webHeadIncludeBuffer);
    web.resizeCanvasToWindow = m_webResizeCanvasToWindow;
    web.hideEmscriptenUI = m_webHideEmscriptenUI;

    LinuxProjectSettings& linuxSettings = m_project->getLinuxProjectSettings();
    applyOverride(linuxSettings.applicationName, m_linuxApplicationNameBuffer, inheritedName);
    applyTextBuffer(linuxSettings.comment, m_linuxCommentBuffer);
    applyTextBuffer(linuxSettings.categories, m_linuxCategoriesBuffer);
    if (linuxSettings.categories.empty()) {
        linuxSettings.categories = LinuxProjectSettings{}.categories;
    }

    WindowsProjectSettings& windows = m_project->getWindowsProjectSettings();
    applyOverride(windows.productName, m_windowsProductNameBuffer, inheritedName);
    applyTextBuffer(windows.companyName, m_windowsCompanyNameBuffer);
    // Compared through the padding export applies, so "2.5" and "2.5.0.0" both read as shared
    const std::string inheritedWindowsVersion = m_project->getApplicationFileVersion();
    auto applyWindowsVersion = [&inheritedWindowsVersion](std::string& value, const auto& buffer) {
        const bool wasInheriting = value.empty();
        applyTextBuffer(value, buffer);
        if (value.empty()) return;
        if (!isWindowsVersionValid(value)
                || (wasInheriting && Project::toFourPartVersion(value) == inheritedWindowsVersion)) {
            value.clear();
        }
    };

    applyWindowsVersion(windows.fileVersion, m_windowsFileVersionBuffer);
    applyWindowsVersion(windows.productVersion, m_windowsProductVersionBuffer);

    MacOSProjectSettings& macOS = m_project->getMacOSProjectSettings();
    applyOverride(macOS.applicationName, m_macOSApplicationNameBuffer, inheritedName);
    applyOverride(macOS.bundleIdentifier, m_macOSBundleIdentifierBuffer, inheritedIdentifier, isAppleBundleIdentifierValid);
    applyAppleVersion(macOS.versionName, m_macOSVersionNameBuffer, inheritedVersion, isAppleVersionValid);
    applyAppleVersion(macOS.buildNumber, m_macOSBuildNumberBuffer, inheritedBuild, isAppleBuildValid);
    macOS.icon = m_macOSIcon;
    macOS.highDpi = m_macOSHighDpi;

    IOSProjectSettings& ios = m_project->getIOSProjectSettings();
    applyOverride(ios.applicationName, m_iosApplicationNameBuffer, inheritedName);
    applyOverride(ios.bundleIdentifier, m_iosBundleIdentifierBuffer, inheritedIdentifier, isAppleBundleIdentifierValid);
    applyAppleVersion(ios.versionName, m_iosVersionNameBuffer, inheritedVersion, isAppleVersionValid);
    applyAppleVersion(ios.buildNumber, m_iosBuildNumberBuffer, inheritedBuild, isAppleBuildValid);
    ios.icon = m_iosIcon;
    ios.hideStatusBar = m_iosHideStatusBar;
    ios.hideHomeIndicator = m_iosHideHomeIndicator;
    ios.supportsHighRefreshRate = m_iosSupportsHighRefreshRate;

    AndroidProjectSettings& android = m_project->getAndroidProjectSettings();
    applyOverride(android.applicationName, m_androidApplicationNameBuffer, inheritedName);
    applyOverride(android.packageName, m_androidPackageNameBuffer, inheritedIdentifier, isJavaPackageNameValid);
    // Anything but a positive integer goes back to inheriting the shared build.
    const bool versionCodeWasInheriting = android.versionCode == 0;
    android.versionCode = parseAndroidVersionCode(m_androidVersionCodeBuffer);
    if (versionCodeWasInheriting && android.versionCode == m_project->getApplicationVersionCode()) {
        android.versionCode = 0;
    }
    applyOverride(android.versionName, m_androidVersionNameBuffer, inheritedVersion);
    android.launcherIcon = m_androidLauncherIcon;
    android.adaptiveIconForeground = m_androidAdaptiveIconForeground;
    android.adaptiveIconBackground = m_androidAdaptiveIconBackground;
    android.minSdk = static_cast<unsigned int>(std::max(1, m_androidMinSdk));
    android.targetSdk = static_cast<unsigned int>(std::max(m_androidMinSdk, m_androidTargetSdk));
    android.orientation = androidOrientationValues[std::clamp(m_androidOrientationIndex, 0, androidOrientationCount - 1)];
    android.abiArmeabiV7a = m_androidAbiArmeabiV7a;
    android.abiArm64V8a = m_androidAbiArm64V8a;
    android.abiX86 = m_androidAbiX86;
    android.abiX86_64 = m_androidAbiX86_64;
    android.permissions = m_androidPermissions;
    android.allowBackup = m_androidAllowBackup;
    android.fullscreen = m_androidFullscreen;
    android.keepScreenOn = m_androidKeepScreenOn;

    const SceneProject* startScene = m_project->getScene(m_startSceneId);
    if (startScene && !startScene->filepath.empty()) {
        m_project->setStartSceneId(startScene->id);
    } else {
        m_project->setStartSceneId(NULL_PROJECT_SCENE);
    }

    m_project->setPackNativeResources(m_packNativeResources);
    m_project->setVersionControlMetadata(m_versionControlMetadata);

    return m_project->saveProjectFile();
}

} // namespace doriax::editor
