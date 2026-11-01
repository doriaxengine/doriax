// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ConversationStore.h"

#include "Project.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>

namespace fs = std::filesystem;

namespace doriax::editor::ai {

namespace {

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

Json messageToJson(const ChatMessage& message) {
    Json out;
    out["role"] = toString(message.role);
    out["content"] = message.content;
    if (!message.model.empty()) out["model"] = message.model;
    if (!message.attachments.empty()) {
        out["attachments"] = Json::array();
        for (const ChatAttachment& attachment : message.attachments) {
            out["attachments"].push_back({
                {"name", attachment.name},
                {"mime_type", attachment.mimeType},
                {"data", attachment.data}
            });
        }
    }
    if (!message.toolCallId.empty()) out["tool_call_id"] = message.toolCallId;
    if (!message.toolName.empty()) out["tool_name"] = message.toolName;
    if (!message.toolDescription.empty()) out["tool_description"] = message.toolDescription;
    if (message.role == ChatRole::Tool) out["tool_success"] = message.toolSuccess;
    if (!message.toolCalls.empty()) {
        Json calls = Json::array();
        for (const ToolCall& call : message.toolCalls) {
            Json entry = {{"id", call.id}, {"name", call.name}, {"arguments", call.arguments}};
            if (!call.thoughtSignature.empty()) {
                entry["thought_signature"] = call.thoughtSignature;
            }
            calls.push_back(entry);
        }
        out["tool_calls"] = calls;
    }
    if (!message.anthropicContent.empty()) {
        out["anthropic_content"] = message.anthropicContent;
    }
    if (!message.reasoningFields.empty()) {
        out["reasoning_fields"] = message.reasoningFields;
    }
    return out;
}

ChatMessage messageFromJson(const Json& node) {
    ChatMessage message;
    message.role = chatRoleFromString(node.value("role", "user"));
    message.content = node.value("content", "");
    message.model = node.value("model", "");
    if (node.contains("attachments") && node["attachments"].is_array()) {
        for (const Json& attachmentNode : node["attachments"]) {
            if (!attachmentNode.is_object()) continue;
            ChatAttachment attachment;
            attachment.name = attachmentNode.value("name", "");
            attachment.mimeType = attachmentNode.value("mime_type", "");
            attachment.data = attachmentNode.value("data", "");
            if (attachment.mimeType.empty()) {
                attachment.mimeType = "text/plain";
            }
            // Trust nothing from disk: the mime type flows verbatim into
            // provider payloads, where an unknown value would 400 every
            // replayed turn of the conversation.
            const bool supported = attachment.mimeType == "text/plain" ||
                                   isSupportedImageMime(attachment.mimeType);
            if (supported && !attachment.name.empty() && !attachment.data.empty()) {
                message.attachments.push_back(std::move(attachment));
            }
        }
    }
    message.toolCallId = node.value("tool_call_id", "");
    message.toolName = node.value("tool_name", "");
    message.toolDescription = node.value("tool_description", "");
    message.toolSuccess = node.value("tool_success", true);
    if (node.contains("tool_calls") && node["tool_calls"].is_array()) {
        for (const Json& callNode : node["tool_calls"]) {
            if (!callNode.is_object()) {
                continue;
            }
            ToolCall call;
            call.id = callNode.value("id", "");
            call.name = callNode.value("name", "");
            call.arguments = callNode.value("arguments", Json::object());
            call.thoughtSignature = callNode.value("thought_signature",
                callNode.value("thoughtSignature", ""));
            message.toolCalls.push_back(call);
        }
    }
    if (node.contains("anthropic_content") && node["anthropic_content"].is_array()) {
        for (const Json& block : node["anthropic_content"]) {
            if (block.is_object()) {
                message.anthropicContent.push_back(block);
            }
        }
    }
    if (node.contains("reasoning_fields") && node["reasoning_fields"].is_object()) {
        message.reasoningFields = node["reasoning_fields"];
    }
    return message;
}

} // namespace

ConversationStore::ConversationStore(Project* project)
    : project(project) {
}

fs::path ConversationStore::conversationsDir() const {
    return project->getProjectInternalPath() / "ai" / "conversations";
}

std::string ConversationStore::newId() {
    static std::atomic<uint64_t> counter{0};
    int64_t millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return std::to_string(millis) + "-" + std::to_string(counter.fetch_add(1));
}

void ConversationStore::save(const std::string& id, const std::string& title,
                             const std::vector<ChatMessage>& messages) {
    if (id.empty()) return;

    fs::path dir = conversationsDir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;

    const int64_t updatedAt = nowSeconds();
    Json root;
    root["id"] = id;
    root["title"] = title;
    root["updated_at"] = updatedAt;
    root["messages"] = Json::array();
    for (const ChatMessage& message : messages) {
        root["messages"].push_back(messageToJson(message));
    }

    // Write to a temp file and rename so a crash mid-write never truncates an
    // existing conversation. list() skips the temp file (extension is ".tmp").
    fs::path finalPath = dir / (id + ".json");
    fs::path tmpPath = dir / (id + ".json.tmp");
    {
        std::ofstream out(tmpPath, std::ios::trunc);
        if (!out) return;
        out << root.dump(2);
        out.flush();
        if (!out) {
            out.close();
            fs::remove(tmpPath, ec);
            return;
        }
    }
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        fs::remove(tmpPath, ec);
        return;
    }

