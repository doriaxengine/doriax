// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AiService.h"

#include "AiProvider.h"
#include "EditorActionExecutor.h"
#include "EditorActionRegistry.h"
#include "Out.h"
#include "RateLimitRetry.h"
#include "SecretStore.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>
#include <memory>
#include <random>
#include <sstream>
#include <unordered_map>

namespace doriax::editor::ai {

namespace {

constexpr int kMaxRateLimitRetries = 5;
constexpr auto kRetryTimingMargin = std::chrono::milliseconds(250);
constexpr auto kMaxProviderRetryDelay = std::chrono::minutes(5);
constexpr size_t kFullHistoricalToolRounds = 1;
constexpr size_t kHistoricalToolResultBytes = 2048;
constexpr size_t kHistoricalArgumentStringBytes = 512;

struct ToolRoundRange {
    size_t assistant = 0;
    size_t toolBegin = 0;
    size_t toolEnd = 0;
};

size_t utf8PrefixLength(const std::string& value, size_t maxBytes) {
    size_t end = std::min(value.size(), maxBytes);
    if (end == value.size()) {
        return end;
    }
    while (end > 0 &&
           (static_cast<unsigned char>(value[end]) & 0xc0u) == 0x80u) {
        --end;
    }
    return end;
}

std::string compactHistoricalToolContent(const std::string& content) {
    if (content.size() <= kHistoricalToolResultBytes) {
        return content;
    }

    const size_t prefixBytes = utf8PrefixLength(
        content, kHistoricalToolResultBytes);
    return content.substr(0, prefixBytes) +
           "\n... [older tool result compacted; " +
           std::to_string(content.size() - prefixBytes) + " bytes omitted]";
}

void compactHistoricalArgument(Json& value) {
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        if (text.size() > kHistoricalArgumentStringBytes) {
            const size_t prefixBytes = utf8PrefixLength(
                text, kHistoricalArgumentStringBytes);
            value = text.substr(0, prefixBytes) +
                    "\n... [older tool argument compacted]";
        }
        return;
    }

    if (value.is_array()) {
        for (Json& item : value) {
            compactHistoricalArgument(item);
        }
        return;
    }

    if (value.is_object()) {
        for (auto& item : value.items()) {
            compactHistoricalArgument(item.value());
        }
    }
}

// Provider requests are stateless, so their history must retain valid
// assistant/tool pairings. Once a later model response has consumed a tool
// round, however, its large source dumps and write arguments can be shortened.
// The stored conversation is never changed, and the newest completed round
// plus the currently unanswered round remain verbatim as working context.
void compactCompletedToolHistory(std::vector<ChatMessage>& messages) {
    std::vector<ToolRoundRange> completedRounds;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role != ChatRole::Assistant ||
            messages[i].toolCalls.empty()) {
            continue;
        }

        const size_t toolBegin = i + 1;
        size_t toolEnd = toolBegin;
        while (toolEnd < messages.size() &&
               messages[toolEnd].role == ChatRole::Tool) {
            ++toolEnd;
        }

        // A later non-tool message means the provider already consumed this
        // result run. A trailing tool run is the continuation being built now.
        if (toolEnd > toolBegin && toolEnd < messages.size()) {
            completedRounds.push_back({i, toolBegin, toolEnd});
        }
        if (toolEnd > toolBegin) {
            i = toolEnd - 1;
        }
    }

    if (completedRounds.size() <= kFullHistoricalToolRounds) {
        return;
    }

    const size_t compactCount =
        completedRounds.size() - kFullHistoricalToolRounds;
    for (size_t roundIndex = 0; roundIndex < compactCount; ++roundIndex) {
        const ToolRoundRange& round = completedRounds[roundIndex];
        ChatMessage& assistant = messages[round.assistant];
        // OpenRouter's reasoning_details can carry signatures
        const bool hasSignedReasoning =
            assistant.reasoningFields.contains("reasoning_details") ||
            std::any_of(
                assistant.toolCalls.begin(), assistant.toolCalls.end(),
                [](const ToolCall& call) {
                    return !call.thoughtSignature.empty();
                });
        if (!hasSignedReasoning) {
            for (ToolCall& call : assistant.toolCalls) {
                compactHistoricalArgument(call.arguments);
            }
        }
        for (size_t i = round.toolBegin; i < round.toolEnd; ++i) {
            messages[i].content =
                compactHistoricalToolContent(messages[i].content);
        }
    }
}

