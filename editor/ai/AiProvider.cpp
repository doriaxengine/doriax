// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AiProvider.h"

#include "HttpClient.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <limits>

namespace doriax::editor::ai {

// Payload builders / response parsers, implemented at the bottom of this file.
// OpenAI, DeepSeek and OpenAI-compatible share the Chat Completions wire format.
Json buildChatCompletionsPayload(const ProviderRequest& request);
Json buildAnthropicPayload(const ProviderRequest& request);
Json buildGeminiPayload(const ProviderRequest& request);
ProviderResponse parseChatCompletionsBody(const std::string& body);
ProviderResponse parseAnthropicBody(const std::string& body);
ProviderResponse parseGeminiBody(const std::string& body);

// Strips JSON Schema keywords Gemini's function-declaration schema rejects (e.g.
// additionalProperties). Returns a null Json for an empty object schema so the
// caller can omit the parameters field entirely.
Json sanitizeGeminiSchema(const Json& schema);

namespace {

Json toolParametersOrObject(const ToolDefinition& tool) {
    return tool.parameters.is_object() ? tool.parameters : Json::object();
}

// Tool-call arguments are carried as a JSON object internally but the Chat
// Completions wire format expects them as a serialized string.
std::string argumentsToString(const Json& args) {
    if (args.is_string()) return args.get<std::string>();
    if (args.is_null()) return "{}";
    return args.dump();
}

bool needsChatCompletionsToolReasoningWorkaround(const ProviderRequest& request) {
    if (request.tools.empty()) {
        return false;
    }

    // GPT-5.6 and newer models support function tools with reasoning through
    // /v1/responses, but not through /v1/chat/completions. Doriax's OpenAI
    // adapters currently use Chat Completions, so explicitly select the
    // supported non-reasoning mode. Parse the numeric version so future
    // families such as 5.7, 5.10 and 6.x are covered without affecting older
    // GPT models. Provider-qualified ids such as "openai/gpt-5.6-terra" are
    // accepted by inspecting the final path segment.
    const std::string& model = request.settings.model;
    const size_t separator = model.rfind('/');
    const size_t nameStart = separator == std::string::npos ? 0 : separator + 1;
    constexpr char prefix[] = "gpt-";
    constexpr size_t prefixLength = sizeof(prefix) - 1;
    if (model.size() < nameStart + prefixLength ||
        model.compare(nameStart, prefixLength, prefix) != 0) {
        return false;
    }

    const char* end = model.data() + model.size();
    const char* version = model.data() + nameStart + prefixLength;
    int major = 0;
    auto [majorEnd, majorError] = std::from_chars(version, end, major);
    if (majorError != std::errc() || majorEnd == version) {
        return false;
    }

    int minor = 0;
    const char* suffix = majorEnd;
    if (suffix < end && *suffix == '.') {
        const char* minorStart = ++suffix;
        auto [minorEnd, minorError] = std::from_chars(minorStart, end, minor);
        if (minorError != std::errc() || minorEnd == minorStart) {
            return false;
        }
        suffix = minorEnd;
    }
    if (suffix < end && *suffix != '-') {
        return false;
    }

    return major > 5 || (major == 5 && minor >= 6);
}

// Also matches gateway ids such as "deepseek/deepseek-v4-pro".
bool modelContains(const ProviderRequest& request, const char* name) {
    std::string model = request.settings.model;
    std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return model.find(name) != std::string::npos;
}

std::string attachmentText(const ChatAttachment& attachment) {
    // Name and body are user-controlled; keep them from breaking out of the
    // wrapper. Quotes/angle brackets and control chars are stripped from the
    // name, and a literal closing tag inside the body is defused so file
    // content can't be read as prompt structure.
    std::string name = attachment.name;
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) {
        return c == '"' || c == '<' || c == '>' || c < 0x20;
    }), name.end());
    std::string body = attachment.data;
    size_t pos = 0;
    while ((pos = body.find("</attachment>", pos)) != std::string::npos) {
        body.insert(pos + 1, "\\");
        pos += 2;
    }
    return "<attachment name=\"" + name + "\">\n" + body + "\n</attachment>";
}