    // Tiny metadata sidecar so list() never has to parse the conversation
    // body, which embeds whole attachments and can be many MB.
    Json meta;
    meta["title"] = title;
    meta["updated_at"] = updatedAt;
    std::ofstream metaOut(dir / (id + ".meta"), std::ios::trunc);
    if (metaOut) {
        metaOut << meta.dump();
    }
}

bool ConversationStore::load(const std::string& id, std::vector<ChatMessage>& outMessages) const {
    std::ifstream in(conversationsDir() / (id + ".json"));
    if (!in) return false;

    Json root = Json::parse(in, nullptr, false);
    if (!root.is_object() || !root.contains("messages") || !root["messages"].is_array()) {
        return false;
    }

    outMessages.clear();
    for (const Json& node : root["messages"]) {
        if (!node.is_object()) {
            continue; // tolerate hand-edited or corrupted entries
        }
        outMessages.push_back(messageFromJson(node));
    }
    return true;
}

std::vector<ConversationMeta> ConversationStore::list() const {
    std::vector<ConversationMeta> result;
    fs::path dir = conversationsDir();
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        return result;
    }

    for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".json") {
            continue;
        }

        ConversationMeta meta;
        // The filename is what load() opens, so it is the id's ground truth
        // even if the embedded "id" field disagrees (copied/renamed files).
        meta.id = it->path().stem().string();

        // Prefer the metadata sidecar: the conversation body embeds whole
        // attachments, so parsing it here would load every blob just to read
        // two fields. Legacy files without a sidecar fall back to the full
        // parse (the next save writes the sidecar).
        fs::path metaFile = it->path();
        metaFile.replace_extension(".meta");
        Json root;
        {
            std::ifstream metaIn(metaFile);
            if (metaIn) {
                root = Json::parse(metaIn, nullptr, false);
            }
        }
        if (!root.is_object()) {
            std::ifstream in(it->path());
            if (!in) continue;
            root = Json::parse(in, nullptr, false);
            if (!root.is_object()) continue;
        }

        meta.title = root.value("title", std::string("Conversation"));
        meta.updatedAt = root.value("updated_at", static_cast<int64_t>(0));
        result.push_back(std::move(meta));
    }

    std::sort(result.begin(), result.end(), [](const ConversationMeta& a, const ConversationMeta& b) {
        if (a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt;
        return a.id > b.id; // stable order for equal timestamps
    });
    return result;
}

void ConversationStore::remove(const std::string& id) {
    if (id.empty()) return;
    std::error_code ec;
    fs::remove(conversationsDir() / (id + ".json"), ec);
    fs::remove(conversationsDir() / (id + ".meta"), ec);
}

} // namespace doriax::editor::ai