std::chrono::milliseconds retryDelay(const HttpResponse& response,
                                     const ProviderResponse& error,
                                     int nextAttempt) {
    std::chrono::milliseconds delay = rateLimitRetryDelay(
        response, error.error, error.retryDelay);
    if (delay.count() > 0) {
        return std::min(delay + kRetryTimingMargin,
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            kMaxProviderRetryDelay));
    }

    // No provider hint: use bounded exponential backoff with a small jitter so
    // multiple editor instances do not retry in lockstep.
    const int exponent = std::clamp(nextAttempt - 1, 0, 5);
    const auto base = std::chrono::seconds(1 << exponent);
    thread_local std::mt19937 rng(std::random_device{}());
    const auto jitter = std::chrono::milliseconds(
        std::uniform_int_distribution<int>(0, 500)(rng));
    return base + jitter;
}

// Turns a provider HTTP failure into a short, readable line. The detail is the
// provider's own error.message (already extracted by the response parsers),
// not the raw JSON envelope.
std::string humanizeProviderError(long status, const std::string& detail) {
    std::string label;
    bool recognized = true;
    if (status == 400) label = "The provider rejected the request";
    else if (status == 401 || status == 403) label = "Authentication failed - check your API key";
    else if (status == 404) label = "Model or endpoint not found - check your AI settings";
    else if (status == 429) label = "Rate limit or quota exceeded";
    else if (status >= 500) label = "The provider is unavailable, try again later";
    else {
        label = "Request failed";
        recognized = false;
    }

    // When the provider sent its own explanation, that alone reads best;
    // the status code only adds value for statuses we can't name.
    if (!detail.empty()) {
        return label + ". " + detail;
    }
    if (!recognized) {
        return label + " (HTTP " + std::to_string(status) + ").";
    }
    return label + ".";
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool looksLikeOllamaEndpoint(const Settings& settings) {
    if (settings.provider != ProviderId::OpenAICompatible) {
        return false;
    }
    std::string endpoint = lowercase(activeEndpointUrl(settings));
    return endpoint.find("ollama") != std::string::npos ||
           endpoint.find(":11434") != std::string::npos;
}

std::string pluralTokens(int count) {
    return count == 1 ? "token" : "tokens";
}

bool hasTokenUsage(const ProviderResponse& response) {
    return response.promptTokens >= 0 ||
           response.completionTokens >= 0 ||
           response.totalTokens >= 0;
}

bool promptUsedNearlyWholeContext(const ProviderResponse& response) {
    if (response.promptTokens < 0 || response.totalTokens <= 0) {
        return false;
    }
    int remaining = response.totalTokens - response.promptTokens;
    int smallHeadroom = std::max(32, response.totalTokens / 20);
    return remaining >= 0 && remaining <= smallHeadroom;
}

std::string tokenUsageSummary(const ProviderResponse& response) {
    std::ostringstream out;
    bool wrote = false;
    if (response.promptTokens >= 0) {
        out << response.promptTokens << " prompt";
        wrote = true;
    }
    if (response.completionTokens >= 0) {
        if (wrote) out << " + ";
        out << response.completionTokens << " output";
        wrote = true;
    }
    if (response.totalTokens >= 0) {
        if (wrote) out << " = ";
        out << response.totalTokens << " total";
    }
    return out.str();
}

// A context window comfortably above what this request already consumed, rounded
// up to a tidy boundary, so the suggestion leaves room for both prompt and reply.
int suggestContextTokens(const ProviderResponse& response) {
    int used = std::max(response.totalTokens, response.promptTokens);
    int target = used > 0 ? used * 2 : 8192;
    for (int boundary : {8192, 16384, 32768, 65536, 131072}) {
        if (target <= boundary) return boundary;
    }
    return target;
}

// Advice for a token limit that is the provider/model context window rather than
// Doriax's output cap. Ollama gets a concrete num_ctx suggestion because a small
// context window is a common culprit.
std::string contextRemedy(const ProviderRequest& request, const ProviderResponse& response) {
    if (!looksLikeOllamaEndpoint(request.settings)) {
        return "Use a larger-context model or reduce the conversation/project context and try again.";
    }
    int suggested = suggestContextTokens(response);
    std::ostringstream out;
    out << "For Ollama, raise the model context length to at least " << suggested
        << " (\"/set parameter num_ctx " << suggested << "\" in the Ollama session, or "
           "\"PARAMETER num_ctx " << suggested << "\" in the Modelfile)";
    if (response.totalTokens > 0) {
        out << "; this response stopped after " << response.totalTokens << " total tokens";
    }
    out << ". You can also use a larger-context model or reduce the conversation/project context, then try again.";
    return out.str();
}

std::string buildTruncationMessage(const ProviderRequest& request, const ProviderResponse& response) {
    const bool hasCompletionUsage = response.completionTokens >= 0;
    // The model reached (within rounding of) Doriax's own output cap, so the stop
    // is the editor's "Max output tokens" setting, not a provider/context limit.
    const bool reachedDoriaxCap =
        hasCompletionUsage && response.completionTokens + 1 >= request.settings.maxOutputTokens;
    const bool stoppedBeforeDoriaxCap = hasCompletionUsage && !reachedDoriaxCap;
    // Only trust the "context full" reading when the stop was not Doriax's cap;
    // otherwise a small cap plus a largish prompt can look like a full window.
    const bool contextLikelyFull = promptUsedNearlyWholeContext(response) && !reachedDoriaxCap;

    std::ostringstream out;
    if (contextLikelyFull) {
        out << "The provider stopped at a token limit";
        if (!response.stopReason.empty()) {
            out << " (" << response.stopReason << ")";
        }
        if (response.completionTokens >= 0) {
            out << " after " << response.completionTokens << " output "
                << pluralTokens(response.completionTokens);
        }
        out << " because the prompt used nearly the whole context window";
        if (hasTokenUsage(response)) {
            out << " (" << tokenUsageSummary(response) << " tokens)";
        }
        out << ". This is not Doriax's \"Max output tokens\" cap ("
            << request.settings.maxOutputTokens << "). " << contextRemedy(request, response);
        return out.str();
    }

    if (stoppedBeforeDoriaxCap) {
        out << "The provider stopped at its own token limit";
        if (!response.stopReason.empty()) {
            out << " (" << response.stopReason << ")";
        }
        out << " after " << response.completionTokens << " output "
            << pluralTokens(response.completionTokens)
            << ", before Doriax's configured max of "
            << request.settings.maxOutputTokens << ".";
        if (hasTokenUsage(response)) {
            out << " Usage: " << tokenUsageSummary(response) << " tokens.";
        }
        out << " " << contextRemedy(request, response);
        return out.str();
    }

    out << "The response hit the max output token limit ("
        << request.settings.maxOutputTokens
        << ") and was cut off before completing.";
    if (hasTokenUsage(response)) {
        out << " Usage: " << tokenUsageSummary(response) << " tokens.";
    }
    out << " Increase \"Max output tokens\" in AI settings (gear icon) and try again - "
           "writing a whole file such as a shader needs a higher limit.";
    return out.str();
}

} // namespace

