// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Exporter.h"
#include "EditorHost.h"
#include "Factory.h"
#include "Generator.h"
#include "Out.h"
#include "Stream.h"
#include "util/Base64.h"
#include "util/FileUtils.h"
#include "util/MsBuildProgress.h"
#include "util/ShaderHeaderBuilder.h"
#include "pool/ShaderPool.h"

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <future>
#include <map>
#include <vector>

using namespace doriax;

namespace {
    constexpr float BUILD_PROGRESS_START = 0.6f;
    constexpr float BUILD_PROGRESS_END = 0.95f;
    constexpr float MSBUILD_COMPILE_END = 0.9f;
    constexpr char RESOURCE_PACK_MAGIC[] = {'D', 'X', 'P', 'K', '1'};
    constexpr const char* RESOURCE_PACK_FILENAME = "resources.pak";
    constexpr const char* RESOURCE_PACK_TEMP_FILENAME = "resources.pak.tmp";
    // The reader seeks with off_t, still 32-bit on the armeabi-v7a and x86 ABIs
    constexpr uint64_t MAX_RESOURCE_PACK_SIZE = INT32_MAX;

    struct ResourcePackBuildEntry {
        std::string path;      // key inside the pack, relative to the export root
        fs::path source;
        uint64_t size = 0;
        uint64_t offset = 0;
        uint8_t key = 0;
        uint32_t shift = 0;
    };

    // Parses make-style "[ 47%]" and ninja-style "[123/456]" build-line prefixes
    // into a 0..1 fraction so the compile step can drive the progress bar.
    bool parseBuildProgress(const std::string& line, float& fraction) {
        if (line.empty() || line[0] != '[') return false;
        size_t close = line.find(']');
        if (close == std::string::npos || close > 16) return false;
        std::string inner = line.substr(1, close - 1);
        try {
            size_t slash = inner.find('/');
            if (slash != std::string::npos) {
                int current = std::stoi(inner.substr(0, slash));
                int total = std::stoi(inner.substr(slash + 1));
                if (total <= 0 || current < 0 || current > total) return false;
                fraction = (float)current / (float)total;
                return true;
            }
            size_t percent = inner.find('%');
            if (percent == std::string::npos) return false;
            int value = std::stoi(inner.substr(0, percent));
            if (value < 0 || value > 100) return false;
            fraction = value / 100.0f;
            return true;
        } catch (...) {
            return false;
        }
    }

    void writeU8(std::ofstream& out, uint8_t value) {
        out.put(static_cast<char>(value));
    }

    void writeU16(std::ofstream& out, uint16_t value) {
        out.put(static_cast<char>(value & 0xff));
        out.put(static_cast<char>((value >> 8) & 0xff));
    }

    void writeU32(std::ofstream& out, uint32_t value) {
        for (int i = 0; i < 4; i++) {
            out.put(static_cast<char>((value >> (i * 8)) & 0xff));
        }
    }

    void writeU64(std::ofstream& out, uint64_t value) {
        for (int i = 0; i < 8; i++) {
            out.put(static_cast<char>((value >> (i * 8)) & 0xff));
        }
    }

    // Undone by ResourcePack::deobfuscate. Not encryption: the key is in the header.
    void obfuscate(std::vector<unsigned char>& data, uint8_t key, uint32_t shift) {
        if (data.empty()) return;

        shift %= data.size();
        if (shift > 0) {
            std::rotate(data.begin(), data.end() - shift, data.end());
        }

        for (unsigned char& byte : data) {
            byte ^= key;
        }
    }

    bool isReservedPackName(std::string name) {
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return name == RESOURCE_PACK_FILENAME || name == RESOURCE_PACK_TEMP_FILENAME;
    }

    // From what the header already carries, so both passes agree without the bytes
    void deriveObfuscation(ResourcePackBuildEntry& entry) {
        const size_t hash = std::hash<std::string>{}(entry.path) ^ (static_cast<size_t>(entry.size) << 1);
        entry.key = static_cast<uint8_t>((hash % 255) + 1);
        entry.shift = entry.size == 0 ? 0 : static_cast<uint32_t>(hash % entry.size);
    }

    bool readWholeFile(const fs::path& path, std::vector<unsigned char>& data) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        in.seekg(0, std::ios::end);
        std::streamoff size = in.tellg();
        if (size < 0) return false;
        in.seekg(0, std::ios::beg);
        data.resize(static_cast<size_t>(size));
        return data.empty() || static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), size));
    }

    void replaceAll(std::string& value, const std::string& from, const std::string& to) {
        if (from.empty()) return;

        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, from.size(), to);
            pos += to.size();
        }
    }

    // Windows CI checkouts and the shipped editor artifact use CRLF. Template
    // patches match LF-only snippets, so normalize before searching/replacing.
    void normalizeNewlines(std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\r') {
                out.push_back('\n');
                if (i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
            } else {
                out.push_back(text[i]);
            }
        }
        text.swap(out);
    }

    std::string escapeGradleString(const std::string& value) {
        std::string out;
        out.reserve(value.size());
        for (char c : value) {
            if (c == '\\' || c == '"' || c == '$') {
                out += '\\';
                out += c;
            } else if (c == '\n') {
                out += "\\n";
            } else if (c == '\r') {
                out += "\\r";
            } else if (c == '\t') {
                out += "\\t";
            } else {
                out += c;
            }
        }
        return out;
    }

    std::string escapeXmlAttribute(const std::string& value) {
        std::string out;
        out.reserve(value.size());
        for (char c : value) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default: out += c; break;
            }
        }
        return out;
    }

    // Android parses string-resource escapes after decoding XML entities.
    // Quoting preserves whitespace and literal @/? prefixes in application names.
    std::string escapeAndroidStringResource(const std::string& value) {
        std::string out = "\"";
        for (char c : value) {
            switch (c) {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out += c; break;
            }
        }
        return escapeXmlAttribute(out + '"');
    }

    std::string androidOrientationManifestValue(editor::AndroidOrientation orientation) {
        switch (orientation) {
            case editor::AndroidOrientation::Portrait: return "portrait";
            case editor::AndroidOrientation::Landscape: return "landscape";
            case editor::AndroidOrientation::SensorPortrait: return "sensorPortrait";
            case editor::AndroidOrientation::SensorLandscape: return "sensorLandscape";
            case editor::AndroidOrientation::FullSensor: return "fullSensor";
            case editor::AndroidOrientation::Unspecified:
            default: return "";
        }
    }

    const char* androidPermissionManifestName(const std::string& key) {
        struct PermissionMap {
            const char* key;
            const char* manifestName;
        };
        static const PermissionMap permissions[] = {
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
        for (const PermissionMap& permission : permissions) {
            if (key == permission.key) return permission.manifestName;
        }
        return nullptr;
    }

    fs::path resolveProjectFile(editor::Project* project, const fs::path& path) {
        if (path.empty() || path.is_absolute()) return path;
        return project->getProjectPath() / path;
    }

    bool copyAndroidResourceFile(editor::Project* project, const fs::path& sourcePath, const fs::path& targetPath, std::string& error) {
        if (sourcePath.empty()) return true;

        std::error_code ec;
        fs::create_directories(targetPath.parent_path(), ec);
        if (ec) {
            error = "Failed to create Android resource directory: " + ec.message();
            return false;
        }

        fs::path source = resolveProjectFile(project, sourcePath);
        fs::copy_file(source, targetPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "Failed to copy Android icon " + source.string() + ": " + ec.message();
            return false;
        }
        return true;
    }

    std::string stripDesktopEntryControlChars(const std::string& value) {
        std::string out;
        out.reserve(value.size());
        for (char c : value) {
            out += (c == '\n' || c == '\r') ? ' ' : c;
        }
        return out;
    }

    void replacePlistStringValue(std::string& plist, const std::string& key, const std::string& value) {
        const std::string keyTag = "\t<key>" + key + "</key>";
        size_t keyPos = plist.find(keyTag);
        if (keyPos == std::string::npos) {
            const size_t dictEnd = plist.rfind("</dict>");
            if (dictEnd == std::string::npos) return;
            plist.insert(dictEnd, "\t<key>" + key + "</key>\n\t<string>" + escapeXmlAttribute(value) + "</string>\n");
            return;
        }

        size_t stringStart = plist.find("<string>", keyPos);
        size_t stringEnd = plist.find("</string>", stringStart);
        if (stringStart == std::string::npos || stringEnd == std::string::npos) return;
        stringStart += std::string("<string>").size();
        plist.replace(stringStart, stringEnd - stringStart, escapeXmlAttribute(value));
    }

    void replacePlistBoolValue(std::string& plist, const std::string& key, bool value) {
        const std::string keyTag = "\t<key>" + key + "</key>";
        const std::string boolTag = value ? "\t<true/>\n" : "\t<false/>\n";
        size_t keyPos = plist.find(keyTag);
        if (keyPos == std::string::npos) {
            const size_t dictEnd = plist.rfind("</dict>");
            if (dictEnd == std::string::npos) return;
            plist.insert(dictEnd, keyTag + "\n" + boolTag);
            return;
        }

        size_t valueStart = plist.find_first_not_of(" \t\r\n", keyPos + keyTag.size());
        if (valueStart == std::string::npos) return;
        if (plist.compare(valueStart, 7, "<true/>") == 0) {
            plist.replace(valueStart, 7, value ? "<true/>" : "<false/>");
        } else if (plist.compare(valueStart, 8, "<false/>") == 0) {
            plist.replace(valueStart, 8, value ? "<true/>" : "<false/>");
        }
    }

    std::array<int, 4> parseWindowsVersion(const std::string& value) {
        std::array<int, 4> parts{1, 0, 0, 0};
        size_t start = 0;
        for (int i = 0; i < 4 && start <= value.size(); i++) {
            size_t dot = value.find('.', start);
            std::string token = value.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            try {
                parts[i] = std::clamp(std::stoi(token), 0, 65535);
            } catch (...) {
                parts[i] = 0;
            }
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        return parts;
    }

    std::string escapeWindowsRcString(const std::string& value) {
        std::string out;
        out.reserve(value.size());
        for (char c : value) {
            if (c == '\\' || c == '"') out += '\\';
            if (c == '\n' || c == '\r') out += ' ';
            else out += c;
        }
        return out;
    }

}

editor::Exporter::Exporter() {
}

editor::Exporter::~Exporter() {
    cancelRequested.store(true);
    commandRunner.cancel();
    if (exportThread.joinable()) {
        exportThread.join();
    }
}

void editor::Exporter::setProgress(const std::string& step, float value) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.currentStep = step;
    progress.overallProgress = value * progressScale;
    progress.detailLine.clear();
}

void editor::Exporter::setProgressRaw(const std::string& step, float value) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.currentStep = step;
    progress.overallProgress = value;
    progress.detailLine.clear();
}

void editor::Exporter::setDetail(const std::string& line) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.detailLine = line;
}

void editor::Exporter::setError(const std::string& message) {
    std::lock_guard<std::mutex> lock(progressMutex);
    progress.failed = true;
    progress.errorMessage = message;
    Out::error("Export failed: %s", message.c_str());
}

editor::ExportProgress editor::Exporter::getProgress() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return progress;
}

bool editor::Exporter::isRunning() const {
    std::lock_guard<std::mutex> lock(progressMutex);
    return progress.started && !progress.finished && !progress.failed;
}

void editor::Exporter::cancelExport() {
    cancelRequested.store(true);
    commandRunner.cancel();
}

bool editor::Exporter::isCancelled() const {
    return cancelRequested.load();
}

void editor::Exporter::startExport(Project* proj, const ExportConfig& cfg) {
    if (exportThread.joinable()) {
        exportThread.join();
    }

    this->project = proj;
    this->config = cfg;
    cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        this->progress = ExportProgress();
        this->progress.started = true;
    }

    // Launch export process in a separate thread so UI does not block
    exportThread = std::thread(&Exporter::runExport, this);
}

bool editor::Exporter::exportProject(Project* proj, const ExportConfig& cfg) {
    if (exportThread.joinable()) {
        exportThread.join();
    }

    this->project = proj;
    this->config = cfg;
    cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        this->progress = ExportProgress();
        this->progress.started = true;
    }

    runExport();

    std::lock_guard<std::mutex> lock(progressMutex);
    return progress.finished && !progress.failed;
}

bool editor::Exporter::generateShaders(const ExportConfig& cfg) {
    if (exportThread.joinable()) {
        exportThread.join();
    }

    this->project = nullptr;
    this->config = cfg;
    cancelRequested.store(false);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        this->progress = ExportProgress();
        this->progress.started = true;
    }

    runExport();

    std::lock_guard<std::mutex> lock(progressMutex);
    return progress.finished && !progress.failed;
}

void editor::Exporter::runExport() {
    const bool shaderGenerationOnly = (project == nullptr);
    const bool buildMode = !shaderGenerationOnly && config.mode != ExportMode::SourceCode;

    // Generation steps report progress in the [0,1] SourceCode range; for build
    // modes they occupy the first half, leaving the rest for configure/build/collect.
    progressScale = buildMode ? 0.5f : 1.0f;

    if (buildMode) {
        if (!prepareCacheTarget()) return;
    } else {
        if (!checkTargetDir()) return;
    }
    if (isCancelled()) { setError("Export cancelled"); return; }

    if (shaderGenerationOnly) {
        if (!buildAndSaveShaders()) return;
        if (isCancelled()) { setError("Export cancelled"); return; }

        setProgress("Shader generation complete", 1.0f);
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progress.finished = true;
        }
        Out::info("Shaders generated successfully at: %s", config.targetDir.string().c_str());
        return;
    }

    if (!clearGenerated()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!loadAndSaveAllScenes()) return;
    resolveShaderKeys();
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyGenerated()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    // Lua before assets, which skips the sources the lua tree took
    if (!copyLua()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyAssets()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyCppScripts()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!copyEngine()) return;
    if (isCancelled()) { setError("Export cancelled"); return; }
    if (!buildAndSaveShaders()) return;
    if (config.mode == ExportMode::SourceCode) {
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!collectSourceResourcePack()) return;
    }

    if (buildMode) {
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!configureBuild()) return;
        if (isCancelled()) { setError("Export cancelled"); return; }
        if (!runBuild()) return;
        if (isCancelled()) { setError("Export cancelled"); return; }
        bool collected = (config.mode == ExportMode::Desktop)
            ? collectDesktopArtifacts()
            : collectWebArtifacts();
        if (!collected) return;
        if (isCancelled()) { setError("Export cancelled"); return; }

        setProgressRaw("Export complete", 1.0f);
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progress.finished = true;
        }
        Out::info("Project exported successfully to: %s", config.destinationDir.string().c_str());
        return;
    }

    if (isCancelled()) { setError("Export cancelled"); return; }

    setProgress("Export complete", 1.0f);
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        progress.finished = true;
    }
    Out::info("Project exported successfully to: %s", config.targetDir.string().c_str());
}