Json chatCompletionsUserContent(const ChatMessage& message, bool supportsImages) {
    if (message.attachments.empty()) {
        return message.content;
    }

    if (!supportsImages) {
        std::string content = message.content;
        for (const ChatAttachment& attachment : message.attachments) {
            if (!content.empty()) content += "\n\n";
            content += attachment.isImage()
                ? "[Image attachment unavailable to this provider: " + attachment.name + "]"
                : attachmentText(attachment);
        }
        return content;
    }

    Json content = Json::array();
    if (!message.content.empty()) {
        content.push_back({{"type", "text"}, {"text", message.content}});
    }
    for (const ChatAttachment& attachment : message.attachments) {
        if (attachment.isImage()) {
            content.push_back({
                {"type", "image_url"},
                {"image_url", {
                    {"url", "data:" + attachment.mimeType + ";base64," + attachment.data},
                    {"detail", "auto"}
                }}
            });
        } else {
            content.push_back({{"type", "text"}, {"text", attachmentText(attachment)}});
        }
    }
    return content;
}

void appendGeminiUserParts(Json& parts, const ChatMessage& message) {
    if (!message.content.empty()) {
        parts.push_back({{"text", message.content}});
    }
    for (const ChatAttachment& attachment : message.attachments) {
        if (attachment.isImage()) {
            parts.push_back({{"inlineData", {
                {"mimeType", attachment.mimeType},
                {"data", attachment.data}
            }}});
        } else {
            parts.push_back({{"text", attachmentText(attachment)}});
        }
    }
}

Json parseArgumentObject(const Json& args) {
    if (args.is_object()) return args;
    if (args.is_string()) {
        try {
            Json parsed = Json::parse(args.get<std::string>());
            return parsed.is_object() ? parsed : Json::object();
        } catch (...) {
            return Json::object();
        }
    }
    return Json::object();
}

ProviderResponse parseErrorOrBody(const std::string& body, Json& root) {
    ProviderResponse result;
    try {
        root = Json::parse(body);
    } catch (const std::exception& e) {
        result.error = std::string("Invalid JSON response: ") + e.what();
        return result;
    }

    if (root.contains("error")) {
        const Json& error = root["error"];
        if (error.is_string()) {
            result.error = error.get<std::string>();
        } else if (error.is_object()) {
            if (error.contains("message") && error["message"].is_string()) {
                result.error = error["message"].get<std::string>();
            }
            if (error.contains("code")) {
                if (error["code"].is_string()) {
                    result.errorCode = error["code"].get<std::string>();
                } else if (error["code"].is_number_integer() ||
                           error["code"].is_number_unsigned()) {
                    result.errorCode = error["code"].dump();
                }
            }
            if (error.contains("type") && error["type"].is_string()) {
                result.errorType = error["type"].get<std::string>();
            }
            if (error.contains("status") && error["status"].is_string()) {
                result.errorStatus = error["status"].get<std::string>();
            }
            if (error.contains("details") && error["details"].is_array()) {
                for (const Json& detail : error["details"]) {
                    if (!detail.is_object()) {
                        continue;
                    }
                    const auto typeIt = detail.find("@type");
                    if (typeIt == detail.end() || !typeIt->is_string()) {
                        continue;
                    }
                    const std::string detailType =
                        typeIt->get<std::string>();
                    if (detailType.find("google.rpc.RetryInfo") ==
                        std::string::npos) {
                        continue;
                    }
                    const Json* retryDelay = nullptr;
                    if (detail.contains("retryDelay")) {
                        retryDelay = &detail["retryDelay"];
                    } else if (detail.contains("retry_delay")) {
                        retryDelay = &detail["retry_delay"];
                    }
                    if (retryDelay && retryDelay->is_string()) {
                        result.retryDelay =
                            retryDelay->get<std::string>();
                        break;
                    }
                }
            }
        }
    }
    return result;
}

int readTokenCount(const Json& object, const char* key) {
    if (!object.is_object() || !object.contains(key)) {
        return -1;
    }

    const Json& value = object[key];
    if (value.is_number_unsigned()) {
        uint64_t count = value.get<uint64_t>();
        if (count <= static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return static_cast<int>(count);
        }
    } else if (value.is_number_integer()) {
        int64_t count = value.get<int64_t>();
        if (count >= 0 && count <= std::numeric_limits<int>::max()) {
            return static_cast<int>(count);
        }
    }
    return -1;
}