AiService::AiService() = default;

AiService::~AiService() {
    cancel();
    if (worker.joinable()) {
        worker.join();
    }
}

void AiService::setSettings(const Settings& newSettings) {
    std::lock_guard<std::mutex> lock(mutex);
    settings = newSettings;
    settings.requestTimeoutSeconds = std::clamp(settings.requestTimeoutSeconds, 1, 3600);
    settings.maxOutputTokens = std::clamp(settings.maxOutputTokens, 256, 16000);
    settings.maxToolRounds = std::clamp(settings.maxToolRounds, 1, 100);
    if (settings.model.empty()) {
        settings.model = defaultModelForProvider(settings.provider);
    }
}

void AiService::setWakeCallback(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex);
    wakeCallback = std::move(callback);
}

void AiService::shutdown() {
    cancel();
    {
        // Dropped before the join, so a late reply cannot wake a torn-down target.
        std::lock_guard<std::mutex> lock(mutex);
        wakeCallback = nullptr;
    }
    // curl aborts from its progress callback, so this waits well under a second.
    if (worker.joinable()) {
        worker.join();
    }
}

void AiService::notifyWake() const {
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(mutex);
        callback = wakeCallback;
    }
    if (callback) callback();
}

Settings AiService::getSettings() const {
    std::lock_guard<std::mutex> lock(mutex);
    return settings;
}