// Runs after loadAndSaveAllScenes(): saving a scene refreshes its cached keys, so
// scenes that were closed or outdated have contributed before the overrides apply.
void editor::Exporter::resolveShaderKeys() {
    if (!project) {
        return;
    }

    std::set<ShaderKey> keys;
    if (!config.shaderKeysOverride.empty()) {
        for (ShaderKey key : config.shaderKeysOverride) {
            keys.insert(ShaderPool::normalizeKey(key));
        }
    } else {
        for (const auto& sceneProject : project->getScenes()) {
            for (ShaderKey key : sceneProject.shaderKeys) {
                keys.insert(ShaderPool::normalizeKey(key));
            }
        }
    }
    for (ShaderKey key : config.shaderAdditions) {
        keys.insert(ShaderPool::normalizeKey(key));
    }
    for (ShaderKey key : config.shaderExclusions) {
        keys.erase(ShaderPool::normalizeKey(key));
    }

    config.selectedShaderKeys = std::move(keys);
}

fs::path editor::Exporter::getExportProjectRoot() const {
    return config.targetDir / "project";
}

std::string editor::Exporter::getAppName() const {
    if (project->getName().empty()) {
        return "doriax-project";
    }
    return Factory::toIdentifier(project->getName());
}

fs::path editor::Exporter::getBuildCacheDir() const {
    return project->getProjectInternalPath() / "export" / (config.mode == ExportMode::Desktop ? "desktop" : "web");
}

std::string editor::Exporter::getEffectiveGenerator() const {
    if (!config.cmakeGenerator.empty()) {
        return config.cmakeGenerator;
    }
    const char* envGenerator = std::getenv("CMAKE_GENERATOR");
    return envGenerator ? envGenerator : "";
}

bool editor::Exporter::shouldSkipExportSupportFile(const fs::path& relativePath) {
    return relativePath == "CMakeLists.txt" || relativePath == "ProjectBuild.cmake"
        || relativePath.filename() == "AGENTS.md";
}

bool editor::Exporter::isCppHeaderFile(const fs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> headerExtensions = {
        ".h", ".hpp", ".hh", ".hxx", ".h++", ".inl", ".ipp", ".tpp"
    };
    return headerExtensions.count(ext) > 0;
}

bool editor::Exporter::isCppSourceFile(const fs::path& path) {
    // C++ scripts are exported to project/scripts by copyCppScripts(); they must
    // never be duplicated into the assets or lua trees. The exported build globs
    // every .cpp under the project root recursively, so a stray source file in
    // both assets and lua would compile twice and break linking with multiple
    // definition errors. Filter by extension so unregistered/orphan scripts are
    // skipped too (not only those attached to a scene).
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> sourceExtensions = {
        ".cpp", ".cc", ".cxx", ".c++", ".c"
    };
    return sourceExtensions.count(ext) > 0 || isCppHeaderFile(path);
}

bool editor::Exporter::isLuaExportFile(const fs::path& path) {
    // The lua directory carries the scripts and the data files they can read
    // at runtime; assets and C++ scripts ship through their own copy steps.
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    static const std::set<std::string> luaExtensions = {
        ".lua", ".luac", ".json", ".txt", ".csv", ".tsv", ".xml", ".ini", ".cfg", ".conf", ".toml", ".dat"
    };
    return luaExtensions.count(ext) > 0;
}

bool editor::Exporter::isLuaSourceFile(const fs::path& path) {
    // Only the lua tree needs these; every platform puts it on the "lua://" lookup path
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".lua" || ext == ".luac";
}

bool editor::Exporter::checkTargetDir() {
    setProgress("Checking target directory...", 0.0f);

    if (config.targetDir.empty()) {
        setError("Target directory not specified");
        return false;
    }

    std::error_code ec;
    if (!fs::exists(config.targetDir, ec)) {
        fs::create_directories(config.targetDir, ec);
        if (ec) {
            setError("Failed to create target directory: " + ec.message());
            return false;
        }
    }

    if (project && !config.overwriteTarget && !fs::is_empty(config.targetDir, ec)) {
        setError("Target directory is not empty");
        return false;
    }

    return true;
}

