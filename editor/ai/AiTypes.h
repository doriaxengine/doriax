// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include "json.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace doriax::editor::ai {

using Json = nlohmann::json;

enum class ProviderId {
    OpenAI,
    Anthropic,
    Gemini,
    DeepSeek,
    OpenAICompatible
};

// A user-named OpenAI-compatible endpoint (OpenCode Zen, OpenRouter, Ollama).
struct CustomEndpoint {
    std::string id; // stable slug; renaming the label keeps the stored key
    std::string label;
    std::string url;
};

// A built-in provider or one custom endpoint. The key store and model catalog
// are keyed by id, so endpoints never share a key or a model list.
struct ProviderAccount {
    ProviderId provider = ProviderId::OpenAI;
    std::string endpointId;
    std::string label;
    std::string id;
    std::string url;
};

struct EndpointPreset {
    std::string id;
    std::string label;
    std::string url;
};

enum class ChatRole {
    System,
    User,
    Assistant,
    Tool
};

enum class ApprovalMode {
    PreviewThenApprove,
    SafeAutoRun,
    FullAgent
};

struct Settings {
    ProviderId provider = ProviderId::OpenAI;
    std::string model = "gpt-5.6-terra";
    // Which custom endpoint is selected when provider == OpenAICompatible.
    std::string endpointId;
    std::vector<CustomEndpoint> customEndpoints;
    ApprovalMode approvalMode = ApprovalMode::PreviewThenApprove;
    int requestTimeoutSeconds = 90;
    // Large enough to write a whole file (scripts, forked shaders) in one tool call; a low
    // cap truncates the call mid-JSON and the partial call is dropped (looks like an empty
    // reply). Providers bill generated output, but may reserve this value for TPM rate-limit
    // accounting, so keep it close to the largest response the editor actually needs.
    int maxOutputTokens = 8192;
    // One "round" is a model turn that requests tools (results are fed back on the next).
    // Script tasks that ground in engine source (inspect + several searches + write + verify)
    // need enough headroom to finish in one user message.
    int maxToolRounds = 24;
};

struct McpSettings {
    bool enabled = false;
    int port = 3674;
    // Off: agents only get the read-only tools
    bool allowChanges = true;
};

struct ToolCall {
    std::string id;
    std::string name;
    Json arguments = Json::object();
    // Opaque Gemini thought signature (REST: thoughtSignature, OpenAI-compatible:
    // extra_content.google.thought_signature). Required on the first functionCall
    // part of each model step for Gemini 3 / thinking models; must be echoed
    // verbatim on the next request.
    std::string thoughtSignature;
};

struct ChatAttachment {
    std::string name;
    std::string mimeType;
    // Images are stored as base64; text attachments are stored as UTF-8.
    std::string data;

    // Derived from the mime type so persisted data can't disagree with it.
    bool isImage() const { return mimeType.rfind("image/", 0) == 0; }
};

// The only attachment mime types the editor produces and providers accept.
// Enforced at the composer AND when loading persisted conversations, so no
// other value ever reaches a provider payload.
inline bool isSupportedImageMime(const std::string& mimeType) {
    return mimeType == "image/png" || mimeType == "image/jpeg" ||
           mimeType == "image/gif" || mimeType == "image/webp";
}

// Single source of truth for which providers can receive image content parts;
// the UI send-gate and the payload builders must agree. OpenAICompatible is
// assumed vision-capable because the model behind a custom endpoint is
// unknowable here; a text-only model answers with the provider's own error.
inline bool providerSupportsImages(ProviderId provider) {
    return provider != ProviderId::DeepSeek;
}

struct ChatMessage {
    ChatRole role = ChatRole::User;
    std::string content;

    // Model id active when a user message was sent, or that wrote an assistant
    // message. The transcript shows a divider when it changes between user messages.
    std::string model;

    // Inline user-provided files. Kept in history so multimodal context remains
    // available during tool continuations and after reloading a conversation.
    std::vector<ChatAttachment> attachments;

    // Set on assistant messages that requested one or more tool calls. Kept in the
    // history so follow-up requests can present a well-formed tool round-trip.
    std::vector<ToolCall> toolCalls;

    // Anthropic content blocks as returned, sent back unchanged.
    std::vector<Json> anthropicContent;