bool AiService::sendUserMessage(const std::string& text,
                                std::vector<ChatAttachment>&& attachments) {
    // No move has happened yet: refusing here leaves the caller's vector
    // intact, so pending attachments survive a rejected send.
    if ((text.empty() && attachments.empty()) || isBusy()) {
        return false;
    }

    ProviderRequest request;
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Typing a new message instead of approving is an implicit decline.
        // Resolve pending tool calls before the user message goes into the
        // history: an assistant tool call left unanswered ahead of a user
        // message makes every provider reject the replayed conversation.
        for (auto& proposal : proposals) {
            if (!proposal.executed) {
                dismissProposalLocked(proposal,
                    "The user did not approve this action and sent a new message instead.");
            }
        }
        // Loaded conversations arrive without proposals, so unanswered tool
        // calls persisted in the history are repaired from the messages
        // themselves.
        repairUnansweredToolCallsLocked();
        ChatMessage userMessage;
        userMessage.role = ChatRole::User;
        userMessage.content = text;
        userMessage.model = settings.model;
        userMessage.attachments = std::move(attachments);
        messages.push_back(std::move(userMessage));
        ++revision;
        toolRounds = 0;
        turnActive = true;
        turnFailed = false;
        request = buildRequestSnapshotLocked();
    }
    dispatchRequest(std::move(request));
    return true;
}

void AiService::cancel() {
    cancelRequested.store(true);

    std::lock_guard<std::mutex> lock(mutex);
    if (pendingRetry) {
        pendingRetry.reset();
        retryScheduled.store(false);
        appendAssistantMessageLocked("Request cancelled.");
        turnFailed = true;
    }
}

bool AiService::isBusy() const {
    return busy.load() || retryScheduled.load();
}

std::string AiService::getActivityText() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (!pendingRetry) {
        // Show the in-flight tool step rather than an unchanging label.
        if (toolRounds > 0) {
            return "Thinking... (step " + std::to_string(toolRounds) + "/" +
                   std::to_string(std::max(1, settings.maxToolRounds)) + ")";
        }
        return "Thinking...";
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        pendingRetry->readyAt - std::chrono::steady_clock::now());
    const int64_t remainingMilliseconds = std::max<int64_t>(0, remaining.count());
    const int64_t remainingSeconds = (remainingMilliseconds + 999) / 1000;
    if (remainingSeconds == 0) {
        return "Rate limited - retrying now (" +
               std::to_string(pendingRetry->attempt) + "/" +
               std::to_string(kMaxRateLimitRetries) + ")";
    }
    return "Rate limited - retrying in " + std::to_string(remainingSeconds) +
           "s (" + std::to_string(pendingRetry->attempt) + "/" +
           std::to_string(kMaxRateLimitRetries) + ")";
}

void AiService::update() {
    if (busy.load()) {
        return;
    }
    // Reap the worker that produced the latest response before reusing it.
    if (worker.joinable()) {
        worker.join();
    }

    ProviderRequest request;
    int retryAttempt = 0;
    bool dispatch = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pendingRetry) {
            if (std::chrono::steady_clock::now() < pendingRetry->readyAt) {
                return;
            }
            request = std::move(pendingRetry->request);
            retryAttempt = pendingRetry->attempt;
            pendingRetry.reset();
            retryScheduled.store(false);
            dispatch = true;
        } else {
            const int maxRounds = std::max(1, settings.maxToolRounds);
            if (!needsContinuationLocked() || toolRounds >= maxRounds) {
                if (needsContinuationLocked() && toolRounds >= maxRounds) {
                    appendAssistantMessageLocked(
                        "Reached the tool-step limit for this request. Send another message to continue.");
                    finishTurnLocked(false);
                } else if (turnActive && !hasPendingProposalsLocked()) {
                    // Agent turn settled (final reply, or waiting is over with no more work).
                    finishTurnLocked(!turnFailed);
                }
                return;
            }
            ++toolRounds;
            request = buildRequestSnapshotLocked();
            dispatch = true;
        }
    }
    if (dispatch) {
        dispatchRequest(std::move(request), retryAttempt);
    }
}