bool editor::Exporter::prepareCacheTarget() {
    setProgress("Preparing build cache...", 0.0f);

    if (config.destinationDir.empty()) {
        setError("Destination directory not specified");
        return false;
    }

    config.targetDir = getBuildCacheDir();

    std::error_code ec;
    fs::create_directories(config.targetDir, ec);
    if (ec) {
        setError("Failed to create build cache directory: " + ec.message());
        return false;
    }

    // Stale generated sources from renamed/deleted scenes would be picked up by
    // the exported CMake's recursive glob and break the link, so the (small)
    // project tree is wiped and fully re-copied every export. The engine tree is
    // kept and synced with copyTreeIfChanged() so the build stays incremental.
    fs::remove_all(getExportProjectRoot(), ec);
    if (ec) {
        setError("Failed to clear cached project sources: " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::configureBuild() {
    setProgressRaw("Configuring build...", 0.5f);

    if (isCancelled()) { setError("Export cancelled"); return false; }

    const fs::path buildDir = config.targetDir / "build";

    // Only explicit VS kits are pinned. Default export may freely select a
    // different toolchain because it rebuilds the engine from source.
    const std::string platform = config.mode == ExportMode::Desktop
        ? Generator::getGeneratorPlatform(config.cmakeGenerator) : "";
    std::string emcmake;
    std::string kitId;
    if (config.mode == ExportMode::Web) {
        EmsdkInfo emsdk = detectEmsdk(config.emsdkPath);
        if (!emsdk.found) {
            setError("Emscripten SDK not found. Configure it in Editor Settings > Web, set EMSDK, or add emcmake to PATH.");
            return false;
        }
        emcmake = emsdk.emcmake;
        Out::info("Using Emscripten %s", emsdk.description.c_str());
        kitId = "web\n" + emcmake + "\n" + config.buildType + "\n" + config.graphicBackend;
    } else {
        std::string effectiveGenerator = getEffectiveGenerator();
        // The Xcode generator builds a macOS .app bundle (MACOSX_BUNDLE in the
        // engine CMake), whose layout artifact collection does not handle and
        // whose assets would need embedding into the bundle. Every other
        // generator on macOS uses the sokol backend with a plain executable.
        if (effectiveGenerator == "Xcode") {
            if (config.cmakeGenerator.empty()) {
                setError("Desktop export does not support the Xcode generator (it produces an app bundle). Unset the CMAKE_GENERATOR environment variable to use the default toolchain.");
            } else {
                setError("Desktop export does not support the Xcode generator (it produces an app bundle). Clear the generator in Editor Settings to use the default toolchain.");
            }
            return false;
        }
        kitId = "desktop\n" + effectiveGenerator + "\n" + config.cmakeCCompiler + "\n" + config.cmakeCxxCompiler + "\n" + config.buildType + "\n" + config.graphicBackend;
        if (!platform.empty()) kitId += "\n" + platform;
    }

    // CMake cannot switch generators or toolchains in place and its cache pins
    // the export path: wipe the build tree whenever the kit differs.
    kitId += "\n" + config.targetDir.lexically_normal().generic_string();

    std::error_code ec;
    const fs::path kitMarker = buildDir / ".doriax_export_kit";
    if (fs::exists(buildDir, ec)) {
        std::string prevKit;
        {
            std::ifstream f(kitMarker);
            if (f.is_open()) {
                prevKit.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            }
        }
        if (prevKit != kitId) {
            Out::warning("Build settings or export location changed. Cleaning export build directory...");
            fs::remove_all(buildDir, ec);
            if (ec) {
                setError("Failed to clean export build directory: " + ec.message());
                return false;
            }
        }
    }

    // CMake re-parses stored paths treating backslashes as escapes; it accepts
    // forward slashes on every platform (see Generator::configureCMake).
    auto toCMakePath = [](const std::string& p) {
        std::string out = p;
        std::replace(out.begin(), out.end(), '\\', '/');
        return out;
    };

    std::string cmd;
    if (config.mode == ExportMode::Web) {
        // The emcmake wrapper injects the Emscripten toolchain file; the
        // subsequent cmake --build needs no wrapper. emcmake resolves the cmake
        // it wraps itself, so the editor override does not apply here.
        // Keep the SDK wrapper: it selects Python using the SDK environment.
        // Calling emcmake.py with an arbitrary system python3 can break a
        // correctly configured SDK and does not fix an outdated interpreter.
        cmd = "\"" + toCMakePath(emcmake) + "\" cmake ";
    } else {
        cmd = Generator::cmakeExecutable() + " ";
        if (!config.cmakeGenerator.empty()) {
            cmd += "-G \"" + config.cmakeGenerator + "\" ";
        }
        if (!platform.empty()) {
            cmd += "-A " + platform + " ";
        }
        if (!config.cmakeCCompiler.empty()) {
            cmd += "-DCMAKE_C_COMPILER=\"" + toCMakePath(config.cmakeCCompiler) + "\" ";
        }
        if (!config.cmakeCxxCompiler.empty()) {
            cmd += "-DCMAKE_CXX_COMPILER=\"" + toCMakePath(config.cmakeCxxCompiler) + "\" ";
        }
    }
    cmd += "-DCMAKE_BUILD_TYPE=" + config.buildType + " ";
    if (!config.graphicBackend.empty()) {
        cmd += "-DGRAPHIC_BACKEND=" + config.graphicBackend + " ";
    }
    cmd += "\"" + toCMakePath(config.targetDir.string()) + "\" ";
    cmd += "-B \"" + toCMakePath(buildDir.string()) + "\"";

    if (config.mode == ExportMode::Desktop) {
        cmd = CommandRunner::msvcEnvPrefix(getEffectiveGenerator()) + cmd;
    }

    Out::info("Configuring export build: %s", cmd.c_str());
    bool incompatiblePython = false;
    bool ok = commandRunner.run(cmd, config.targetDir, [this, &incompatiblePython](const std::string& line) {
        if (line.find("TypeError: 'type' object is not subscriptable") != std::string::npos) {
            incompatiblePython = true;
        }
        Out::build("%s", line.c_str());
        setDetail(line);
    });

    if (!ok) {
        if (isCancelled()) {
            setError("Export cancelled");
        } else if (config.mode == ExportMode::Web && incompatiblePython) {
            setError("Emscripten failed with an incompatible Python interpreter. Activate the SDK environment and restart the editor from that terminal. Check EMSDK_PYTHON and use a Python version supported by your installed SDK. See Output for the traceback.");
        } else {
            setError("Build configuration failed. See the Output window for details.");
        }
        return false;
    }

    fs::create_directories(buildDir, ec);
    std::ofstream f(kitMarker);
    if (f.is_open()) {
        f << kitId;
    }

    return true;
}

bool editor::Exporter::runBuild() {
    setProgressRaw("Compiling project...", BUILD_PROGRESS_START);

    if (isCancelled()) { setError("Export cancelled"); return false; }

    const fs::path buildDir = config.targetDir / "build";

    unsigned int jobs = config.buildJobs == 0 ? Generator::getAutomaticParallelBuildJobs() : config.buildJobs;
    jobs = std::min(jobs, Generator::getMaxParallelBuildJobs());

    // A Web tree was configured by the cmake emcmake picked, and driving its
    // build with a different one either fails or reconfigures the tree.
    const std::string cmakeCmd = config.mode == ExportMode::Web
        ? std::string("cmake")
        : Generator::cmakeExecutable();

    std::string cmd = cmakeCmd + " --build \"" + buildDir.string() + "\" --config " + config.buildType + " --parallel " + std::to_string(jobs);
    if (config.mode == ExportMode::Desktop) {
        // The build step invokes the compiler/linker, so it needs the same
        // MSVC environment as configure.
        cmd = CommandRunner::msvcEnvPrefix(getEffectiveGenerator()) + cmd;
    }

    // Initialization only predicts the work denominator. Do not advance the
    // bar until MSBuild confirms work by emitting a source filename.
    MsBuildProgress msBuildProgress(buildDir, config.buildType);

    Out::info("Building export with %u parallel jobs...", jobs);
    bool ok = commandRunner.run(cmd, buildDir, [this, &msBuildProgress](const std::string& line) {
        Out::build("%s", line.c_str());
        float fraction = 0.0f;
        bool hasProgress = parseBuildProgress(line, fraction);
        float buildEnd = BUILD_PROGRESS_END;
        if (!hasProgress && msBuildProgress.consumeLine(line, fraction)) {
            hasProgress = true;
            buildEnd = MSBUILD_COMPILE_END;
        }
        {
            std::lock_guard<std::mutex> lock(progressMutex);
            progress.detailLine = line;
            if (hasProgress) {
                progress.overallProgress = BUILD_PROGRESS_START
                    + (buildEnd - BUILD_PROGRESS_START) * fraction;
            }
        }
    });

    if (!ok) {
        if (isCancelled()) {
            setError("Export cancelled");
        } else {
            setError("Build failed. See the Output window for details.");
        }
        return false;
    }

    return true;
}

bool editor::Exporter::collectDesktopArtifacts() {
    setProgressRaw("Copying artifacts...", 0.95f);

    const fs::path buildDir = config.targetDir / "build";
    std::string exeName = getAppName();
#ifdef _WIN32
    exeName += ".exe";
#endif

    std::error_code ec;
    const fs::path projectRoot = getExportProjectRoot();
    fs::path exePath = buildDir / exeName;
    if (!fs::exists(exePath, ec)) {
        // Multi-config generators (Visual Studio) place binaries in a per-config subdir.
        fs::path configExePath = buildDir / config.buildType / exeName;
        if (fs::exists(configExePath, ec)) {
            exePath = configExePath;
        } else if (fs::exists(buildDir / (getAppName() + ".app"), ec)
                   || fs::exists(buildDir / config.buildType / (getAppName() + ".app"), ec)) {
            // A stale Xcode-configured cache can slip past the generator guard.
            setError("The build produced an app bundle, which Desktop export does not support. Delete the project's .doriax/export/desktop folder and export again with the default toolchain.");
            return false;
        } else {
            setError("Built executable not found at: " + exePath.string() + " or " + configExePath.string());
            return false;
        }
    }

    fs::create_directories(config.destinationDir, ec);
    if (ec) {
        setError("Failed to create destination directory: " + ec.message());
        return false;
    }

    fs::copy_file(exePath, config.destinationDir / exeName, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        setError("Failed to copy executable: " + ec.message());
        return false;
    }

    bool packCreated = false;
    if (config.packNativeResources) {
        if (!writeNativeResourcePack(config.destinationDir / RESOURCE_PACK_FILENAME, packCreated)) {
            return false;
        }
    } else {
        if (!removeResourcePack(config.destinationDir / RESOURCE_PACK_FILENAME)) {
            return false;
        }
    }

    if (isCancelled()) {
        // A committed pack stays: nothing else in the destination holds the resources
        setError("Export cancelled");
        return false;
    }

    if (packCreated) {
        for (const char* dir : {"assets", "lua"}) {
            ec.clear();
            fs::remove_all(config.destinationDir / dir, ec);
            if (ec) {
                setError(std::string("Failed to remove unpacked ") + dir + ": " + ec.message());
                return false;
            }
        }
    } else {
        // The desktop runtime resolves "assets" and "lua" relative to the working
        // directory, so ship them next to the executable.
        for (const char* dir : {"assets", "lua"}) {
            fs::path src = projectRoot / dir;
            if (!fs::exists(src, ec)) continue;
            copyTree(src, config.destinationDir / dir, ec);
            if (ec) {
                setError(std::string("Failed to copy ") + dir + ": " + ec.message());
                return false;
            }
        }
    }

#if defined(__linux__)
    const std::string settingsCommand = Generator::cmakeExecutable() + " -DAPP_FILE=\""
        + (fs::absolute(config.destinationDir) / exeName).string() + "\" -DWEB=OFF -P \""
        + (projectRoot / "export-settings" / "apply.cmake").string() + "\"";
    if (!commandRunner.run(settingsCommand, config.targetDir, [](const std::string& line) { Out::build("%s", line.c_str()); })) {
        setError("Failed to generate Linux launcher");
        return false;
    }
#endif

    return true;
}

// Clears a pack and the temporary an interrupted write may have left beside it.
bool editor::Exporter::removeResourcePack(const fs::path& packPath) {
    std::error_code ec;
    for (const fs::path& path : {packPath, packPath.parent_path() / RESOURCE_PACK_TEMP_FILENAME}) {
        fs::remove(path, ec);
        if (ec) {
            setError("Failed to remove " + path.string() + ": " + ec.message());
            return false;
        }
    }

    return true;
}

bool editor::Exporter::writeNativeResourcePack(const fs::path& outputPath, bool& created) {
    setProgressRaw("Packing resources...", 0.95f);

    created = false;
    std::vector<ResourcePackBuildEntry> entries;
    const fs::path projectRoot = getExportProjectRoot();

    // Written aside and renamed, so a half-written pack is never left for the cleanup
    const fs::path tempPath = outputPath.parent_path() / RESOURCE_PACK_TEMP_FILENAME;

    // Sizing pass: paths and sizes settle the header, so no bytes are read yet
    std::error_code ec;
    for (const char* rootName : {"assets", "lua"}) {
        const fs::path root = projectRoot / rootName;

        fs::recursive_directory_iterator files(root, fs::directory_options::skip_permission_denied, ec);
        if (ec == std::errc::no_such_file_or_directory) continue;
        if (ec) {
            setError(std::string("Failed to read exported ") + rootName + " directory: " + ec.message());
            return false;
        }

        for (fs::recursive_directory_iterator end; files != end; files.increment(ec)) {
            if (isCancelled()) {
                setError("Export cancelled");
                return false;
            }

            const fs::path filePath = files->path();
            const bool regular = files->is_regular_file(ec);
            if (ec) {
                setError("Failed to read resource for pack: " + filePath.string() + ": " + ec.message());
                return false;
            }
            if (!regular) continue;
            // A pack left by an earlier export into the same directory is replaced, not packed
            if (filePath == outputPath || filePath == tempPath) continue;

            fs::path relPath = fs::relative(filePath, projectRoot, ec);
            if (ec || relPath.empty()) {
                setError("Failed to resolve resource path for pack: " + filePath.string());
                return false;
            }

            ResourcePackBuildEntry entry;
            entry.path = relPath.generic_string();
            if (entry.path.size() > UINT16_MAX) {
                setError("Resource path is too long for pack: " + entry.path);
                return false;
            }

            const uintmax_t fileSize = fs::file_size(filePath, ec);
            if (ec) {
                setError("Failed to read resource size for pack: " + filePath.string() + ": " + ec.message());
                return false;
            }
            // Data::open() reports lengths as unsigned int
            if (fileSize > UINT32_MAX) {
                setError("Resource is too large to pack: " + filePath.string());
                return false;
            }

            entry.source = filePath;
            entry.size = static_cast<uint64_t>(fileSize);
            deriveObfuscation(entry);

            entries.push_back(std::move(entry));
        }
        // increment() lands on end when it fails, so the loop condition exits first
        if (ec) {
            setError(std::string("Failed to walk exported ") + rootName + " directory: " + ec.message());
            return false;
        }
    }

    if (entries.empty()) {
        return removeResourcePack(outputPath);
    }

    std::sort(entries.begin(), entries.end(), [](const ResourcePackBuildEntry& a, const ResourcePackBuildEntry& b) {
        return a.path < b.path;
    });

    // magic + count, then per entry: u16 path length, path, u64 offset, u64 size, u8 key, u32 shift
    uint64_t offset = sizeof(RESOURCE_PACK_MAGIC) + 4;
    for (const auto& entry : entries) {
        offset += 2 + entry.path.size() + 8 + 8 + 1 + 4;
    }
    for (auto& entry : entries) {
        entry.offset = offset;
        offset += entry.size;
    }

    if (offset > MAX_RESOURCE_PACK_SIZE) {
        setError("Packed resources exceed the 2 GiB limit. Disable Native Resource Pack in Project Settings.");
        return false;
    }

    fs::create_directories(outputPath.parent_path(), ec);
    if (ec) {
        setError("Failed to create resource pack directory: " + ec.message());
        return false;
    }

    std::ofstream out(tempPath, std::ios::binary);
    if (!out) {
        setError("Failed to create resource pack: " + tempPath.string());
        return false;
    }

    out.write(RESOURCE_PACK_MAGIC, sizeof(RESOURCE_PACK_MAGIC));
    writeU32(out, static_cast<uint32_t>(entries.size()));

    for (const auto& entry : entries) {
        writeU16(out, static_cast<uint16_t>(entry.path.size()));
        out.write(entry.path.data(), static_cast<std::streamsize>(entry.path.size()));
        writeU64(out, entry.offset);
        writeU64(out, entry.size);
        writeU8(out, entry.key);
        writeU32(out, entry.shift);
    }

    // Write pass: one resource in memory at a time, so packing does not scale with the project
    std::vector<unsigned char> data;
    bool bodiesWritten = true;
    for (const auto& entry : entries) {
        if (isCancelled()) {
            setError("Export cancelled");
            bodiesWritten = false;
            break;
        }
        if (!readWholeFile(entry.source, data)) {
            setError("Failed to read resource for pack: " + entry.source.string());
            bodiesWritten = false;
            break;
        }
        if (data.size() != entry.size) {
            setError("Resource changed while packing: " + entry.source.string());
            bodiesWritten = false;
            break;
        }

        obfuscate(data, entry.key, entry.shift);
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
    }

    // A flush failure only surfaces on close, and Windows will not remove an open file
    out.close();
    if (bodiesWritten && !out) {
        setError("Failed to write resource pack: " + tempPath.string());
        bodiesWritten = false;
    }
    if (!bodiesWritten) {
        fs::remove(tempPath, ec);
        return false;
    }

    if (isCancelled()) {
        fs::remove(tempPath, ec);
        setError("Export cancelled");
        return false;
    }

    fs::rename(tempPath, outputPath, ec);
    if (ec) {
        setError("Failed to replace resource pack: " + ec.message());
        fs::remove(tempPath, ec);
        return false;
    }

    created = true;
    return true;
}

bool editor::Exporter::collectSourceResourcePack() {
    const fs::path projectRoot = getExportProjectRoot();
    const fs::path assetsDir = projectRoot / "assets";
    const fs::path luaDir = projectRoot / "lua";
    const fs::path packPath = assetsDir / RESOURCE_PACK_FILENAME;

    // Overwriting an earlier export keeps its files, and a pack shadows the loose trees
    if (!config.packNativeResources) {
        return removeResourcePack(packPath);
    }

    bool packCreated = false;
    if (!writeNativeResourcePack(packPath, packCreated)) {
        return false;
    }
    if (!packCreated) {
        return true;
    }

    if (isCancelled()) {
        removeResourcePack(packPath);
        setError("Export cancelled");
        return false;
    }

    // Only the pack ships, but the lua directory itself stays: the exported Xcode
    // workspace lists it as a resource folder and fails the build when it is missing.
    std::error_code ec;
    std::vector<fs::path> removable;
    if (!collectPackedTreeEntries(assetsDir, removable, packPath)) return false;
    if (!collectPackedTreeEntries(luaDir, removable)) return false;

    for (const fs::path& path : removable) {
        fs::remove_all(path, ec);
        if (ec) {
            setError("Failed to remove unpacked resource after packing: " + path.string() + ": " + ec.message());
            return false;
        }
    }

    return true;
}

// Gathers a packed tree's contents before anything is removed, so the walk never
// mutates the directory it is reading. The directory itself is left in place.
bool editor::Exporter::collectPackedTreeEntries(const fs::path& dir, std::vector<fs::path>& out, const fs::path& keep) {
    std::error_code ec;
    fs::directory_iterator entries(dir, ec);
    if (ec == std::errc::no_such_file_or_directory) return true;

    for (fs::directory_iterator end; !ec && entries != end; entries.increment(ec)) {
        if (!keep.empty() && entries->path() == keep) continue;

        out.push_back(entries->path());
    }
    if (ec) {
        setError("Failed to read exported directory " + dir.string() + ": " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::collectWebArtifacts() {
    setProgressRaw("Copying artifacts...", 0.95f);

    const fs::path buildDir = config.targetDir / "build";
    const std::string appName = getAppName();

    std::error_code ec;
    fs::create_directories(config.destinationDir, ec);
    if (ec) {
        setError("Failed to create destination directory: " + ec.message());
        return false;
    }

    for (const char* ext : {".html", ".js", ".wasm"}) {
        fs::path src = buildDir / (appName + (std::string(ext) == ".html" ? ".export.html" : ext));
        if (!fs::exists(src, ec)) {
            setError("Built web output not found: " + src.string());
            return false;
        }
        fs::copy_file(src, config.destinationDir / (appName + ext), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy " + src.filename().string() + ": " + ec.message());
            return false;
        }
    }

    const fs::path favicon = buildDir / "favicon.png";
    if (fs::exists(favicon, ec)) {
        fs::copy_file(favicon, config.destinationDir / "favicon.png", fs::copy_options::overwrite_existing, ec);
    } else {
        fs::remove(config.destinationDir / "favicon.png", ec);
    }
    if (ec) { setError("Failed to update web favicon: " + ec.message()); return false; }

    // The .data preload bundle exists only when the project has assets or lua.
    fs::path dataSrc = buildDir / (appName + ".data");
    if (fs::exists(dataSrc, ec)) {
        fs::copy_file(dataSrc, config.destinationDir / (appName + ".data"), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy " + dataSrc.filename().string() + ": " + ec.message());
            return false;
        }
    }

    return true;
}

bool editor::Exporter::clearGenerated() {
    setProgress("Clearing generated directory...", 0.05f);

    fs::path generatedDir = project->getProjectInternalPath() / "generated";

    std::error_code ec;
    if (fs::exists(generatedDir, ec)) {
        fs::remove_all(generatedDir, ec);
        if (ec) {
            setError("Failed to clear generated directory: " + ec.message());
            return false;
        }
    }
    fs::create_directories(generatedDir, ec);
    if (ec) {
        setError("Failed to recreate generated directory: " + ec.message());
        return false;
    }

    return true;
}

bool editor::Exporter::loadAndSaveAllScenes() {
    setProgress("Saving scene sources...", 0.1f);

    std::promise<bool> savePromise;
    auto saveFuture = savePromise.get_future();

    editor::getEditorHost().enqueueMainThreadTask([this, &savePromise]() {
        try {
            std::vector<uint32_t> temporarilyLoaded;
            auto& scenes = project->getScenes();

            // Load all unloaded scenes (opened=false to avoid UI side-effects)
            for (size_t i = 0; i < scenes.size(); i++) {
                auto& sceneProject = scenes[i];
                if (sceneProject.filepath.empty() || sceneProject.scene) {
                    continue;
                }
                project->loadScene(sceneProject.filepath, false, false, true);

                temporarilyLoaded.push_back(sceneProject.id);
            }

            // Save all scenes to regenerate their .cpp sources
            for (size_t i = 0; i < scenes.size(); i++) {
                auto& sceneProject = scenes[i];
                if (sceneProject.filepath.empty() || !sceneProject.scene) {
                    continue;
                }
                project->saveSceneToPath(sceneProject.id, sceneProject.filepath);
            }

            // Unload all scenes that were not loaded before export
            for (uint32_t sceneId : temporarilyLoaded) {
                SceneProject* sp = project->getScene(sceneId);
                if (sp) {
                    project->deleteSceneProject(sp);
                }
            }

            savePromise.set_value(true);
        } catch (...) {
            savePromise.set_exception(std::current_exception());
        }
    });

    try {
        saveFuture.get();
    } catch (const std::exception& e) {
        setError(std::string("Scene save failed: ") + e.what());
        return false;
    }

    return true;
}

bool editor::Exporter::copyGenerated() {
    setProgress("Copying generated files...", 0.2f);

    fs::path generatedSrc = project->getProjectInternalPath() / "generated";
    fs::path generatedDst = getExportProjectRoot();

    std::error_code ec;
    fs::create_directories(generatedDst, ec);

    // main.cpp is handled separately below. PlatformEditor.* is the GLFW host
    // the editor used to emit into every project; projects created before it
    // was dropped still carry the files, and the exported build globs every
    // *.cpp under the project root, so a stale copy would be compiled.
    static const std::set<std::string> excludedFiles = {
        "main.cpp", "PlatformEditor.h", "PlatformEditor.cpp"
    };

    if (fs::exists(generatedSrc, ec)) {
        for (auto& entry : fs::recursive_directory_iterator(generatedSrc, fs::directory_options::skip_permission_denied, ec)) {
            fs::path relativePath = fs::relative(entry.path(), generatedSrc, ec);

            if (entry.is_regular_file() && excludedFiles.count(relativePath.filename().string())) {
                continue;
            }

            fs::path destPath = generatedDst / relativePath;
            if (entry.is_directory()) {
                fs::create_directories(destPath, ec);
            } else if (entry.is_regular_file()) {
                fs::create_directories(destPath.parent_path(), ec);
                fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    // Process main.cpp: copy from Generator output but strip editor-specific parts
    fs::path mainSrc = generatedSrc / "main.cpp";
    if (fs::exists(mainSrc, ec)) {
        std::ifstream ifs(mainSrc, std::ios::in | std::ios::binary);
        if (!ifs) {
            setError("Failed to read generated main.cpp");
            return false;
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        // Remove the entry point (the exported build compiles the engine's own
        // platform main.cpp, which already defines main()). Generator brackets
        // it with markers because the block is preprocessor-conditional.
        const std::string entryBegin = "// DORIAX_ENTRY_POINT_BEGIN\n";
        const std::string entryEnd = "// DORIAX_ENTRY_POINT_END\n";
        size_t pos = content.find(entryBegin);
        if (pos != std::string::npos) {
            size_t endPos = content.find(entryEnd, pos);
            if (endPos != std::string::npos) {
                endPos += entryEnd.size();
                while (endPos < content.size() && content[endPos] == '\n') endPos++;
                content.erase(pos, endPos - pos);
            }
        }

        // Update start scene if user selected one
        if (config.startSceneId != 0) {
            std::string startSceneName;
            for (const auto& sceneProject : project->getScenes()) {
                if (sceneProject.id == config.startSceneId) {
                    startSceneName = sceneProject.name;
                    break;
                }
            }
            if (!startSceneName.empty()) {
                std::string loadPrefix = "SceneManager::loadScene(\"";
                pos = content.find(loadPrefix);
                if (pos != std::string::npos) {
                    size_t nameStart = pos + loadPrefix.size();
                    size_t nameEnd = content.find("\")", nameStart);
                    if (nameEnd != std::string::npos) {
                        content.replace(nameStart, nameEnd - nameStart, startSceneName);
                    }
                }
            }
        }

        FileUtils::writeIfChanged(generatedDst / "main.cpp", content);
    }

    // Also copy scene_scripts.cpp
    fs::path sceneScriptsSrc = project->getProjectInternalPath() / "scene_scripts.cpp";
    if (fs::exists(sceneScriptsSrc, ec)) {
        std::ifstream ifs(sceneScriptsSrc, std::ios::in | std::ios::binary);
        if (!ifs) {
            setError("Failed to read generated scene_scripts.cpp");
            return false;
        }

        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        const std::string editorOnlyComment = "// Doriax API headers for this project are provided by .doriax/engine-api; see generated CMakeLists.txt for local and upstream source references.\n";
        const std::string exportComment = "// This file binds scene script metadata to compiled C++ and Lua scripts for the current build configuration.\n";
        size_t commentPos = content.find(editorOnlyComment);
        if (commentPos != std::string::npos) {
            content.replace(commentPos, editorOnlyComment.size(), exportComment);
        }

        FileUtils::writeIfChanged(generatedDst / "scene_scripts.cpp", content);
    }

    return true;
}

bool editor::Exporter::copyAssets() {
    setProgress("Copying assets...", 0.35f);

    fs::path assetsSrc = config.assetsDir;
    if (assetsSrc.empty()) {
        assetsSrc = project->getProjectPath();
    }
    if (assetsSrc.is_relative()) {
        assetsSrc = project->getProjectPath() / assetsSrc;
    }

    std::error_code ec;
    assetsSrc = fs::weakly_canonical(assetsSrc, ec);

    if (!fs::exists(assetsSrc, ec)) {
        setError("Assets directory does not exist: " + assetsSrc.string());
        return false;
    }

    fs::path assetsDst = getExportProjectRoot() / "assets";
    fs::create_directories(assetsDst, ec);

    // References are relative to this directory, so its contents become the export root
    for (auto& entry : fs::recursive_directory_iterator(assetsSrc, fs::directory_options::skip_permission_denied, ec)) {
        fs::path relPath = fs::relative(entry.path(), assetsSrc, ec);
        if (ec || relPath.empty()) continue;

        // Skip hidden directories (starting with '.') and build directories
        std::string firstComponent = relPath.begin()->string();
        if (!firstComponent.empty() && (firstComponent[0] == '.' || firstComponent == "build")) continue;

        // The engine loads a "resources.pak" at the asset root, so the name is reserved.
        // Matched without case and before the regular-file skip: a directory aliases it too.
        if (isReservedPackName(firstComponent)) {
            setError("\"" + firstComponent + "\" is reserved for the resource pack. "
                "Rename " + entry.path().string());
            return false;
        }

        // Skip project support files that should not ship as assets
        if (shouldSkipExportSupportFile(relPath)) continue;

        // Skip C++ source/header files; registered scripts ship via copyCppScripts
        if (!entry.is_regular_file() || isCppSourceFile(entry.path())) continue;

        // Skip Lua sources already shipped in the lua tree
        if (isLuaSourceFile(entry.path()) && luaCopiedSources.count(entry.path().lexically_normal())) continue;

        fs::path destPath = assetsDst / relPath;
        fs::create_directories(destPath.parent_path(), ec);
        fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
    }

    return true;
}

bool editor::Exporter::copyLua() {
    setProgress("Copying Lua scripts...", 0.3f);

    // Cleared before the early returns: with no lua tree, assets keeps the Lua files
    luaCopiedSources.clear();

    fs::path luaSrc = config.luaDir;
    if (luaSrc.empty()) {
        return true; // No Lua directory configured, skip
    }
    if (luaSrc.is_relative()) {
        luaSrc = project->getProjectPath() / luaSrc;
    }

    std::error_code ec;
    luaSrc = fs::weakly_canonical(luaSrc, ec);

    if (!fs::exists(luaSrc, ec)) {
        return true; // Lua directory doesn't exist, not an error
    }

    // Created by the copy below, so a project without Lua scripts has no lua directory
    fs::path luaDst = getExportProjectRoot() / "lua";

    // A directory of its own holds only what the scripts need, so all of it ships. With
    // the default "." the Lua root is the project root, where the allowlist filters.
    const bool luaRootIsProjectRoot = (luaSrc == fs::weakly_canonical(project->getProjectPath(), ec));

    for (auto it = fs::recursive_directory_iterator(luaSrc, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        auto& entry = *it;
        fs::path relPath = fs::relative(entry.path(), luaSrc, ec);
        if (ec || relPath.empty()) continue;

        // Skip hidden directories (starting with '.') and build directories
        std::string firstComponent = relPath.begin()->string();
        if (!firstComponent.empty() && (firstComponent[0] == '.' || firstComponent == "build")) { it.disable_recursion_pending(); continue; }

        // Skip project support files that should not ship in the Lua directory
        if (shouldSkipExportSupportFile(relPath)) continue;

        // Skip C++ source/header files; registered scripts ship via copyCppScripts
        if (!entry.is_regular_file() || isCppSourceFile(entry.path())) continue;

        if (luaRootIsProjectRoot && !isLuaExportFile(entry.path())) continue;

        fs::path destPath = luaDst / relPath;
        fs::create_directories(destPath.parent_path(), ec);
        fs::copy_file(entry.path(), destPath, fs::copy_options::overwrite_existing, ec);
        if (!ec) luaCopiedSources.insert(entry.path().lexically_normal());
    }

    return true;
}

bool editor::Exporter::copyCppScripts() {
    setProgress("Copying C++ scripts...", 0.4f);

    fs::path scriptsDst = getExportProjectRoot() / "scripts";
    const fs::path projectRoot = project->getProjectPath();

    std::error_code ec;
    std::set<std::string> copiedPaths;
    const fs::path normalizedProjectRoot = projectRoot.lexically_normal();
    const auto projectRelativePath = [&](const fs::path& path) {
        const fs::path relPath = path.lexically_normal().lexically_relative(normalizedProjectRoot);
        if (relPath.empty() || relPath.is_absolute()
            || (relPath.begin() != relPath.end() && *relPath.begin() == "..")) {
            return fs::path();
        }
        return relPath;
    };

    for (const auto& sceneProject : project->getScenes()) {
        for (const auto& script : sceneProject.cppScripts) {
            // Copy source file
            if (!script.path.empty()) {
                fs::path srcPath = script.path;
                if (srcPath.is_relative()) {
                    srcPath = project->getProjectPath() / srcPath;
                }
                std::string pathKey = srcPath.string();
                if (copiedPaths.count(pathKey)) continue;
                copiedPaths.insert(pathKey);

                if (fs::exists(srcPath, ec)) {
                    const fs::path relPath = projectRelativePath(srcPath);
                    if (!relPath.empty()) {
                        fs::path dstPath = scriptsDst / relPath;
                        fs::create_directories(dstPath.parent_path(), ec);
                        fs::copy_file(srcPath, dstPath, fs::copy_options::overwrite_existing, ec);
                    }
                }
            }

            // Copy header file
            if (!script.headerPath.empty()) {
                fs::path hdrPath = script.headerPath;
                if (hdrPath.is_relative()) {
                    hdrPath = project->getProjectPath() / hdrPath;
                }
                std::string pathKey = hdrPath.string();
                if (copiedPaths.count(pathKey)) continue;
                copiedPaths.insert(pathKey);

                if (fs::exists(hdrPath, ec)) {
                    const fs::path relPath = projectRelativePath(hdrPath);
                    if (!relPath.empty()) {
                        fs::path dstPath = scriptsDst / relPath;
                        fs::create_directories(dstPath.parent_path(), ec);
                        fs::copy_file(hdrPath, dstPath, fs::copy_options::overwrite_existing, ec);
                    }
                }
            }
        }
    }

    // Preserve unregistered support headers for project-root-relative includes.
    try {
        for (auto it = fs::recursive_directory_iterator(projectRoot, fs::directory_options::skip_permission_denied);
             it != fs::recursive_directory_iterator(); ++it) {
            const auto& entry = *it;
            const fs::path relPath = projectRelativePath(entry.path());
            if (relPath.empty()) {
                if (entry.is_directory()) it.disable_recursion_pending();
                continue;
            }
            const std::string firstComponent = relPath.begin()->string();

            if (entry.is_directory() && !firstComponent.empty()
                && (firstComponent[0] == '.' || firstComponent == "build")) {
                it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file() || !isCppHeaderFile(entry.path())
                || !copiedPaths.insert(entry.path().string()).second) {
                continue;
            }

            const fs::path dstPath = scriptsDst / relPath;
            fs::create_directories(dstPath.parent_path());
            fs::copy_file(entry.path(), dstPath, fs::copy_options::overwrite_existing);
        }
    } catch (const fs::filesystem_error& e) {
        setError("Failed to copy project headers: " + std::string(e.what()));
        return false;
    }

    // Sources under a script root are compiled without a script component
    // referencing them, so they ship even when nothing registered pulls them in.
    for (const fs::path& scriptDir : project->getScriptDirs()) {
        const fs::path relDir = scriptDirRelativePath(scriptDir);
        if (relDir.empty()) {
            Out::warning("Script directory outside the project is not exported: %s", scriptDir.generic_string().c_str());
            continue;
        }

        const fs::path rootPath = normalizedProjectRoot / relDir;
        if (!fs::is_directory(rootPath, ec)) continue;

        try {
            for (auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied);
                 it != fs::recursive_directory_iterator(); ++it) {
                const std::string name = it->path().filename().string();
                if (it->is_directory() && (name.empty() || name[0] == '.' || name == "build")) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_regular_file() || !isCppSourceFile(it->path())) continue;

                const fs::path relPath = projectRelativePath(it->path());
                if (relPath.empty() || !copiedPaths.insert(it->path().string()).second) continue;

                const fs::path dstPath = scriptsDst / relPath;
                fs::create_directories(dstPath.parent_path(), ec);
                fs::copy_file(it->path(), dstPath, fs::copy_options::overwrite_existing, ec);
            }
        } catch (const fs::filesystem_error& e) {
            setError("Failed to copy script directory: " + std::string(e.what()));
            return false;
        }
    }

    // Goes to the project root, not scripts/, because that is where the exported
    // CMakeLists includes it from.
    const fs::path userBuildFile = projectRoot / "ProjectBuild.cmake";
    if (fs::is_regular_file(userBuildFile, ec)) {
        fs::copy_file(userBuildFile, getExportProjectRoot() / "ProjectBuild.cmake",
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            setError("Failed to copy ProjectBuild.cmake: " + ec.message());
            return false;
        }
    }

    return true;
}

void editor::Exporter::copyTree(const fs::path& src, const fs::path& dst, std::error_code& ec) {
#ifdef _WIN32
    auto getWindowsLongPath = [](const fs::path& path) {
        const fs::path absolutePath = fs::absolute(path);
        const fs::path::string_type nativePath = absolutePath.native();

        if (nativePath.rfind(L"\\\\?\\", 0) == 0) {
            return absolutePath;
        }
        if (nativePath.rfind(L"\\\\", 0) == 0) {
            return fs::path(L"\\\\?\\UNC\\" + nativePath.substr(2));
        }
        return fs::path(L"\\\\?\\" + nativePath);
    };

    // Prefix absolute paths so Win32 APIs used by std::filesystem can traverse
    // directory trees beyond the legacy MAX_PATH limit.
    fs::copy(getWindowsLongPath(src),
             getWindowsLongPath(dst),
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
#else
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
#endif
}

void editor::Exporter::copyTreeIfChanged(const fs::path& src, const fs::path& dst, std::error_code& ec, bool pruneStale) {
#ifdef _WIN32
    // Prefix absolute paths so Win32 APIs used by std::filesystem can traverse
    // directory trees beyond the legacy MAX_PATH limit (see copyTree).
    auto adjustPath = [](const fs::path& path) {
        const fs::path absolutePath = fs::absolute(path);
        const fs::path::string_type nativePath = absolutePath.native();

        if (nativePath.rfind(L"\\\\?\\", 0) == 0) {
            return absolutePath;
        }
        if (nativePath.rfind(L"\\\\", 0) == 0) {
            return fs::path(L"\\\\?\\UNC\\" + nativePath.substr(2));
        }
        return fs::path(L"\\\\?\\" + nativePath);
    };
#else
    auto adjustPath = [](const fs::path& path) { return path; };
#endif

    auto filesEqual = [](const fs::path& a, const fs::path& b) -> bool {
        std::ifstream fa(a, std::ios::binary);
        std::ifstream fb(b, std::ios::binary);
        if (!fa || !fb) return false;
        constexpr size_t BUFFER_SIZE = 1 << 16;
        std::vector<char> bufA(BUFFER_SIZE), bufB(BUFFER_SIZE);
        while (true) {
            fa.read(bufA.data(), BUFFER_SIZE);
            fb.read(bufB.data(), BUFFER_SIZE);
            std::streamsize readA = fa.gcount();
            std::streamsize readB = fb.gcount();
            if (readA != readB) return false;
            if (readA == 0) return true;
            if (std::memcmp(bufA.data(), bufB.data(), (size_t)readA) != 0) return false;
        }
    };

    // Unlike copyTree, unchanged destination files are left untouched so they
    // keep their mtime and incremental builds over the tree skip recompiling.
    ec.clear();
    const fs::path srcAdj = adjustPath(src);
    const fs::path dstAdj = adjustPath(dst);
    try {
        fs::create_directories(dstAdj);
        for (const auto& entry : fs::recursive_directory_iterator(srcAdj)) {
            const fs::path target = dstAdj / fs::relative(entry.path(), srcAdj);
            if (entry.is_directory()) {
                fs::create_directories(target);
            } else if (entry.is_regular_file()) {
                if (fs::exists(target)
                    && fs::file_size(target) == entry.file_size()
                    && filesEqual(entry.path(), target)) {
                    continue;
                }
                fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
            }
        }

        // Mirror semantics for the build cache: drop destination entries the
        // source no longer has, or files removed/renamed in an SDK update
        // would keep being compiled by the exported build's recursive globs.
        if (pruneStale) {
            std::vector<fs::path> stale;
            for (const auto& entry : fs::recursive_directory_iterator(dstAdj)) {
                if (!fs::exists(srcAdj / fs::relative(entry.path(), dstAdj))) {
                    stale.push_back(entry.path());
                }
            }
            for (const fs::path& path : stale) {
                // No-op for children of an already-removed directory.
                fs::remove_all(path);
            }
        }
    } catch (const fs::filesystem_error& e) {
        ec = e.code();
    }
}

bool editor::Exporter::copyEngine() {
    setProgress("Copying engine...", 0.5f);

    fs::path exeDir = FileUtils::getExecutableDir();

    fs::path sdkRoot;
    const std::vector<fs::path> sdkCandidates = {
        FileUtils::getEngineDir(),
        exeDir / "engine",
        exeDir.parent_path() / "engine",
        exeDir
    };

    std::error_code ec;
    for (const auto& candidate : sdkCandidates) {
        if (fs::exists(candidate / "CMakeLists.txt", ec)) {
            sdkRoot = candidate;
            break;
        }
    }

    if (sdkRoot.empty()) {
        setError("Doriax SDK root not found near executable");
        return false;
    }

    auto copyDir = [&](const std::string& name, bool pruneStale) -> bool {
        fs::path src = sdkRoot / name;
        fs::path dst = config.targetDir / name;

        if (!fs::exists(src, ec)) {
            setError(name + " directory not found at: " + src.string());
            return false;
        }
        // Content-compare copy: unchanged files keep their mtime so the
        // Desktop/Web build cache stays incremental across exports.
        copyTreeIfChanged(src, dst, ec, pruneStale);
        if (ec) {
            setError("Failed to copy " + name + " directory: " + ec.message());
            return false;
        }
        return true;
    };

    if (!copyDir("core", true)) return false;
    if (!copyDir("libs", true)) return false;
    if (!copyDir("platform", true)) return false;
    if (!copyDir("renders", true)) return false;
    //if (!copyDir("tools")) return false;
    if (!copyDir("workspaces", true)) return false;
    if (config.mode == ExportMode::SourceCode) {
        if (!writeAndroidProjectSettings()) return false;
        if (!writeAppleProjectSettings()) return false;
    }

    // The SDK "shaders" dir holds only stub headers (getBase64Shader returning
    // "") under the exact names buildAndSaveShaders generates into. Copy them
    // only when missing: syncing would clobber the previous export's generated
    // headers with stubs just to regenerate them, bumping their mtime and
    // recompiling every TU that includes them on each incremental export.
    {
        fs::path src = sdkRoot / "shaders";
        fs::path dst = config.targetDir / "shaders";
        if (!fs::exists(src, ec)) {
            setError("shaders directory not found at: " + src.string());
            return false;
        }
        fs::create_directories(dst, ec);
        if (ec) {
            setError("Failed to create shaders directory: " + ec.message());
            return false;
        }
        for (const auto& entry : fs::directory_iterator(src)) {
            if (!entry.is_regular_file()) continue;
            fs::path target = dst / entry.path().filename();
            if (fs::exists(target, ec)) continue;
            fs::copy_file(entry.path(), target, ec);
            if (ec) {
                setError("Failed to copy shaders directory: " + ec.message());
                return false;
            }
        }
    }

    fs::path cmakeSrc = sdkRoot / "CMakeLists.txt";
    fs::path cmakeDst = config.targetDir / "CMakeLists.txt";
    if (!fs::exists(cmakeSrc, ec)) {
        setError("CMakeLists.txt not found at: " + cmakeSrc.string());
        return false;
    }

    std::string appName = getAppName();

    // Patch from the SDK source and write with writeIfChanged so an unchanged
    // result keeps its mtime (no spurious reconfigure of the build cache).
    std::ifstream cmakeIfs(cmakeSrc, std::ios::in | std::ios::binary);
    if (!cmakeIfs) {
        setError("Failed to read SDK CMakeLists.txt");
        return false;
    }
    std::string cmakeContent((std::istreambuf_iterator<char>(cmakeIfs)), std::istreambuf_iterator<char>());
    cmakeIfs.close();
    normalizeNewlines(cmakeContent);

    const std::string defaultAppName = "set(APP_NAME doriax-project)";
    const std::string patchedAppName = "set(APP_NAME " + appName + ")";
    const size_t appNamePos = cmakeContent.find(defaultAppName);
    if (appNamePos == std::string::npos) {
        setError("Exported CMakeLists.txt is missing default APP_NAME");
        return false;
    }
    cmakeContent.replace(appNamePos, defaultAppName.size(), patchedAppName);

    // Generate the embedded window/executable icon before the settings marker
    // so DORIAX_WINDOW_ICON is only switched on when the files exist. A bad
    // icon degrades to an iconless export (warned), never a failed one.
    bool iconGenerated = false;
    if (!project->getWindowIcon().empty()) {
        iconGenerated = writeAppIcon();
    }
    if (!iconGenerated) {
        for (const char* name : {"app_icon.h", "app_icon.ico", "app_icon.png"}) {
            fs::remove(getExportProjectRoot() / name, ec);
            if (ec) { setError("Failed to remove previous export icon: " + ec.message()); return false; }
        }
    }
    if (!writeWindowsResourceFile(iconGenerated)) {
        return false;
    }

    const std::string projectSettingsMarker = "# @DORIAX_PROJECT_SETTINGS@";
    const size_t projectSettingsPos = cmakeContent.find(projectSettingsMarker);
    if (projectSettingsPos != std::string::npos) {
        const WindowSettings window = project->getWindowSettings();

        // The title crosses two quoting layers: the CMake string literal here and
        // the C string literal it becomes through add_definitions. Escape for the
        // C level first, then for the CMake level ($ stops variable expansion).
        // Control characters are sanitized upstream in Project::getWindowSettings().
        std::string title;
        for (char c : window.title) {
            if (c == '\\' || c == '"') title += '\\';
            title += c;
        }
        std::string cmakeTitle;
        for (char c : title) {
            if (c == '\\' || c == '"' || c == '$') cmakeTitle += '\\';
            cmakeTitle += c;
        }

        // First line reuses the marker's existing indentation; subsequent lines
        // match the surrounding standalone setup block (4 spaces).
        const std::string indent = "\n    ";
        std::string projectSettings = std::string("set(DORIAX_VSYNC_ENABLED ")
            + (project->isVSyncEnabled() ? "ON" : "OFF") + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_WIDTH " + std::to_string(window.width) + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_HEIGHT " + std::to_string(window.height) + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_MODE " + std::to_string(static_cast<int>(window.mode)) + ")";
        projectSettings += indent + std::string("set(DORIAX_WINDOW_RESIZABLE ") + (window.resizable ? "ON" : "OFF") + ")";
        projectSettings += indent + "set(DORIAX_WINDOW_TITLE \"" + cmakeTitle + "\")";
        if (iconGenerated) {
            projectSettings += indent + "set(DORIAX_WINDOW_ICON ON)";
        }
        projectSettings += indent + "set(DORIAX_CXX_STANDARD " + std::to_string(project->getCxxStandard()) + ")";
        // Normal variables set before add_subdirectory(engine) shadow the engine
        // option() cache entries, so toggling physics only needs a re-configure.
        projectSettings += indent + std::string("set(DORIAX_PHYSICS_2D ") + (project->isPhysics2DEnabled() ? "ON" : "OFF") + ")";
        projectSettings += indent + std::string("set(DORIAX_PHYSICS_3D ") + (project->isPhysics3DEnabled() ? "ON" : "OFF") + ")";
        cmakeContent.replace(projectSettingsPos, projectSettingsMarker.size(), projectSettings);
    } else {
        Out::warning("Exported CMakeLists.txt is missing the project settings marker; using platform defaults");
    }

    // Script roots reach the export as include directories; their sources come in
    // through the recursive glob once copyCppScripts() has copied them.
    const std::string scriptDirsMarker = "# @DORIAX_SCRIPT_DIRS@";
    const size_t scriptDirsPos = cmakeContent.find(scriptDirsMarker);
    if (scriptDirsPos != std::string::npos) {
        cmakeContent.replace(scriptDirsPos, scriptDirsMarker.size(), buildScriptDirIncludes());
    }

    // Inject per-project HybridArray capacities so the exported build sizes its
    // fixed-capacity arrays to the larger of what the project actually uses and the
    // engine defaults in core/Engine.h (big models grow past the defaults). Missing
    // marker is non-fatal: the build simply falls back to those defaults.
    const std::string maxValuesMarker = "# @DORIAX_SCENE_MAX_VALUES@";
    const size_t maxValuesPos = cmakeContent.find(maxValuesMarker);
    if (maxValuesPos != std::string::npos) {
        cmakeContent.replace(maxValuesPos, maxValuesMarker.size(), buildSceneMaxValuesDefinitions());
    } else {
        Out::warning("Exported CMakeLists.txt is missing the SceneMaxValues marker; using engine default capacities");
    }

    if (!writeExportSettingsScript(cmakeContent)) return false;
    FileUtils::writeIfChanged(cmakeDst, cmakeContent);

    return true;
}

bool editor::Exporter::writeExportSettingsScript(std::string& cmakeContent) {
    // Bracket arguments preserve user text without CMake variable expansion.
    auto literal = [](const std::string& value) {
        std::string equals = "=";
        while (value.find("]" + equals + "]") != std::string::npos) equals += "=";
        return "[" + equals + "[" + value + "]" + equals + "]";
    };
    const WebProjectSettings& web = project->getWebProjectSettings();
    const LinuxProjectSettings& linuxSettings = project->getLinuxProjectSettings();
    const fs::path settingsDir = getExportProjectRoot() / "export-settings";
    std::error_code ec;
    fs::create_directories(settingsDir, ec);
    if (ec) { setError("Cannot create export settings directory: " + ec.message()); return false; }
    auto copySettingFile = [&](const fs::path& source, const char* name) {
        const fs::path target = settingsDir / name;
        if (source.empty()) fs::remove(target, ec);
        else fs::copy_file(resolveProjectFile(project, source), target, fs::copy_options::overwrite_existing, ec);
        if (ec) { setError("Cannot prepare export setting file: " + ec.message()); return false; }
        return true;
    };
    if (config.mode != ExportMode::Desktop
            && (!copySettingFile(web.favicon, "favicon.png") || !copySettingFile(web.customHtmlShell, "shell.html"))) return false;
    std::string script;
    auto value = [&](const char* name, const std::string& text) {
        script += "set(" + std::string(name) + " " + literal(text) + ")\n";
    };
    value("title", escapeXmlAttribute(project->getApplicationName(web.applicationName)));
    value("head", web.headInclude);
    value("resize", web.resizeCanvasToWindow ? "ON" : "OFF");
    value("hide_ui", web.hideEmscriptenUI ? "ON" : "OFF");
    value("name", stripDesktopEntryControlChars(project->getApplicationName(linuxSettings.applicationName)));
    value("comment", stripDesktopEntryControlChars(linuxSettings.comment));
    value("categories", stripDesktopEntryControlChars(linuxSettings.categories));
    script += R"cmake(
get_filename_component(output "${APP_FILE}" DIRECTORY)
get_filename_component(app "${APP_FILE}" NAME_WE)
if(WEB)
    # Keep the linker output untouched; repeated builds cannot duplicate inserts.
    file(READ "${APP_FILE}" html)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/shell.html")
        file(READ "${CMAKE_CURRENT_LIST_DIR}/shell.html" shell)
        string(FIND "${shell}" "{{DORIAX_DEFAULT_HTML}}" marker)
        if(marker EQUAL -1)
            message(FATAL_ERROR "Custom web HTML shell must contain {{DORIAX_DEFAULT_HTML}}")
        endif()
        string(REPLACE "{{DORIAX_DEFAULT_HTML}}" "${html}" html "${shell}")
    endif()
    string(FIND "${html}" "<title>" first)
    string(FIND "${html}" "</title>" last)
    if(first GREATER_EQUAL 0 AND last GREATER first)
        math(EXPR first "${first} + 7")
        string(SUBSTRING "${html}" 0 ${first} prefix)
        string(SUBSTRING "${html}" ${last} -1 suffix)
        set(html "${prefix}${title}${suffix}")
    endif()
    set(extra "${head}\n")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/favicon.png")
        file(COPY "${CMAKE_CURRENT_LIST_DIR}/favicon.png" DESTINATION "${output}")
        string(APPEND extra "<link rel=\"icon\" href=\"favicon.png\">\n")
    else()
        file(REMOVE "${output}/favicon.png")
    endif()
    if(resize)
        string(APPEND extra "<style>html, body { margin: 0; width: 100%; min-height: 100%; } #canvas { width: 100% !important; height: 100vh !important; height: 100dvh !important; display: block; border: 0; padding: 0; }</style>\n")
    endif()
    if(hide_ui)
        # Keep nodes alive: the SDK's status/print callbacks reference them.
        string(APPEND extra "<style>#emscripten_logo, .emscripten_logo, #status, #progress, #spinner, .spinner, #controls, #output, body > a[href='http://emscripten.org'], body > a[href='https://emscripten.org'] { display: none !important; } .emscripten_border { border: 0 !important; } body > hr { display: none; }</style>\n")
    endif()
    string(REPLACE "{{DORIAX_TITLE}}" "${title}" html "${html}")
    string(FIND "${html}" "{{DORIAX_HEAD_INCLUDE}}" marker)
    if(marker EQUAL -1)
        string(REPLACE "</head>" "${extra}</head>" html "${html}")
    else()
        string(REPLACE "{{DORIAX_HEAD_INCLUDE}}" "${extra}" html "${html}")
    endif()
    file(WRITE "${output}/${app}.export.html" "${html}")
else()
    set(executable "${APP_FILE}")
    foreach(character IN ITEMS "\\" "\"" "`" "$")
        string(REPLACE "${character}" "\\${character}" executable "${executable}")
    endforeach()
    set(entry "[Desktop Entry]\nType=Application\nName=${name}\nComment=${comment}\nCategories=${categories}\nExec=\"${executable}\"\nPath=${output}\nTerminal=false\nStartupWMClass=${app}\n")
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../app_icon.png")
        file(COPY "${CMAKE_CURRENT_LIST_DIR}/../app_icon.png" DESTINATION "${output}")
        string(APPEND entry "Icon=${output}/app_icon.png\n")
    else()
        file(REMOVE "${output}/app_icon.png")
    endif()
    file(WRITE "${output}/${app}.desktop" "${entry}")
endif()
)cmake";
    FileUtils::writeIfChanged(settingsDir / "apply.cmake", script);
    cmakeContent += R"cmake(
if(TARGET ${APP_NAME} AND (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    add_custom_target(doriax-export-settings ALL
        COMMAND "${CMAKE_COMMAND}" "-DAPP_FILE=$<TARGET_FILE:${APP_NAME}>" "-DWEB=${EMSCRIPTEN}"
            -P "${PROJECT_ROOT}/export-settings/apply.cmake"
        DEPENDS ${APP_NAME}
        VERBATIM)
endif()
)cmake";
    // CMake and the hand-maintained Xcode project must use the same IDs.
    replaceAll(cmakeContent, "set(APP_BUNDLE_IDENTIFIER \"org.doriax.doriaxengine\")",
        "if(CMAKE_SYSTEM_NAME STREQUAL \"iOS\")\nset(APP_BUNDLE_IDENTIFIER "
        + literal(project->getApplicationIdentifier(project->getIOSProjectSettings().bundleIdentifier))
        + ")\nelse()\nset(APP_BUNDLE_IDENTIFIER "
        + literal(project->getApplicationIdentifier(project->getMacOSProjectSettings().bundleIdentifier)) + ")\nendif()");
    replaceAll(cmakeContent, "MACOSX_BUNDLE_INFO_PLIST \"${DORIAX_ROOT}/workspaces/xcode/macos/Info.plist\"",
        "MACOSX_BUNDLE_INFO_PLIST \"${DORIAX_ROOT}/workspaces/xcode/macos/Info.plist\"\n"
        "                XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER ${APP_BUNDLE_IDENTIFIER}");
    return true;
}

fs::path editor::Exporter::scriptDirRelativePath(const fs::path& scriptDir) const {
    const fs::path projectRoot = project->getProjectPath().lexically_normal();
    const fs::path rootPath = (scriptDir.is_absolute() ? scriptDir : projectRoot / scriptDir).lexically_normal();
    const fs::path relPath = rootPath.lexically_relative(projectRoot);

    if (relPath.empty() || relPath.is_absolute() || *relPath.begin() == "..") {
        return {};
    }
    return relPath;
}

std::string editor::Exporter::buildScriptDirIncludes() const {
    // copyCppScripts() mirrors the project-relative layout under scripts/, so a
    // root keeps its path there.
    std::string dirs;
    for (const fs::path& scriptDir : project->getScriptDirs()) {
        const fs::path relDir = scriptDirRelativePath(scriptDir);
        if (relDir.empty()) {
            continue;
        }

        dirs += "\n        ${PROJECT_ROOT}/scripts";
        if (relDir != ".") {
            dirs += "/" + relDir.generic_string();
        }
    }

    if (dirs.empty()) {
        return {};
    }
    return "target_include_directories(${APP_NAME} PRIVATE" + dirs + "\n    )";
}

std::string editor::Exporter::buildSceneMaxValuesDefinitions() const {
    // Aggregate the per-scene maxima into a single project-wide capacity for each
    // HybridArray. SceneProject::maxValues is refreshed for every scene during
    // loadAndSaveAllScenes(), which always runs before copyEngine().
    SceneMaxValues agg;
    for (const SceneProject& sceneProject : project->getScenes()) {
        agg.maxSubmeshes        = std::max(agg.maxSubmeshes, sceneProject.maxValues.maxSubmeshes);
        agg.maxTilemapTilesRect = std::max(agg.maxTilemapTilesRect, sceneProject.maxValues.maxTilemapTilesRect);
        agg.maxTilemapTiles     = std::max(agg.maxTilemapTiles, sceneProject.maxValues.maxTilemapTiles);
        agg.maxExternalBuffers  = std::max(agg.maxExternalBuffers, sceneProject.maxValues.maxExternalBuffers);
        agg.maxSpriteFrames     = std::max(agg.maxSpriteFrames, sceneProject.maxValues.maxSpriteFrames);
        agg.maxBones            = std::max(agg.maxBones, sceneProject.maxValues.maxBones);
    }

    // Floor each capacity at the engine default declared in core/Engine.h: growing past the
    // default sizes big models correctly, while never shrinking below the known-safe baseline.
    // This matters because the editor-time calc can undercount model-backed meshes (e.g.
    // numExternalBuffers is regenerated at runtime, not serialized) and cannot see content
    // created dynamically by scripts. Passing the macros themselves as the floor keeps these in
    // lockstep with the engine defaults (string literals are not macro-expanded; the bare macro
    // arguments are).
    auto define = [&](const char* macro, unsigned int value, unsigned int engineDefault) {
        return std::string("add_definitions(\"-D") + macro + "=" + std::to_string(std::max(value, engineDefault)) + "\")";
    };

    // First line reuses the marker's existing indentation; subsequent lines are
    // indented to match the surrounding standalone setup block (4 spaces).
    const std::string indent = "\n    ";
    std::string out = define("MAX_SUBMESHES", agg.maxSubmeshes, MAX_SUBMESHES);
    out += indent + define("MAX_TILEMAP_TILESRECT", agg.maxTilemapTilesRect, MAX_TILEMAP_TILESRECT);
    out += indent + define("MAX_TILEMAP_TILES", agg.maxTilemapTiles, MAX_TILEMAP_TILES);
    out += indent + define("MAX_SPRITE_FRAMES", agg.maxSpriteFrames, MAX_SPRITE_FRAMES);
    out += indent + define("MAX_EXTERNAL_BUFFERS", agg.maxExternalBuffers, MAX_EXTERNAL_BUFFERS);
    out += indent + define("MAX_BONES", agg.maxBones, MAX_BONES);
    return out;
}

bool editor::Exporter::writeAndroidProjectSettings() {
    const AndroidProjectSettings& android = project->getAndroidProjectSettings();
    if (!android.abiArmeabiV7a && !android.abiArm64V8a && !android.abiX86 && !android.abiX86_64) {
        setError("Android export needs at least one selected architecture");
        return false;
    }

    auto readText = [&](const fs::path& path, std::string& out) -> bool {
        std::ifstream ifs(path, std::ios::in | std::ios::binary);
        if (!ifs) {
            setError("Failed to read Android export file: " + path.string());
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        normalizeNewlines(out);
        return true;
    };

    const fs::path androidAppDir = config.targetDir / "workspaces" / "androidstudio" / "app";
    const fs::path buildGradlePath = androidAppDir / "build.gradle";
    const fs::path manifestPath = androidAppDir / "src" / "main" / "AndroidManifest.xml";
    const fs::path stringsPath = androidAppDir / "src" / "main" / "res" / "values" / "strings.xml";
    const fs::path stylesPath = androidAppDir / "src" / "main" / "res" / "values" / "styles.xml";
    const fs::path drawableDir = androidAppDir / "src" / "main" / "res" / "drawable";
    const fs::path adaptiveIconDir = androidAppDir / "src" / "main" / "res" / "mipmap-anydpi-v26";
    const fs::path mainActivityPath = config.targetDir / "platform" / "android" / "java" / "org" / "doriaxengine" / "doriax" / "MainActivity.java";

    const std::string appName = project->getApplicationName(android.applicationName);

    std::string gradle;
    if (!readText(buildGradlePath, gradle)) return false;
    for (const char* marker : {"compileSdk 33", "applicationId \"com.yourcompany.project\"",
            "minSdkVersion 21", "targetSdkVersion 33", "versionCode 1", "versionName \"1.0\"",
            "                abiFilters \"arm64-v8a\"\n"
            "                abiFilters \"x86\"\n"
            "                abiFilters \"armeabi-v7a\"\n"
            "                abiFilters \"x86_64\""}) {
        if (gradle.find(marker) == std::string::npos) {
            setError("Incompatible Android Gradle export template: missing " + std::string(marker));
            return false;
        }
    }

    replaceAll(gradle, "compileSdk 33", "compileSdk " + std::to_string(android.targetSdk));
    replaceAll(gradle, "applicationId \"com.yourcompany.project\"",
        "applicationId \"" + escapeGradleString(project->getApplicationIdentifier(android.packageName)) + "\"");
    replaceAll(gradle, "minSdkVersion 21", "minSdkVersion " + std::to_string(android.minSdk));
    replaceAll(gradle, "targetSdkVersion 33", "targetSdkVersion " + std::to_string(android.targetSdk));
    replaceAll(gradle, "versionCode 1", "versionCode " + std::to_string(project->getApplicationVersionCode(android.versionCode)));
    replaceAll(gradle, "versionName \"1.0\"",
        "versionName \"" + escapeGradleString(project->getApplicationVersion(android.versionName)) + "\"");

    std::vector<std::string> abis;
    if (android.abiArm64V8a) abis.push_back("\"arm64-v8a\"");
    if (android.abiX86) abis.push_back("\"x86\"");
    if (android.abiArmeabiV7a) abis.push_back("\"armeabi-v7a\"");
    if (android.abiX86_64) abis.push_back("\"x86_64\"");
    std::string abiLine = "                abiFilters ";
    for (size_t i = 0; i < abis.size(); i++) {
        if (i > 0) abiLine += ", ";
        abiLine += abis[i];
    }
    replaceAll(gradle,
        "                abiFilters \"arm64-v8a\"\n"
        "                abiFilters \"x86\"\n"
        "                abiFilters \"armeabi-v7a\"\n"
        "                abiFilters \"x86_64\"",
        abiLine);
    FileUtils::writeIfChanged(buildGradlePath, gradle);

    const bool hasLauncherIcon = !android.launcherIcon.empty();
    const bool hasAdaptiveIcon = !android.adaptiveIconForeground.empty() && !android.adaptiveIconBackground.empty();
    std::string iconReference = "@mipmap/ic_launcher";

    if (hasLauncherIcon) {
        std::string copyError;
        if (!copyAndroidResourceFile(project, android.launcherIcon, drawableDir / "ic_launcher.png", copyError)) {
            setError(copyError);
            return false;
        }
        iconReference = "@drawable/ic_launcher";
    }

    if (hasAdaptiveIcon) {
        std::string copyError;
        if (!copyAndroidResourceFile(project, android.adaptiveIconForeground, drawableDir / "ic_launcher_foreground.png", copyError)) {
            setError(copyError);
            return false;
        }
        if (!copyAndroidResourceFile(project, android.adaptiveIconBackground, drawableDir / "ic_launcher_background.png", copyError)) {
            setError(copyError);
            return false;
        }

        std::string adaptiveIcon;
        adaptiveIcon += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
        adaptiveIcon += "<adaptive-icon xmlns:android=\"http://schemas.android.com/apk/res/android\">\n";
        adaptiveIcon += "    <background android:drawable=\"@drawable/ic_launcher_background\" />\n";
        adaptiveIcon += "    <foreground android:drawable=\"@drawable/ic_launcher_foreground\" />\n";
        adaptiveIcon += "</adaptive-icon>\n";
        FileUtils::writeIfChanged(adaptiveIconDir / "ic_launcher.xml", adaptiveIcon);
        iconReference = "@mipmap/ic_launcher";
    }

    std::string manifest;
    manifest += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    manifest += "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\">\n\n";
    bool wrotePermission = false;
    for (const std::string& permission : android.permissions) {
        const char* manifestName = androidPermissionManifestName(permission);
        if (!manifestName) continue;
        manifest += "    <uses-permission android:name=\"android.permission.";
        manifest += manifestName;
        manifest += "\" />\n";
        wrotePermission = true;
    }
    if (wrotePermission) manifest += "\n";
    manifest += "    <application\n";
    manifest += std::string("        android:allowBackup=\"") + (android.allowBackup ? "true" : "false") + "\"\n";
    manifest += "        android:icon=\"" + iconReference + "\"\n";
    manifest += "        android:label=\"@string/app_name\">\n\n";
    manifest += "        <meta-data\n";
    manifest += "            android:name=\"com.google.android.gms.ads.APPLICATION_ID\"\n";
    manifest += "            android:value=\"ca-app-pub-3940256099942544~3347511713\"/>\n\n";
    manifest += "        <activity android:name=\".MainActivity\"\n";
    manifest += "            android:label=\"@string/app_name\"\n";
    manifest += "            android:configChanges=\"orientation|keyboardHidden|keyboard|screenSize\"\n";
    manifest += "            android:theme=\"@style/AppTheme\"\n";
    const std::string orientation = androidOrientationManifestValue(android.orientation);
    if (!orientation.empty()) {
        manifest += "            android:screenOrientation=\"" + orientation + "\"\n";
    }
    manifest += "            android:exported=\"true\">\n";
    manifest += "            <meta-data android:name=\"android.app.lib_name\" android:value=\"doriax-android\" />\n";
    manifest += "            <intent-filter>\n";
    manifest += "                <action android:name=\"android.intent.action.MAIN\" />\n";
    manifest += "                <category android:name=\"android.intent.category.LAUNCHER\" />\n";
    manifest += "            </intent-filter>\n";
    manifest += "        </activity>\n";
    manifest += "    </application>\n\n";
    manifest += "    <uses-feature android:glEsVersion=\"0x00030000\" android:required=\"true\" />\n\n";
    manifest += "</manifest>";
    FileUtils::writeIfChanged(manifestPath, manifest);

    std::string strings;
    strings += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n";
    strings += "<resources>\n";
    strings += "    <string name=\"app_name\" formatted=\"false\">" + escapeAndroidStringResource(appName) + "</string>\n";
    strings += "</resources>";
    FileUtils::writeIfChanged(stringsPath, strings);

    std::string styles;
    styles += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n\n";
    styles += "<resources xmlns:android=\"http://schemas.android.com/apk/res/android\">\n";
    styles += "    <style name=\"AppTheme\" parent=\"Theme.AppCompat.Light.NoActionBar\">\n";
    styles += std::string("        <item name=\"android:windowFullscreen\">") + (android.fullscreen ? "true" : "false") + "</item>\n";
    styles += "    </style>\n";
    styles += "</resources>";
    FileUtils::writeIfChanged(stylesPath, styles);

    std::string activity;
    if (!readText(mainActivityPath, activity)) return false;
    if ((!android.fullscreen && (activity.find("WindowCompat.setDecorFitsSystemWindows(getWindow(), false);") == std::string::npos
            || activity.find("\t\thideSystemUI();\n") == std::string::npos))
            || (android.keepScreenOn && activity.find("\t\tsuper.onCreate(savedInstanceState);") == std::string::npos)) {
        setError("Incompatible Android Activity export template");
        return false;
    }
    if (!android.fullscreen) {
        replaceAll(activity,
            "\t\t// When true, the app will fit inside any system UI windows.\n"
            "\t\t// When false, we render behind any system UI windows.\n"
            "\t\tWindowCompat.setDecorFitsSystemWindows(getWindow(), false);\n"
            "\t\thideSystemUI();",
            "\t\t// When true, the app will fit inside any system UI windows.\n"
            "\t\t// When false, we render behind any system UI windows.\n"
            "\t\tWindowCompat.setDecorFitsSystemWindows(getWindow(), true);");
        replaceAll(activity, "\t\thideSystemUI();\n", "");
    }
    const std::string superOnCreate = "\t\tsuper.onCreate(savedInstanceState);";
    if (android.keepScreenOn && activity.find("FLAG_KEEP_SCREEN_ON") == std::string::npos) {
        replaceAll(activity, superOnCreate, "\t\tgetWindow().addFlags(LayoutParams.FLAG_KEEP_SCREEN_ON);\n\n" + superOnCreate);
    }
    FileUtils::writeIfChanged(mainActivityPath, activity);

    return true;
}

bool editor::Exporter::writeAppleProjectSettings() {
    const fs::path xcodeDir = config.targetDir / "workspaces" / "xcode";
    const fs::path iosPlistPath = xcodeDir / "ios" / "Info.plist";
    const fs::path macOSPlistPath = xcodeDir / "macos" / "Info.plist";
    const fs::path appIconSetDir = xcodeDir / "Assets.xcassets" / "AppIcon.appiconset";

    auto readText = [&](const fs::path& path, std::string& out) -> bool {
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            out.clear();
            return true;
        }
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            setError("Failed to read Apple export file: " + path.string());
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        normalizeNewlines(out);
        return true;
    };

    auto writeAppleIconPng = [&](const fs::path& sourcePath, const fs::path& outputPath, int size) -> bool {
        if (sourcePath.empty()) return true;

        fs::path source = resolveProjectFile(project, sourcePath);
        int srcWidth = 0, srcHeight = 0, srcChannels = 0;
        unsigned char* srcPixels = stbi_load(source.string().c_str(), &srcWidth, &srcHeight, &srcChannels, 4);
        if (!srcPixels) {
            setError("Apple icon could not be loaded: " + source.string());
            return false;
        }

        std::vector<unsigned char> pixels((size_t)size * size * 4);
        if (srcWidth == size && srcHeight == size) {
            memcpy(pixels.data(), srcPixels, pixels.size());
        } else {
            stbir_resize_uint8_srgb(srcPixels, srcWidth, srcHeight, 0,
                                    pixels.data(), size, size, 0, STBIR_RGBA);
        }
        stbi_image_free(srcPixels);

        std::error_code ec;
        fs::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            setError("Failed to create Apple icon directory: " + ec.message());
            return false;
        }
        if (!stbi_write_png(outputPath.string().c_str(), size, size, 4, pixels.data(), size * 4)) {
            setError("Failed to write Apple icon: " + outputPath.string());
            return false;
        }
        return true;
    };

    const MacOSProjectSettings& macOS = project->getMacOSProjectSettings();
    std::string macOSPlist;
    if (!readText(macOSPlistPath, macOSPlist)) {
        return false;
    }
    if (!macOSPlist.empty()) {
        const std::string appName = project->getApplicationName(macOS.applicationName);
        replacePlistStringValue(macOSPlist, "CFBundleName", appName);
        replacePlistStringValue(macOSPlist, "CFBundleDisplayName", appName);
        replacePlistStringValue(macOSPlist, "CFBundleIdentifier", project->getApplicationIdentifier(macOS.bundleIdentifier));
        replacePlistStringValue(macOSPlist, "CFBundleShortVersionString",
            Project::toAppleVersion(project->getApplicationVersion(macOS.versionName)));
        replacePlistStringValue(macOSPlist, "CFBundleVersion",
            Project::toAppleVersion(project->getApplicationBuild(macOS.buildNumber)));
        replacePlistBoolValue(macOSPlist, "NSHighResolutionCapable", macOS.highDpi);
        FileUtils::writeIfChanged(macOSPlistPath, macOSPlist);
    }

    const IOSProjectSettings& ios = project->getIOSProjectSettings();
    // Keep signing/build settings consistent with the generated Info.plist.
    const fs::path projectFile = xcodeDir / "Doriax.xcodeproj" / "project.pbxproj";
    std::string xcodeProject;
    if (!readText(projectFile, xcodeProject)) return false;

    // The Apple workspace builds the engine from an explicit Xcode file list,
    // not through engine/CMakeLists.txt. Mirror the project physics switches so
    // disabled backends are neither compiled nor linked there either.
    auto removeProjectLinesContaining = [&](const std::string& needle) {
        size_t match = 0;
        while ((match = xcodeProject.find(needle, match)) != std::string::npos) {
            const size_t lineStart = xcodeProject.rfind('\n', match);
            const size_t eraseStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            const size_t lineEnd = xcodeProject.find('\n', match);
            const size_t eraseEnd = lineEnd == std::string::npos ? xcodeProject.size() : lineEnd + 1;
            xcodeProject.erase(eraseStart, eraseEnd - eraseStart);
            match = eraseStart;
        }
    };
    auto removeProjectEntriesWithId = [&](const std::string& id) {
        bool removedObject = false;
        bool removedReference = false;
        size_t match = 0;
        while ((match = xcodeProject.find(id, match)) != std::string::npos) {
            const size_t lineStart = xcodeProject.rfind('\n', match);
            const size_t eraseStart = lineStart == std::string::npos ? 0 : lineStart + 1;
            const size_t lineEnd = xcodeProject.find('\n', match);
            const size_t currentLineEnd = lineEnd == std::string::npos ? xcodeProject.size() : lineEnd;
            const size_t assignment = xcodeProject.find('=', match);
            const bool isObject = assignment < currentLineEnd;

            size_t eraseEnd = lineEnd == std::string::npos ? xcodeProject.size() : lineEnd + 1;
            if (isObject) {
                // These PBXBuildFile/PBXTargetDependency objects have no nested dictionaries.
                const size_t objectStart = xcodeProject.find_first_not_of(" \t\r\n", assignment + 1);
                const size_t objectEnd = xcodeProject.find('}', objectStart);
                const size_t terminator = objectEnd == std::string::npos ? std::string::npos
                    : xcodeProject.find_first_not_of(" \t\r\n", objectEnd + 1);
                if (objectStart == std::string::npos || xcodeProject[objectStart] != '{'
                    || objectEnd == std::string::npos || terminator == std::string::npos
                    || xcodeProject[terminator] != ';'
                    || xcodeProject.find('{', objectStart + 1) < objectEnd) {
                    setError("Apple export template contains an incomplete PBX object: " + id);
                    return false;
                }
                const size_t objectLineEnd = xcodeProject.find('\n', terminator + 1);
                eraseEnd = objectLineEnd == std::string::npos ? xcodeProject.size() : objectLineEnd + 1;
                removedObject = true;
            } else {
                removedReference = true;
            }
            xcodeProject.erase(eraseStart, eraseEnd - eraseStart);
            match = eraseStart;
        }
        if (!removedObject || !removedReference) {
            setError("Apple export template is missing a PBX object or reference: " + id);
            return false;
        }
        return true;
    };

    std::vector<std::string> excludedPhysicsSources;
    if (!project->isPhysics2DEnabled()) {
        removeProjectLinesContaining("DORIAX_PHYSICS_2D,");
        excludedPhysicsSources.insert(excludedPhysicsSources.end(), {
            "Body2D.cpp", "Contact2D.cpp", "Joint2D.cpp", "Manifold2D.cpp"
        });
        for (const char* id : {"71E8247F2A9C22E100C8E6F2", "71E824822A9C235A00C8E6F2",
                               "71E8247E2A9C22D000C8E6F2", "71E824812A9C22F100C8E6F2"}) {
            if (!removeProjectEntriesWithId(id)) return false;
        }
    }
    if (!project->isPhysics3DEnabled()) {
        removeProjectLinesContaining("DORIAX_PHYSICS_3D,");
        excludedPhysicsSources.insert(excludedPhysicsSources.end(), {
            "Body3D.cpp", "CollideShapeResult3D.cpp", "Contact3D.cpp", "Joint3D.cpp"
        });
        for (const char* id : {"7105CEAA2AD62363007C91BA", "7105CEAD2AD62381007C91BA",
                               "7105CEA92AD62350007C91BA", "7105CEAC2AD62376007C91BA"}) {
            if (!removeProjectEntriesWithId(id)) return false;
        }
    }

    if (project->isPhysics2DEnabled() || project->isPhysics3DEnabled()) {
        excludedPhysicsSources.push_back("PhysicsSystemDisabled.cpp");
    } else {
        excludedPhysicsSources.push_back("PhysicsSystem.cpp");
    }

    std::string excludedSources;
    for (const std::string& source : excludedPhysicsSources) {
        if (!excludedSources.empty()) excludedSources += " ";
        excludedSources += source;
    }
    replaceAll(xcodeProject,
        "EXCLUDED_SOURCE_FILE_NAMES = PhysicsSystemDisabled.cpp;",
        "EXCLUDED_SOURCE_FILE_NAMES = \"" + excludedSources + "\";");

    size_t blockStart = 0;
    while ((blockStart = xcodeProject.find("buildSettings = {", blockStart)) != std::string::npos) {
        const size_t blockEnd = xcodeProject.find("};", blockStart);
        if (blockEnd == std::string::npos) break;
        std::string block = xcodeProject.substr(blockStart, blockEnd - blockStart);
        const bool isIOS = block.find("INFOPLIST_FILE = ios/Info.plist;") != std::string::npos;
        const bool isMacOS = block.find("INFOPLIST_FILE = macos/Info.plist;") != std::string::npos;
        if (isIOS || isMacOS) {
            const std::string key = "PRODUCT_BUNDLE_IDENTIFIER = ";
            const size_t start = block.find(key);
            const size_t end = block.find(';', start);
            if (start == std::string::npos || end == std::string::npos) {
                setError("Apple export template is missing PRODUCT_BUNDLE_IDENTIFIER");
                return false;
            }
            block.replace(start + key.size(), end - start - key.size(),
                "\"" + project->getApplicationIdentifier(isIOS ? ios.bundleIdentifier : macOS.bundleIdentifier) + "\"");
            xcodeProject.replace(blockStart, blockEnd - blockStart, block);
        }
        blockStart += block.size() + 2;
    }
    FileUtils::writeIfChanged(projectFile, xcodeProject);
    std::string iosPlist;
    if (!readText(iosPlistPath, iosPlist)) {
        return false;
    }
    if (!iosPlist.empty()) {
        const std::string appName = project->getApplicationName(ios.applicationName);
        replacePlistStringValue(iosPlist, "CFBundleName", appName);
        replacePlistStringValue(iosPlist, "CFBundleDisplayName", appName);
        replacePlistStringValue(iosPlist, "CFBundleIdentifier", project->getApplicationIdentifier(ios.bundleIdentifier));
        replacePlistStringValue(iosPlist, "CFBundleShortVersionString",
            Project::toAppleVersion(project->getApplicationVersion(ios.versionName)));
        replacePlistStringValue(iosPlist, "CFBundleVersion",
            Project::toAppleVersion(project->getApplicationBuild(ios.buildNumber)));
        replacePlistBoolValue(iosPlist, "UIStatusBarHidden", ios.hideStatusBar);
        replacePlistBoolValue(iosPlist, "CADisableMinimumFrameDurationOnPhone", ios.supportsHighRefreshRate);
        FileUtils::writeIfChanged(iosPlistPath, iosPlist);
    }

    const fs::path iosViewControllerPath = config.targetDir / "platform" / "apple" / "ios" / "ViewController.m";
    std::string viewController;
    if (!readText(iosViewControllerPath, viewController)) {
        return false;
    }
    if (!viewController.empty()) {
        if (viewController.find("prefersHomeIndicatorAutoHidden") == std::string::npos) {
            const std::string method = std::string("\n- (BOOL)prefersHomeIndicatorAutoHidden\n{\n    return ")
                + (ios.hideHomeIndicator ? "YES" : "NO") + ";\n}\n";
            const size_t endPos = viewController.rfind("@end");
            if (endPos != std::string::npos) {
                viewController.insert(endPos, method);
                FileUtils::writeIfChanged(iosViewControllerPath, viewController);
            }
        } else {
            replaceAll(viewController, "return YES;\n}\n\n@end", std::string("return ") + (ios.hideHomeIndicator ? "YES" : "NO") + ";\n}\n\n@end");
            replaceAll(viewController, "return NO;\n}\n\n@end", std::string("return ") + (ios.hideHomeIndicator ? "YES" : "NO") + ";\n}\n\n@end");
            FileUtils::writeIfChanged(iosViewControllerPath, viewController);
        }
    }

    if (!ios.icon.empty() || !macOS.icon.empty()) {
        struct AppleIconSlot {
            const char* filename;
            const char* idiom;
            const char* platform;
            const char* sizeText;
            const char* scale;
            int pixelSize;
            bool ios;
        };
        const AppleIconSlot slots[] = {
            {"AppIcon-iOS-1024.png", "universal", "ios", "1024x1024", nullptr, 1024, true},
            {"AppIcon-mac-16.png", "mac", nullptr, "16x16", "1x", 16, false},
            {"AppIcon-mac-16@2x.png", "mac", nullptr, "16x16", "2x", 32, false},
            {"AppIcon-mac-32.png", "mac", nullptr, "32x32", "1x", 32, false},
            {"AppIcon-mac-32@2x.png", "mac", nullptr, "32x32", "2x", 64, false},
            {"AppIcon-mac-128.png", "mac", nullptr, "128x128", "1x", 128, false},
            {"AppIcon-mac-128@2x.png", "mac", nullptr, "128x128", "2x", 256, false},
            {"AppIcon-mac-256.png", "mac", nullptr, "256x256", "1x", 256, false},
            {"AppIcon-mac-256@2x.png", "mac", nullptr, "256x256", "2x", 512, false},
            {"AppIcon-mac-512.png", "mac", nullptr, "512x512", "1x", 512, false},
            {"AppIcon-mac-512@2x.png", "mac", nullptr, "512x512", "2x", 1024, false},
        };

        std::string contents;
        contents += "{\n";
        contents += "  \"images\" : [\n";
        bool first = true;
        for (const AppleIconSlot& slot : slots) {
            const fs::path source = slot.ios ? ios.icon : macOS.icon;
            if (!source.empty() && !writeAppleIconPng(source, appIconSetDir / slot.filename, slot.pixelSize)) {
                return false;
            }
            if (!first) contents += ",\n";
            first = false;
            contents += "    {\n";
            if (!source.empty()) contents += "      \"filename\" : \"" + std::string(slot.filename) + "\",\n";
            contents += "      \"idiom\" : \"" + std::string(slot.idiom) + "\",\n";
            if (slot.platform) contents += "      \"platform\" : \"" + std::string(slot.platform) + "\",\n";
            if (slot.scale) contents += "      \"scale\" : \"" + std::string(slot.scale) + "\",\n";
            contents += "      \"size\" : \"" + std::string(slot.sizeText) + "\"\n";
            contents += "    }";
        }
        contents += "\n  ],\n";
        contents += "  \"info\" : {\n";
        contents += "    \"author\" : \"xcode\",\n";
        contents += "    \"version\" : 1\n";
        contents += "  }\n";
        contents += "}\n";
        FileUtils::writeIfChanged(appIconSetDir / "Contents.json", contents);
    }

    return true;
}

bool editor::Exporter::writeWindowsResourceFile(bool includeIcon) {
    const WindowsProjectSettings& windows = project->getWindowsProjectSettings();
    const std::string productName = project->getApplicationName(windows.productName);
    const std::string companyName = windows.companyName;
    const std::string fileVersionText = project->getApplicationFileVersion(windows.fileVersion);
    const std::string productVersionText = project->getApplicationFileVersion(windows.productVersion);
    const std::array<int, 4> fileVersion = parseWindowsVersion(fileVersionText);
    const std::array<int, 4> productVersion = parseWindowsVersion(productVersionText);

    const fs::path projectRoot = getExportProjectRoot();
    std::error_code ec;
    fs::create_directories(projectRoot, ec);
    if (ec) {
        setError("Failed to create Windows resource directory: " + ec.message());
        return false;
    }

    std::ofstream f(projectRoot / "app_icon.rc", std::ios::binary);
    if (!f) {
        setError("Failed to write app_icon.rc");
        return false;
    }

    if (includeIcon) {
        f << "1 ICON \"app_icon.ico\"\n\n";
    }

    f << "#include <windows.h>\n\n";
    f << "1 VERSIONINFO\n";
    f << "FILEVERSION " << fileVersion[0] << "," << fileVersion[1] << "," << fileVersion[2] << "," << fileVersion[3] << "\n";
    f << "PRODUCTVERSION " << productVersion[0] << "," << productVersion[1] << "," << productVersion[2] << "," << productVersion[3] << "\n";
    f << "FILEFLAGSMASK 0x3fL\n";
    f << "FILEFLAGS 0x0L\n";
    f << "FILEOS 0x40004L\n";
    f << "FILETYPE 0x1L\n";
    f << "FILESUBTYPE 0x0L\n";
    f << "BEGIN\n";
    f << "    BLOCK \"StringFileInfo\"\n";
    f << "    BEGIN\n";
    f << "        BLOCK \"040904b0\"\n";
    f << "        BEGIN\n";
    if (!companyName.empty()) {
        f << "            VALUE \"CompanyName\", \"" << escapeWindowsRcString(companyName) << "\\0\"\n";
    }
    f << "            VALUE \"FileDescription\", \"" << escapeWindowsRcString(productName) << "\\0\"\n";
    f << "            VALUE \"FileVersion\", \"" << escapeWindowsRcString(fileVersionText) << "\\0\"\n";
    f << "            VALUE \"InternalName\", \"" << escapeWindowsRcString(getAppName()) << "\\0\"\n";
    f << "            VALUE \"OriginalFilename\", \"" << escapeWindowsRcString(getAppName()) << ".exe\\0\"\n";
    f << "            VALUE \"ProductName\", \"" << escapeWindowsRcString(productName) << "\\0\"\n";
    f << "            VALUE \"ProductVersion\", \"" << escapeWindowsRcString(productVersionText) << "\\0\"\n";
    f << "        END\n";
    f << "    END\n";
    f << "    BLOCK \"VarFileInfo\"\n";
    f << "    BEGIN\n";
    f << "        VALUE \"Translation\", 0x409, 1200\n";
    f << "    END\n";
    f << "END\n";

    return true;
}

bool editor::Exporter::writeAppIcon() {
    fs::path iconPath = project->getWindowIcon();
    if (iconPath.is_relative()) {
        iconPath = project->getProjectPath() / iconPath;
    }

    int srcWidth = 0, srcHeight = 0, srcChannels = 0;
    unsigned char* srcPixels = stbi_load(iconPath.string().c_str(), &srcWidth, &srcHeight, &srcChannels, 4);
    if (!srcPixels) {
        Out::warning("Project icon could not be loaded, exporting without icon: %s", iconPath.string().c_str());
        return false;
    }

    const int maxDimension = std::max(srcWidth, srcHeight);

    // RGBA at a given square size, resized from the source (stretch: icons are
    // expected to be square). Gamma-aware so downscaled icons keep their tone.
    auto makeSize = [&](int size) -> std::vector<unsigned char> {
        std::vector<unsigned char> pixels((size_t)size * size * 4);
        if (size == srcWidth && size == srcHeight) {
            memcpy(pixels.data(), srcPixels, pixels.size());
        } else {
            stbir_resize_uint8_srgb(srcPixels, srcWidth, srcHeight, 0,
                                    pixels.data(), size, size, 0, STBIR_RGBA);
        }
        return pixels;
    };

    // Skip sizes larger than the source (upscaling only blurs); keep at least one.
    auto filterSizes = [&](std::initializer_list<int> wanted) {
        std::vector<int> sizes;
        for (int size : wanted) {
            if (size <= maxDimension) sizes.push_back(size);
        }
        if (sizes.empty()) sizes.push_back(*wanted.begin());
        return sizes;
    };

    // --- app_icon.h: RGBA images the window backends set at startup ---
    const std::vector<int> runtimeSizes = filterSizes({16, 32, 64, 128});
    std::string header;
    header += "// Generated by the Doriax editor export from the project icon. Do not edit.\n";
    header += "#ifndef DORIAX_APP_ICON_H\n#define DORIAX_APP_ICON_H\n\n";
    header += "typedef struct DoriaxAppIconImage { int width; int height; const unsigned char* pixels; } DoriaxAppIconImage;\n\n";
    for (int size : runtimeSizes) {
        std::vector<unsigned char> pixels = makeSize(size);
        header += "static const unsigned char doriax_app_icon_" + std::to_string(size) + "[] = {";
        char buf[8];
        for (size_t i = 0; i < pixels.size(); i++) {
            if (i % 32 == 0) header += "\n";
            snprintf(buf, sizeof(buf), "%u,", pixels[i]);
            header += buf;
        }
        header += "\n};\n\n";
    }
    header += "static const DoriaxAppIconImage DORIAX_APP_ICON_IMAGES[] = {\n";
    for (int size : runtimeSizes) {
        std::string s = std::to_string(size);
        header += "    {" + s + ", " + s + ", doriax_app_icon_" + s + "},\n";
    }
    header += "};\n\n";
    header += "#define DORIAX_APP_ICON_COUNT " + std::to_string(runtimeSizes.size()) + "\n\n";
    header += "#endif // DORIAX_APP_ICON_H\n";

    // --- app_icon.ico: PNG-compressed entries, embedded into the Windows exe ---
    const std::vector<int> icoSizes = filterSizes({16, 32, 48, 256});
    std::string ico;
    std::vector<std::string> icoPngs;
    auto appendPng = [](void* context, void* data, int size) {
        ((std::string*)context)->append((const char*)data, (size_t)size);
    };
    for (int size : icoSizes) {
        std::vector<unsigned char> pixels = makeSize(size);
        std::string png;
        if (!stbi_write_png_to_func(appendPng, &png, size, size, 4, pixels.data(), size * 4)) {
            Out::warning("Failed to encode project icon at %dx%d, exporting without icon", size, size);
            stbi_image_free(srcPixels);
            return false;
        }
        icoPngs.push_back(std::move(png));
    }

    // --- app_icon.png: launcher-size PNG for the Linux .desktop entry ---
    std::string launcherPng;
    {
        const int launcherSize = std::min(maxDimension, 256);
        std::vector<unsigned char> pixels = makeSize(launcherSize);
        if (!stbi_write_png_to_func(appendPng, &launcherPng, launcherSize, launcherSize, 4, pixels.data(), launcherSize * 4)) {
            Out::warning("Failed to encode project icon at %dx%d, exporting without icon", launcherSize, launcherSize);
            stbi_image_free(srcPixels);
            return false;
        }
    }
    stbi_image_free(srcPixels);

    auto appendU16 = [&ico](uint16_t value) {
        ico += (char)(value & 0xFF);
        ico += (char)((value >> 8) & 0xFF);
    };
    auto appendU32 = [&ico](uint32_t value) {
        ico += (char)(value & 0xFF);
        ico += (char)((value >> 8) & 0xFF);
        ico += (char)((value >> 16) & 0xFF);
        ico += (char)((value >> 24) & 0xFF);
    };

    appendU16(0);                            // reserved
    appendU16(1);                            // type: icon
    appendU16((uint16_t)icoSizes.size());    // image count
    uint32_t dataOffset = 6 + 16 * (uint32_t)icoSizes.size();
    for (size_t i = 0; i < icoSizes.size(); i++) {
        int size = icoSizes[i];
        ico += (char)(size >= 256 ? 0 : size); // width (0 = 256)
        ico += (char)(size >= 256 ? 0 : size); // height
        ico += (char)0;                        // palette colors
        ico += (char)0;                        // reserved
        appendU16(1);                          // color planes
        appendU16(32);                         // bits per pixel
        appendU32((uint32_t)icoPngs[i].size());
        appendU32(dataOffset);
        dataOffset += (uint32_t)icoPngs[i].size();
    }
    for (const std::string& png : icoPngs) {
        ico += png;
    }

    const fs::path projectRoot = getExportProjectRoot();
    std::error_code ec;
    fs::create_directories(projectRoot, ec);

    {
        std::ofstream f(projectRoot / "app_icon.h", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.h, exporting without icon");
            return false;
        }
        f << header;
    }
    {
        std::ofstream f(projectRoot / "app_icon.ico", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.ico, exporting without icon");
            return false;
        }
        f.write(ico.data(), (std::streamsize)ico.size());
    }
    {
        std::ofstream f(projectRoot / "app_icon.png", std::ios::binary);
        if (!f) {
            Out::warning("Failed to write app_icon.png, exporting without icon");
            return false;
        }
        f.write(launcherPng.data(), (std::streamsize)launcherPng.size());
    }

    return true;
}

bool editor::Exporter::buildAndSaveShaders() {
    setProgress("Building shaders...", 0.6f);

    if (config.selectedShaderKeys.empty()) {
        setError("No shaders selected");
        return false;
    }

    // Drop retired/reserved property bits so keys collected before a feature bit was
    // retired collapse onto their current equivalent (the std::set dedupes them),
    // instead of exporting stale duplicate variants with '?' in their names.
    {
        std::set<ShaderKey> normalizedKeys;
        for (const ShaderKey& key : config.selectedShaderKeys) {
            normalizedKeys.insert(ShaderPool::normalizeKey(key));
        }
        config.selectedShaderKeys = std::move(normalizedKeys);
    }

    // Shader-only generation writes straight into the requested dir
    fs::path shadersDst = project ? config.targetDir / "shaders" : config.targetDir;

    std::error_code ec;
    fs::create_directories(shadersDst, ec);
    if (ec) {
        setError("Failed to create shaders directory: " + ec.message());
        return false;
    }

    shadersDst = fs::absolute(shadersDst, ec);
    if (ec) {
        setError("Failed to resolve shaders directory: " + ec.message());
        return false;
    }

    // Default to glsl410 if no backend was selected
    std::vector<ShaderBackend> backends(config.selectedBackends.begin(), config.selectedBackends.end());
    if (backends.empty()) {
        backends.push_back(ShaderBackend::GLCore);
    }

    int total = (int)config.selectedShaderKeys.size() * backends.size();
    int current = 0;
    bool hadFailure = false;
    std::map<std::string, std::vector<ShaderHeaderBuilder::HeaderShader>> headerShaders;

    for (const ShaderKey& shaderKey : config.selectedShaderKeys) {
        ShaderType type = ShaderPool::getShaderTypeFromKey(shaderKey);
        uint32_t props = ShaderPool::getPropertiesFromKey(shaderKey);
        uint16_t customId = ShaderPool::getCustomIdFromKey(shaderKey);
        std::string shaderStr = ShaderPool::getShaderStr(type, props, customId);

        for (ShaderBackend backend : backends) {
            float shaderProgress = 0.6f + (0.3f * (float)current / (float)std::max(total, 1));
            std::string fmtStr = ShaderPool::getShaderLangStr(backend);
            setProgress("Building shader: " + shaderStr + " (" + fmtStr + ")", shaderProgress);

            try {
                // Synchronous build without cache
                ShaderData resultData = shaderBuilder.buildShaderForExport(shaderKey, project, backend);

                std::string basename = shaderStr + "_" + fmtStr;
                std::string err;

                // Persist with the storage key (customId stripped): the runtime assigns
                // its own session-local id, so the embedded key must not depend on it.
                ShaderKey storageKey = ShaderPool::getStorageKey(shaderKey);

                if (config.shaderOutputFormat == ShaderOutputFormat::Header) {
                    std::vector<unsigned char> sdatBytes;
                    if (!ShaderDataSerializer::writeToBytes(sdatBytes, storageKey, resultData, &err)) {
                        Out::warning("Failed to encode shader %s: %s", basename.c_str(), err.c_str());
                        hadFailure = true;
                    } else {
                        headerShaders[fmtStr].push_back({basename, Base64::encode(sdatBytes.data(), sdatBytes.size())});
                    }
                } else if (config.shaderOutputFormat == ShaderOutputFormat::Json) {
                    std::string filename = basename + ".json";
                    fs::path outputPath = shadersDst / filename;
                    if (!ShaderDataSerializer::writeJsonToFile(outputPath.string(), storageKey, resultData, &err)) {
                        Out::warning("Failed to save shader %s: %s", filename.c_str(), err.c_str());
                        hadFailure = true;
                    }
                } else {
                    std::string filename = basename + ".sdat";
                    fs::path outputPath = shadersDst / filename;
                    if (!ShaderDataSerializer::writeToFile(outputPath.string(), storageKey, resultData, &err)) {
                        Out::warning("Failed to save shader %s: %s", filename.c_str(), err.c_str());
                        hadFailure = true;
                    }
                }
            } catch (const std::exception& e) {
                Out::warning("Failed to build shader %s (%s): %s", shaderStr.c_str(), fmtStr.c_str(), e.what());
                hadFailure = true;
            }

            current++;
        }
    }

    if (config.shaderOutputFormat == ShaderOutputFormat::Header) {
        setProgress("Writing shader headers...", 0.95f);
        for (ShaderBackend backend : backends) {
            const std::string fmtStr = ShaderPool::getShaderLangStr(backend);
            std::string err;
            const auto it = headerShaders.find(fmtStr);
            const std::vector<ShaderHeaderBuilder::HeaderShader> emptyShaders;
            const std::vector<ShaderHeaderBuilder::HeaderShader>& shaders = it == headerShaders.end() ? emptyShaders : it->second;
            if (!ShaderHeaderBuilder::writeShaderHeader(shadersDst / (fmtStr + ".h"), fmtStr, shaders, err)) {
                Out::warning("Failed to write shader header %s.h: %s", fmtStr.c_str(), err.c_str());
                hadFailure = true;
            }
        }
    }

    if (hadFailure) {
        setError("One or more shaders failed to build or save");
        return false;
    }

    return true;
}

// Static helpers for UI display

std::string editor::Exporter::getShaderDisplayName(ShaderType type, uint32_t properties, uint16_t customId) {
    std::string name = ShaderPool::getShaderTypeName(type);

    // Distinguish forked shaders (same type/props, different source) in the list.
    std::string customName = ShaderPool::getCustomShaderName(customId);
    if (!customName.empty()) {
        name += " [" + customName + "]";
    }

    int propCount = ShaderPool::getShaderPropertyCount(type);

    std::string props;
    for (int i = 0; i < propCount; i++) {
        if (properties & (1 << i)) {
            if (!props.empty()) props += ", ";
            props += ShaderPool::getShaderPropertyName(type, i, true);
        }
    }

    if (!props.empty()) {
        name += " (" + props + ")";
    }

    return name;
}

std::string editor::Exporter::getCMakeGraphicBackend(::doriax::ShaderBackend backend) {
    switch (backend) {
        case ShaderBackend::GLES3:      return "gles3";
        case ShaderBackend::D3D11:      return "d3d11";
        case ShaderBackend::MetalMacOS:
        case ShaderBackend::MetalIOS:   return "metal";
        case ShaderBackend::Vulkan:     return "vulkan";
        default:                        return "glcore";
    }
}

editor::EmsdkInfo editor::Exporter::detectEmsdk(const std::string& overridePath) {
    EmsdkInfo info;

#ifdef _WIN32
    const char* emcmakeName = "emcmake.bat";
#else
    const char* emcmakeName = "emcmake";
#endif

    auto tryRoot = [&](const fs::path& root, const std::string& source) -> bool {
        if (root.empty()) return false;
        std::error_code ec;
        // Accept both the emsdk root and the emscripten dir pointed at directly.
        const fs::path candidates[] = {
            root / "upstream" / "emscripten" / emcmakeName,
            root / emcmakeName
        };
        for (const fs::path& candidate : candidates) {
            if (fs::exists(candidate, ec)) {
                info.found = true;
                info.emcmake = candidate.string();
                info.description = "from " + source;
                return true;
            }
        }
        return false;
    };

    // An explicit override is an explicit choice: when set, it alone decides.
    if (!overridePath.empty()) {
        tryRoot(fs::path(overridePath), "configured path");
        return info;
    }

    const char* emsdkEnv = std::getenv("EMSDK");
    if (emsdkEnv && tryRoot(fs::path(emsdkEnv), "EMSDK environment variable")) {
        return info;
    }

#ifdef _WIN32
    if (!CommandRunner::runCaptureNoWindow("where emcmake.bat 2>nul").empty()) {
        info.found = true;
        info.emcmake = "emcmake";
        info.description = "on PATH";
    }
#else
    if (std::system("command -v emcmake >/dev/null 2>&1") == 0) {
        info.found = true;
        info.emcmake = "emcmake";
        info.description = "on PATH";
    }
#endif

    return info;
}