void parseOpenAiUsage(const Json& root, ProviderResponse& result) {
    const Json& usage = root.value("usage", Json::object());
    result.promptTokens = readTokenCount(usage, "prompt_tokens");
    result.completionTokens = readTokenCount(usage, "completion_tokens");
    result.totalTokens = readTokenCount(usage, "total_tokens");
}

void parseAnthropicUsage(const Json& root, ProviderResponse& result) {
    const Json& usage = root.value("usage", Json::object());
    result.promptTokens = readTokenCount(usage, "input_tokens");
    result.completionTokens = readTokenCount(usage, "output_tokens");
    if (result.promptTokens >= 0 && result.completionTokens >= 0) {
        result.totalTokens = result.promptTokens + result.completionTokens;
    }
}

void parseGeminiUsage(const Json& root, ProviderResponse& result) {
    const Json& usage = root.value("usageMetadata", Json::object());
    result.promptTokens = readTokenCount(usage, "promptTokenCount");
    result.completionTokens = readTokenCount(usage, "candidatesTokenCount");
    result.totalTokens = readTokenCount(usage, "totalTokenCount");
}

// --- Provider adapters ----------------------------------------------------

class OpenAIProvider : public Provider {
public:
    ProviderId id() const override { return ProviderId::OpenAI; }

    HttpRequest buildRequest(const ProviderRequest& request) const override {
        HttpRequest http;
        http.url = "https://api.openai.com/v1/chat/completions";
        http.headers = {
            "Content-Type: application/json",
            "Authorization: Bearer " + request.apiKey
        };
        http.body = buildChatCompletionsPayload(request).dump();
        return http;
    }

    ProviderResponse parseResponse(const std::string& body) const override {
        return parseChatCompletionsBody(body);
    }
};

class OpenAICompatibleProvider : public Provider {
public:
    ProviderId id() const override { return ProviderId::OpenAICompatible; }

    HttpRequest buildRequest(const ProviderRequest& request) const override {
        HttpRequest http;
        http.url = activeEndpointUrl(request.settings);
        http.headers = {"Content-Type: application/json"};
        // A local server may take no credentials; an empty bearer gets rejected.
        if (!request.apiKey.empty()) {
            http.headers.push_back("Authorization: Bearer " + request.apiKey);
        }
        // Attribution headers only OpenRouter reads.
        if (http.url.find("openrouter.ai") != std::string::npos) {
            http.headers.push_back("HTTP-Referer: https://doriaxengine.org");
            http.headers.push_back("X-Title: Doriax Editor");
        }
        http.body = buildChatCompletionsPayload(request).dump();
        return http;
    }

    ProviderResponse parseResponse(const std::string& body) const override {
        return parseChatCompletionsBody(body);
    }
};

class DeepSeekProvider : public Provider {
public:
    ProviderId id() const override { return ProviderId::DeepSeek; }

    HttpRequest buildRequest(const ProviderRequest& request) const override {
        HttpRequest http;
        http.url = "https://api.deepseek.com/chat/completions";
        http.headers = {
            "Content-Type: application/json",
            "Authorization: Bearer " + request.apiKey
        };
        http.body = buildChatCompletionsPayload(request).dump();
        return http;
    }

    ProviderResponse parseResponse(const std::string& body) const override {
        return parseChatCompletionsBody(body);
    }
};

class AnthropicProvider : public Provider {
public:
    ProviderId id() const override { return ProviderId::Anthropic; }

    HttpRequest buildRequest(const ProviderRequest& request) const override {
        HttpRequest http;
        http.url = "https://api.anthropic.com/v1/messages";
        http.headers = {
            "Content-Type: application/json",
            "x-api-key: " + request.apiKey,
            "anthropic-version: 2023-06-01"
        };
        http.body = buildAnthropicPayload(request).dump();
        return http;
    }

    ProviderResponse parseResponse(const std::string& body) const override {
        return parseAnthropicBody(body);
    }
};

class GeminiProvider : public Provider {
public:
    ProviderId id() const override { return ProviderId::Gemini; }

    HttpRequest buildRequest(const ProviderRequest& request) const override {
        HttpRequest http;
        http.url = "https://generativelanguage.googleapis.com/v1beta/models/" +
                   request.settings.model + ":generateContent?key=" +
                   HttpClient::urlEncode(request.apiKey);
        http.headers = {"Content-Type: application/json"};
        http.body = buildGeminiPayload(request).dump();
        return http;
    }