std::vector<ChatMessage> AiService::getMessages() const {
    std::lock_guard<std::mutex> lock(mutex);
    return messages;
}

uint64_t AiService::getRevision() const {
    return revision.load();
}

std::vector<ActionProposal> AiService::getProposals() const {
    std::lock_guard<std::mutex> lock(mutex);
    return proposals;
}

void AiService::clearConversation() {
    if (isBusy()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    messages.clear();
    ++revision;
    proposals.clear();
    toolRounds = 0;
    turnActive = false;
    turnFailed = false;
}

void AiService::loadConversation(std::vector<ChatMessage> newMessages) {
    if (isBusy()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    messages = std::move(newMessages);
    ++revision;
    proposals.clear();
    toolRounds = 0;
    turnActive = false;
    turnFailed = false;
}

ActionResult AiService::executeProposal(uint64_t proposalId, EditorActionExecutor& executor) {
    ActionProposal proposal;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& item : proposals) {
            if (item.id == proposalId) {
                if (item.executed) {
                    return item.result;
                }
                item.executing = true;
                proposal = item;
                break;
            }
        }
    }

    if (proposal.id == 0) {
        return {false, "Action proposal not found.", Json::object()};
    }

    ActionResult result = executor.execute(proposal.toolName, proposal.arguments, &cancelRequested);

    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& item : proposals) {
            if (item.id == proposalId) {
                item.executing = false;
                item.executed = true;
                item.result = result;
                break;
            }
        }

        ChatMessage toolMessage;
        toolMessage.role = ChatRole::Tool;
        toolMessage.toolCallId = proposal.toolCallId;
        toolMessage.toolName = proposal.toolName;
        toolMessage.toolDescription = proposal.description;
        toolMessage.toolSuccess = result.success;
        toolMessage.content = EditorActionExecutor::resultText(result);
        messages.push_back(toolMessage);
        ++revision;
    }

    EditorActionExecutor::logFailure(result, "AI");

    return result;
}

void AiService::removeProposal(uint64_t proposalId) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& proposal : proposals) {
        if (proposal.id != proposalId || proposal.executed) {
            continue;
        }
        dismissProposalLocked(proposal, "The user dismissed this action.");
        break;
    }
}