    // Chat Completions reasoning_content / reasoning_details, sent back to the
    // model that wrote them.
    Json reasoningFields = Json::object();

    // Set on tool-result messages (role == Tool).
    std::string toolCallId;
    std::string toolName;
    // Human-readable label for the transcript (from EditorActionRegistry::describe).
    std::string toolDescription;
    bool toolSuccess = true;
};

struct ToolDefinition {
    std::string name;
    std::string description;
    Json parameters = Json::object();
    bool readOnly = true;
    // Kept from the editor chat, since a project switch would end its turn
    bool mcpOnly = false;
};

struct ProviderRequest {
    Settings settings;
    std::string apiKey;
    std::vector<ChatMessage> messages;
    std::vector<ToolDefinition> tools;
    std::string systemPrompt;
};

struct ProviderResponse {
    std::string text;
    std::vector<ToolCall> toolCalls;
    // Anthropic content blocks from the model turn.
    std::vector<Json> anthropicContent;
    // Chat Completions reasoning fields from the model turn.
    Json reasoningFields = Json::object();
    std::string error;
    // Provider error metadata (for example OpenAI's "insufficient_quota" or
    // Gemini's numeric code plus "RESOURCE_EXHAUSTED" status). Keeping these
    // separate from the message lets the service classify HTTP 429 responses.
    std::string errorCode;
    std::string errorType;
    std::string errorStatus;
    // google.rpc.RetryInfo.retryDelay returned in error.details, usually a
    // protobuf JSON duration such as "21s" or "1.045846877s".
    std::string retryDelay;
    // Provider stop reason such as finish_reason "length", stop_reason "max_tokens",
    // or Gemini finishReason "MAX_TOKENS".
    std::string stopReason;
    // Token usage when returned by the provider. A value of -1 means unknown.
    int promptTokens = -1;
    int completionTokens = -1;
    int totalTokens = -1;
    // True when the model stopped because it hit a token limit; the reply is likely incomplete.
    bool truncated = false;
};

struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::string> headers;
    std::string body;
    long timeoutSeconds = 90;
};

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;
    // Response headers are retained so callers can honor Retry-After and
    // provider-specific rate-limit reset hints without parsing body text.
    std::vector<std::pair<std::string, std::string>> headers;
};

enum class ActionFailureSeverity {
    // An exploratory lookup did not find the requested scene/entity/component.
    // The model should see the miss, but it is not an editor diagnostic.
    ExpectedMiss,
    // The request was invalid or unsupported, without indicating an editor or
    // project failure.
    Warning,
    // The editor, project, filesystem, or external dependency failed.
    Error
};

struct ActionResult {
    bool success = false;
    std::string message;
    Json data = Json::object();
    ActionFailureSeverity failureSeverity = ActionFailureSeverity::Error;
};

struct ActionProposal {
    uint64_t id = 0;
    std::string toolCallId;
    std::string toolName;
    Json arguments = Json::object();
    std::string description;
    bool readOnly = true;
    bool executed = false;
    bool executing = false;
    ActionResult result;
};

std::string toString(ProviderId provider);
std::string providerLabel(ProviderId provider);
ProviderId providerFromString(const std::string& value);
std::string defaultModelForProvider(ProviderId provider);

// A custom endpoint is usable on its URL alone: a local server needs no key.
inline bool accountRequiresApiKey(ProviderId provider) {
    return provider != ProviderId::OpenAICompatible;
}

// Built-in providers keep their bare name so existing key files keep working;
// custom endpoints append their id.
std::string accountKey(ProviderId provider, const std::string& endpointId);
std::string accountKey(const Settings& settings);

const CustomEndpoint* findEndpoint(const Settings& settings, const std::string& endpointId);
std::string activeEndpointUrl(const Settings& settings);

// Built-in providers, then custom endpoints.
std::vector<ProviderAccount> listAccounts(const Settings& settings);

// Presets offered by the "Add endpoint" menu.
const std::vector<EndpointPreset>& endpointPresets();
// Slugifies a label into an id unique among the existing endpoints.
std::string makeEndpointId(const Settings& settings, const std::string& label);

std::string toString(ChatRole role);
ChatRole chatRoleFromString(const std::string& value);

std::string toString(ApprovalMode mode);
ApprovalMode approvalModeFromString(const std::string& value);

} // namespace doriax::editor::ai