    ProviderResponse parseResponse(const std::string& body) const override {
        return parseGeminiBody(body);
    }
};

} // namespace

std::unique_ptr<Provider> createProvider(ProviderId provider) {
    switch (provider) {
        case ProviderId::Anthropic: return std::make_unique<AnthropicProvider>();
        case ProviderId::Gemini: return std::make_unique<GeminiProvider>();
        case ProviderId::DeepSeek: return std::make_unique<DeepSeekProvider>();
        case ProviderId::OpenAICompatible: return std::make_unique<OpenAICompatibleProvider>();
        case ProviderId::OpenAI:
        default: return std::make_unique<OpenAIProvider>();
    }
}

// --- Chat Completions (OpenAI + OpenAI-compatible) ------------------------

Json buildChatCompletionsPayload(const ProviderRequest& request) {
    Json root;
    root["model"] = request.settings.model;
    if (needsChatCompletionsToolReasoningWorkaround(request)) {
        root["reasoning_effort"] = "none";
    }
    if (request.settings.provider == ProviderId::OpenAI) {
        // OpenAI deprecated max_tokens; GPT-5+ and o-series reject it outright,
        // while max_completion_tokens works on every current OpenAI chat model.
        // Compatible endpoints (Ollama, LM Studio, vLLM) and DeepSeek still
        // expect max_tokens.
        root["max_completion_tokens"] = request.settings.maxOutputTokens;
    } else {
        root["max_tokens"] = request.settings.maxOutputTokens;
    }

    // DeepSeek rejects an assistant turn without reasoning_content once tools are
    // sent, also when reached through OpenRouter or another compatible endpoint
    const bool needsReasoning = request.settings.provider == ProviderId::DeepSeek ||
                                modelContains(request, "deepseek");
    const bool gemini = modelContains(request, "gemini");

    Json messages = Json::array();
    messages.push_back({{"role", "system"}, {"content", request.systemPrompt}});

    for (const auto& message : request.messages) {
        switch (message.role) {
            case ChatRole::System:
                break;
            case ChatRole::User:
                messages.push_back({
                    {"role", "user"},
                    {"content", chatCompletionsUserContent(
                        message, providerSupportsImages(request.settings.provider))}
                });
                break;
            case ChatRole::Assistant: {
                // Reasoning only goes back to the model that wrote it; other
                // endpoints may reject the unknown fields
                const bool ownReasoning = message.model == request.settings.model;
                Json item;
                item["role"] = "assistant";
                if (message.toolCalls.empty()) {
                    item["content"] = message.content;
                } else {
                    // content may be empty when the turn is purely tool calls.
                    item["content"] = message.content.empty() ? Json(nullptr) : Json(message.content);
                    Json toolCalls = Json::array();
                    bool firstCall = true;
                    for (const auto& call : message.toolCalls) {
                        Json toolCall = {
                            {"id", call.id},
                            {"type", "function"},
                            {"function", {
                                {"name", call.name},
                                {"arguments", argumentsToString(call.arguments)}
                            }}
                        };
                        std::string signature = ownReasoning ? call.thoughtSignature : "";
                        // Gemini 3 wants a signature on each step's first call; Google
                        // documents this bypass for steps another model made
                        if (!ownReasoning && gemini && firstCall) {
                            signature = "skip_thought_signature_validator";
                        }
                        if (!signature.empty()) {
                            toolCall["extra_content"] = {
                                {"google", {{"thought_signature", signature}}}
                            };
                        }
                        toolCalls.push_back(toolCall);
                        firstCall = false;
                    }
                    item["tool_calls"] = toolCalls;
                }
                if (ownReasoning) item.update(message.reasoningFields);
                // Stands in for turns without reasoning, like editor notes
                if (needsReasoning && !item.contains("reasoning_content") &&
                    !item.contains("reasoning_details")) {
                    item["reasoning_content"] = ".";
                }
                messages.push_back(item);
                break;
            }
            case ChatRole::Tool:
                messages.push_back({
                    {"role", "tool"},
                    {"tool_call_id", message.toolCallId},
                    {"content", message.content}
                });
                break;
        }
    }
    root["messages"] = messages;

    if (!request.tools.empty()) {
        Json tools = Json::array();
        for (const auto& tool : request.tools) {
            tools.push_back({
                {"type", "function"},
                {"function", {
                    {"name", tool.name},
                    {"description", tool.description},
                    {"parameters", toolParametersOrObject(tool)}
                }}
            });
        }
        root["tools"] = tools;
        root["tool_choice"] = "auto";
    }
    return root;
}