// Ensures every assistant tool call is answered exactly once, by a tool
// result inside the contiguous run that follows its assistant message. Live
// turns keep this shape through the proposal list; this covers histories that
// arrive without proposals (loadConversation), where an unanswered call would
// otherwise make providers reject the replay forever. A matching result
// stranded later in the history (a stale proposal that was executed after the
// conversation moved on) is relocated into place rather than duplicated, any
// tool result left with no call to answer is dropped, and calls with no
// result anywhere get a synthetic "declined" one.
void AiService::repairUnansweredToolCallsLocked() {
    // Pass 1: a tool message is anchored when it is the first result for one
    // of its own assistant's calls, inside the run right after that assistant.
    std::vector<bool> anchored(messages.size(), false);
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role != ChatRole::Assistant || messages[i].toolCalls.empty()) {
            continue;
        }
        std::unordered_map<std::string, size_t> expected;
        for (const ToolCall& call : messages[i].toolCalls) {
            ++expected[call.id];
        }
        size_t j = i + 1;
        while (j < messages.size() && messages[j].role == ChatRole::Tool) {
            auto expectedIt = expected.find(messages[j].toolCallId);
            if (expectedIt != expected.end() && expectedIt->second > 0) {
                anchored[j] = true;
                --expectedIt->second;
            }
            ++j;
        }
        i = j - 1;
    }

    // Everything else is an orphan: queue every result per call id as a
    // candidate to relocate next to a matching call. Queues preserve the
    // multiplicity of legacy ID-less parallel calls that share a name.
    std::unordered_map<std::string, std::deque<ChatMessage>> late;
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role == ChatRole::Tool && !anchored[i]) {
            const std::string callId = messages[i].toolCallId;
            late[callId].push_back(std::move(messages[i]));
        }
    }

    // Pass 2: rebuild, appending relocated or synthetic results for calls the
    // anchored run leaves unanswered.
    std::vector<ChatMessage> repaired;
    repaired.reserve(messages.size());
    for (size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].role == ChatRole::Tool && !anchored[i]) {
            continue;
        }
        repaired.push_back(std::move(messages[i]));
        if (repaired.back().role != ChatRole::Assistant || repaired.back().toolCalls.empty()) {
            continue;
        }
        const std::vector<ToolCall> calls = repaired.back().toolCalls;

        std::unordered_map<std::string, size_t> answered;
        size_t j = i + 1;
        while (j < messages.size() && messages[j].role == ChatRole::Tool) {
            if (anchored[j]) {
                ++answered[messages[j].toolCallId];
                repaired.push_back(std::move(messages[j]));
            }
            ++j;
        }
        for (const ToolCall& call : calls) {
            auto answeredIt = answered.find(call.id);
            if (answeredIt != answered.end() && answeredIt->second > 0) {
                --answeredIt->second;
                continue;
            }
            auto lateIt = late.find(call.id);
            if (lateIt != late.end() && !lateIt->second.empty()) {
                repaired.push_back(std::move(lateIt->second.front()));
                lateIt->second.pop_front();
                if (lateIt->second.empty()) {
                    late.erase(lateIt);
                }
                continue;
            }
            ChatMessage toolMessage;
            toolMessage.role = ChatRole::Tool;
            toolMessage.toolCallId = call.id;
            toolMessage.toolName = call.name;
            toolMessage.toolDescription = EditorActionRegistry::describe(call.name, call.arguments);
            toolMessage.toolSuccess = false;
            toolMessage.content = "This action was never run before the conversation moved on; treat it as declined.";
            repaired.push_back(std::move(toolMessage));
        }
        i = j - 1;
    }
    messages = std::move(repaired);
    ++revision;
}

// Resolve the call with a dismissal result rather than dropping it, so the
// assistant's tool call stays answered and the next request that replays the
// history remains well-formed.
void AiService::dismissProposalLocked(ActionProposal& proposal, const std::string& note) {
    proposal.executing = false;
    proposal.executed = true;
    proposal.result = {false, "Dismissed by the user.", Json::object()};

    ChatMessage toolMessage;
    toolMessage.role = ChatRole::Tool;
    toolMessage.toolCallId = proposal.toolCallId;
    toolMessage.toolName = proposal.toolName;
    toolMessage.toolDescription = proposal.description;
    toolMessage.toolSuccess = false;
    toolMessage.content = note;
    messages.push_back(toolMessage);
    ++revision;
}

std::string AiService::buildSystemPrompt() const {
    std::ostringstream prompt;
    prompt
        << "You are Doriax Editor Assistant, an AI helper embedded inside the Doriax game editor.\n"
        << "You answer questions and drive the editor through the provided tools. The engine layer is not aware of you.\n"
        << "Call tools only when they help the user's explicit goal, and never invent tool names.\n"
        << "Read-only tools inspect project context. Mutating, file-writing, download, and import tools may require user approval before they run.\n"
        << "After a tool returns, use its result to decide the next step and report concise progress to the user.\n"
        << "Work efficiently within the tool-step budget: batch independent tool calls into a single step instead of one per step, and never repeat a search or read whose result you already have. Gather the API facts you need, then write the file.\n"
        << "Never request arbitrary shell commands. Never ask for secrets in chat.\n"
        << EditorActionRegistry::guidance();
    return prompt.str();
}

ProviderRequest AiService::buildRequestSnapshotLocked() const {
    ProviderRequest request;
    request.settings = settings;
    request.apiKey = SecretStore::getApiKey(accountKey(settings));
    request.messages = messages;
    // Claude rejects its thinking blocks once an earlier message changes, also
    // behind OpenRouter
    if (settings.provider != ProviderId::Anthropic &&
        lowercase(settings.model).find("claude") == std::string::npos) {
        compactCompletedToolHistory(request.messages);
    }
    for (const ToolDefinition& tool : EditorActionRegistry::tools()) {
        if (!tool.mcpOnly) request.tools.push_back(tool);
    }
    request.systemPrompt = buildSystemPrompt();
    return request;
}