ProviderResponse parseChatCompletionsBody(const std::string& body) {
    Json root;
    ProviderResponse result = parseErrorOrBody(body, root);
    if (!result.error.empty()) return result;

    if (!root.contains("choices") || !root["choices"].is_array() || root["choices"].empty()) {
        return result;
    }

    parseOpenAiUsage(root, result);
    result.stopReason = root["choices"][0].value("finish_reason", "");
    result.truncated = result.stopReason == "length";

    const Json& message = root["choices"][0].value("message", Json::object());
    if (message.contains("content") && message["content"].is_string()) {
        result.text = message["content"].get<std::string>();
    }
    // DeepSeek and OpenRouter reasoning models want these back on later requests
    for (const char* key : {"reasoning_content", "reasoning_details"}) {
        const auto it = message.find(key);
        if (it != message.end() && !it->empty() && *it != "") result.reasoningFields[key] = *it;
    }

    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
        for (const auto& item : message["tool_calls"]) {
            const Json& fn = item.value("function", Json::object());
            ToolCall call;
            call.id = item.value("id", "");
            call.name = fn.value("name", "");
            call.arguments = parseArgumentObject(fn.value("arguments", Json::object()));
            call.thoughtSignature = item.value("/extra_content/google/thought_signature"_json_pointer, "");
            if (!call.name.empty()) result.toolCalls.push_back(call);
        }
    }
    return result;
}

// --- Anthropic ------------------------------------------------------------

Json buildAnthropicPayload(const ProviderRequest& request) {
    Json root;
    root["model"] = request.settings.model;
    root["system"] = request.systemPrompt;
    root["max_tokens"] = request.settings.maxOutputTokens;

    Json messages = Json::array();
    for (size_t i = 0; i < request.messages.size();) {
        const ChatMessage& message = request.messages[i];

        if (message.role == ChatRole::Tool) {
            // Merge a run of tool results into a single user message; Anthropic
            // requires roles to alternate.
            Json content = Json::array();
            while (i < request.messages.size() && request.messages[i].role == ChatRole::Tool) {
                const ChatMessage& tool = request.messages[i];
                Json block = {
                    {"type", "tool_result"},
                    {"tool_use_id", tool.toolCallId},
                    {"content", tool.content}
                };
                if (!tool.toolSuccess) block["is_error"] = true;
                content.push_back(block);
                ++i;
            }
            messages.push_back({{"role", "user"}, {"content", content}});
            continue;
        }

        if (message.role == ChatRole::User) {
            Json content = Json::array();
            for (const ChatAttachment& attachment : message.attachments) {
                if (attachment.isImage()) {
                    content.push_back({
                        {"type", "image"},
                        {"source", {
                            {"type", "base64"},
                            {"media_type", attachment.mimeType},
                            {"data", attachment.data}
                        }}
                    });
                } else {
                    content.push_back({{"type", "text"},
                                       {"text", attachmentText(attachment)}});
                }
            }
            if (!message.content.empty()) {
                content.push_back({{"type", "text"}, {"text", message.content}});
            }
            // Anthropic rejects an empty content array; a user turn can end up
            // empty when a loaded conversation dropped its only attachment.
            if (content.empty()) {
                content.push_back({{"type", "text"}, {"text", "(empty message)"}});
            }
            messages.push_back({{"role", "user"}, {"content", content}});
        } else if (message.role == ChatRole::Assistant && !message.anthropicContent.empty()) {
            // Rebuilding the turn would break its thinking signatures
            messages.push_back({{"role", "assistant"}, {"content", message.anthropicContent}});
        } else if (message.role == ChatRole::Assistant) {
            Json content = Json::array();
            if (!message.content.empty()) {
                content.push_back({{"type", "text"}, {"text", message.content}});
            }
            for (const auto& call : message.toolCalls) {
                content.push_back({
                    {"type", "tool_use"},
                    {"id", call.id},
                    {"name", call.name},
                    {"input", call.arguments.is_object() ? call.arguments : Json::object()}
                });
            }
            if (!content.empty()) {
                messages.push_back({{"role", "assistant"}, {"content", content}});
            }
        }
        ++i;
    }
    root["messages"] = messages;

    if (!request.tools.empty()) {
        Json tools = Json::array();
        for (const auto& tool : request.tools) {
            tools.push_back({
                {"name", tool.name},
                {"description", tool.description},
                {"input_schema", toolParametersOrObject(tool)}
            });
        }
        root["tools"] = tools;
    }
    return root;
}