void AiService::dispatchRequest(ProviderRequest request, int retryAttempt) {
    // dispatchRequest only runs when no request is in flight, but the previous
    // worker may have finished without being reaped yet (update() usually joins
    // it). Join it here before reassigning, otherwise the std::thread move-assign
    // onto a still-joinable handle would call std::terminate.
    if (worker.joinable()) {
        worker.join();
    }
    cancelRequested.store(false);
    busy.store(true);
    worker = std::thread(&AiService::runProviderRequest, this,
                         std::move(request), retryAttempt);
}

bool AiService::needsContinuationLocked() const {
    // Continue only when the conversation ends on tool results the model has
    // not yet responded to, and nothing is still awaiting user approval.
    if (messages.empty() || messages.back().role != ChatRole::Tool) {
        return false;
    }
    for (const auto& proposal : proposals) {
        if (!proposal.executed) {
            return false;
        }
    }
    return true;
}

bool AiService::hasPendingProposalsLocked() const {
    for (const auto& proposal : proposals) {
        if (!proposal.executed) {
            return true;
        }
    }
    return false;
}

void AiService::finishTurnLocked(bool success) {
    if (!turnActive) {
        return;
    }
    turnActive = false;
    turnFailed = false;
    if (success) {
        Out::success("AI chat session completed");
    }
}

void AiService::runProviderRequest(ProviderRequest request, int retryAttempt) {
    // Declared first so it destructs last: the woken frame must see `busy` cleared.
    struct WakeOnExit {
        const AiService* service;
        ~WakeOnExit() { service->notifyWake(); }
    } wakeOnExit{this};

    if (request.apiKey.empty() && accountRequiresApiKey(request.settings.provider)) {
        std::lock_guard<std::mutex> lock(mutex);
        appendAssistantMessageLocked("No API key set for " + providerLabel(request.settings.provider) +
                                     ". Open AI settings to add one before sending requests.");
        turnFailed = true;
        busy.store(false);
        return;
    }

    if (request.settings.provider == ProviderId::OpenAICompatible &&
        activeEndpointUrl(request.settings).empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        appendAssistantMessageLocked("The selected endpoint has no URL. Open AI settings to set "
                                     "its Chat Completions URL, or pick another model.");
        turnFailed = true;
        busy.store(false);
        return;
    }

    ProviderResponse parsed;
    try {
        std::unique_ptr<Provider> provider = createProvider(request.settings.provider);
        HttpRequest httpRequest = provider->buildRequest(request);
        httpRequest.timeoutSeconds = request.settings.requestTimeoutSeconds;
        HttpResponse httpResponse = httpClient.send(httpRequest, &cancelRequested);

        if (cancelRequested.load()) {
            std::lock_guard<std::mutex> lock(mutex);
            appendAssistantMessageLocked("Request cancelled.");
            turnFailed = true;
            busy.store(false);
            return;
        }

        if (!httpResponse.error.empty()) {
            parsed.error = "Connection error: " + httpResponse.error;
        } else if (httpResponse.status < 200 || httpResponse.status >= 300) {
            // Reuse the provider parser to pull error.message out of the body.
            ProviderResponse errorBody = provider->parseResponse(httpResponse.body);
            if (isRetryableRateLimit(httpResponse.status, errorBody) &&
                retryAttempt < kMaxRateLimitRetries) {
                const int nextAttempt = retryAttempt + 1;
                const std::chrono::milliseconds delay =
                    retryDelay(httpResponse, errorBody, nextAttempt);
                bool scheduled = false;
                bool cancelled = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (cancelRequested.load()) {
                        appendAssistantMessageLocked("Request cancelled.");
                        turnFailed = true;
                        cancelled = true;
                    } else {
                        pendingRetry = PendingRetry{
                            std::move(request),
                            std::chrono::steady_clock::now() + delay,
                            nextAttempt
                        };
                        retryScheduled.store(true);
                        scheduled = true;
                    }
                }
                if (cancelled) {
                    busy.store(false);
                    return;
                }
                if (scheduled) {
                    Out::warning(
                        "AI provider rate limit reached; retrying in %.2f seconds "
                        "(attempt %d/%d)",
                        static_cast<double>(delay.count()) / 1000.0,
                        nextAttempt, kMaxRateLimitRetries);
                    busy.store(false);
                    return;
                }
            }

            parsed.error = humanizeProviderError(httpResponse.status, errorBody.error);
            Out::error("AI provider request failed (HTTP %ld): %s",
                httpResponse.status,
                errorBody.error.empty() ? httpResponse.body.c_str() : errorBody.error.c_str());
        } else {
            parsed = provider->parseResponse(httpResponse.body);
        }
    } catch (const std::exception& e) {
        parsed.error = e.what();
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!parsed.error.empty()) {
            appendAssistantMessageLocked(parsed.error);
            turnFailed = true;
        } else if (parsed.toolCalls.empty() && parsed.truncated) {
            // A truncated reply often drops an incomplete tool call mid-JSON, so
            // it arrives with no usable tool call. Use provider usage, when
            // available, to distinguish the editor's output cap from a full
            // provider/model context window.
            std::string message = buildTruncationMessage(request, parsed);
            if (!parsed.text.empty()) {
                message = parsed.text + "\n\n" + message;
            }
            appendAssistantMessageLocked(message);
            turnFailed = true;
        } else if (parsed.text.empty() && parsed.toolCalls.empty()) {
            appendAssistantMessageLocked("The provider returned no text or tool calls.");
            turnFailed = true;
        } else {
            ChatMessage assistant;
            assistant.role = ChatRole::Assistant;
            assistant.model = request.settings.model;
            assistant.content = parsed.text;
            assistant.toolCalls = parsed.toolCalls;
            assistant.anthropicContent = parsed.anthropicContent;
            assistant.reasoningFields = parsed.reasoningFields;
            messages.push_back(assistant);
            ++revision;
            for (const ToolCall& call : parsed.toolCalls) {
                addToolCallProposalLocked(call);
            }
        }
    }

    busy.store(false);
}