ProviderResponse parseAnthropicBody(const std::string& body) {
    Json root;
    ProviderResponse result = parseErrorOrBody(body, root);
    if (!result.error.empty()) return result;

    parseAnthropicUsage(root, result);
    result.stopReason = root.value("stop_reason", "");
    result.truncated = result.stopReason == "max_tokens";

    if (!root.contains("content") || !root["content"].is_array()) {
        return result;
    }
    for (const auto& part : root["content"]) {
        const std::string type = part.value("type", "");
        if (type == "text" && part.contains("text") && part["text"].is_string()) {
            const std::string text = part["text"].get<std::string>();
            // Blank text blocks are rejected when sent back
            if (text.find_first_not_of(" \t\r\n") == std::string::npos) continue;
            if (!result.text.empty()) result.text += "\n";
            result.text += text;
        } else if (type == "tool_use") {
            ToolCall call;
            call.id = part.value("id", "");
            call.name = part.value("name", "");
            call.arguments = parseArgumentObject(part.value("input", Json::object()));
            if (!call.name.empty()) result.toolCalls.push_back(call);
        }
        result.anthropicContent.push_back(part);
    }
    return result;
}

// --- Gemini ---------------------------------------------------------------

Json sanitizeGeminiSchema(const Json& schema) {
    if (!schema.is_object()) {
        return schema;
    }

    Json out = Json::object();
    for (auto it = schema.begin(); it != schema.end(); ++it) {
        const std::string& key = it.key();
        // Keywords Gemini's OpenAPI subset rejects outright.
        if (key == "additionalProperties" || key == "$schema" ||
            key == "additionalItems" || key == "$ref" ||
            key == "$defs" || key == "definitions") {
            continue;
        }
        // Drop empty "required" arrays; Gemini treats required as optional.
        if (key == "required" && it->is_array() && it->empty()) {
            continue;
        }
        if (key == "properties" && it->is_object()) {
            Json props = Json::object();
            for (auto p = it->begin(); p != it->end(); ++p) {
                Json child = sanitizeGeminiSchema(p.value());
                if (!child.is_null()) props[p.key()] = child;
            }
            out["properties"] = props;
        } else if (key == "items") {
            out["items"] = sanitizeGeminiSchema(it.value());
        } else {
            out[key] = it.value();
        }
    }

    // An OBJECT schema with no properties confuses Gemini; signal omission.
    if (out.value("type", "") == "object") {
        auto props = out.find("properties");
        if (props == out.end() || (props->is_object() && props->empty())) {
            return Json(nullptr);
        }
    }
    return out;
}