void AiService::appendAssistantMessageLocked(const std::string& text) {
    messages.push_back({ChatRole::Assistant, text});
    ++revision;
}

void AiService::addToolCallProposalLocked(const ToolCall& call) {
    ActionProposal proposal;
    proposal.id = nextProposalId++;
    proposal.toolCallId = call.id;
    proposal.toolName = call.name;
    proposal.arguments = call.arguments;
    proposal.readOnly = EditorActionRegistry::isReadOnly(call.name);

    // A wrong-typed argument throws here, outside the worker's try/catch: that would
    // terminate the editor, so route it through the validation-failure path.
    ValidationResult validation;
    try {
        proposal.description = EditorActionRegistry::describe(call.name, call.arguments);
        validation = EditorActionRegistry::validate(call.name, call.arguments);
        if (validation.ok && EditorActionRegistry::getTool(call.name).mcpOnly) {
            validation = {false, call.name + " is only available to MCP clients."};
        }
    } catch (const std::exception& e) {
        if (proposal.description.empty()) {
            proposal.description = call.name;
        }
        validation = {false, "Malformed arguments for " + call.name + ": " + e.what() +
                             ". Re-send the call with each field in its documented type."};
    }

    if (!validation.ok) {
        // Resolve invalid calls immediately and feed the error back so the
        // tool round-trip stays well-formed and the model can recover.
        proposal.executed = true;
        proposal.result = {false, validation.error, Json::object()};

        ChatMessage toolMessage;
        toolMessage.role = ChatRole::Tool;
        toolMessage.toolCallId = call.id;
        toolMessage.toolName = call.name;
        toolMessage.toolDescription = proposal.description;
        toolMessage.toolSuccess = false;
        toolMessage.content = EditorActionExecutor::resultText(proposal.result);
        messages.push_back(toolMessage);
        ++revision;
    }
    proposals.push_back(proposal);
}

} // namespace doriax::editor::ai