Json buildGeminiPayload(const ProviderRequest& request) {
    Json root;
    root["system_instruction"] = {{"parts", Json::array({{{"text", request.systemPrompt}}})}};

    Json contents = Json::array();
    for (size_t i = 0; i < request.messages.size();) {
        const ChatMessage& message = request.messages[i];

        if (message.role == ChatRole::Tool) {
            // Merge a run of tool results into a single user turn. A user
            // message right after the run (e.g. tool calls dismissed because
            // the user typed something new) must ride in the SAME turn as a
            // text part: Gemini expects function responses in the user turn
            // immediately following the model's function call, and two
            // consecutive user contents break role alternation.
            Json parts = Json::array();
            while (i < request.messages.size() && request.messages[i].role == ChatRole::Tool) {
                const ChatMessage& tool = request.messages[i];
                Json response = {
                    {"name", tool.toolName},
                    {"response", {{"result", tool.content}}}
                };
                // Echo the model-issued call id; it is what disambiguates
                // parallel calls to the same function. Legacy history stored
                // the function name as the id (and an id Gemini never issued
                // must not be sent back), so only a distinct id is a real one.
                if (!tool.toolCallId.empty() && tool.toolCallId != tool.toolName) {
                    response["id"] = tool.toolCallId;
                }
                parts.push_back({{"functionResponse", response}});
                ++i;
            }
            while (i < request.messages.size() && request.messages[i].role == ChatRole::User) {
                appendGeminiUserParts(parts, request.messages[i]);
                ++i;
            }
            contents.push_back({{"role", "user"}, {"parts", parts}});
            continue;
        }

        if (message.role == ChatRole::User) {
            Json parts = Json::array();
            appendGeminiUserParts(parts, message);
            // Gemini rejects an empty parts array; a user turn can end up empty
            // when a loaded conversation dropped its only attachment.
            if (parts.empty()) {
                parts.push_back({{"text", "(empty message)"}});
            }
            contents.push_back({{"role", "user"}, {"parts", parts}});
        } else if (message.role == ChatRole::Assistant) {
            Json parts = Json::array();
            if (!message.content.empty()) {
                parts.push_back({{"text", message.content}});
            }
            bool firstFunctionCall = true;
            for (const auto& call : message.toolCalls) {
                Json functionCall = {
                    {"name", call.name},
                    {"args", call.arguments.is_object() ? call.arguments : Json::object()}
                };
                // Mirror a real model-issued id on the replayed functionCall;
                // the matching functionResponse below must reference an id
                // that is present on this call. Older saved Gemini history
                // used the function name as a synthetic id, so do not echo it.
                if (!call.id.empty() && call.id != call.name) {
                    functionCall["id"] = call.id;
                }
                Json part = {{"functionCall", functionCall}};
                // Gemini 3 requires thoughtSignature on the first functionCall of
                // each model step. Echo the opaque token when we have it; otherwise
                // use the documented bypass so older history does not 400.
                std::string signature = call.thoughtSignature;
                if (signature.empty() && firstFunctionCall) {
                    signature = "skip_thought_signature_validator";
                }
                if (!signature.empty()) {
                    part["thoughtSignature"] = signature;
                }
                parts.push_back(part);
                firstFunctionCall = false;
            }
            if (!parts.empty()) {
                contents.push_back({{"role", "model"}, {"parts", parts}});
            }
        }
        ++i;
    }
    root["contents"] = contents;

    if (!request.tools.empty()) {
        Json declarations = Json::array();
        for (const auto& tool : request.tools) {
            Json decl = {{"name", tool.name}, {"description", tool.description}};
            Json params = sanitizeGeminiSchema(toolParametersOrObject(tool));
            if (!params.is_null()) decl["parameters"] = params;
            declarations.push_back(decl);
        }
        root["tools"] = Json::array({{{"function_declarations", declarations}}});
    }

    root["generationConfig"] = {{"maxOutputTokens", request.settings.maxOutputTokens}};
    return root;
}

ProviderResponse parseGeminiBody(const std::string& body) {
    Json root;
    ProviderResponse result = parseErrorOrBody(body, root);
    if (!result.error.empty()) return result;

    if (!root.contains("candidates") || !root["candidates"].is_array() || root["candidates"].empty()) {
        return result;
    }
    parseGeminiUsage(root, result);
    result.stopReason = root["candidates"][0].value("finishReason", "");
    result.truncated = result.stopReason == "MAX_TOKENS";

    const Json& parts = root["candidates"][0].value("content", Json::object()).value("parts", Json::array());
    if (!parts.is_array()) {
        return result;
    }
    for (const auto& part : parts) {
        if (part.contains("text") && part["text"].is_string()) {
            if (!result.text.empty()) result.text += "\n";
            result.text += part["text"].get<std::string>();
        }
        if (part.contains("functionCall")) {
            const Json& fn = part["functionCall"];
            ToolCall call;
            // Keep the model-issued call id (required back in the matching
            // functionResponse, and the only thing that tells parallel calls
            // to the same function apart). Older models omit it; fall back to
            // the name, which the serializer knows not to echo as an id.
            call.id = fn.value("id", "");
            call.name = fn.value("name", "");
            if (call.id.empty()) {
                call.id = call.name;
            }
            call.arguments = parseArgumentObject(fn.value("args", Json::object()));
            // Sibling of functionCall on the Part (REST camelCase).
            call.thoughtSignature = part.value("thoughtSignature",
                part.value("thought_signature", ""));
            if (!call.name.empty()) result.toolCalls.push_back(call);
        }
    }
    return result;
}

} // namespace doriax::editor::ai
