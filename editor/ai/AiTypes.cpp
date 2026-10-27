// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "AiTypes.h"

#include <cctype>

namespace doriax::editor::ai {

std::string toString(ProviderId provider) {
    switch (provider) {
        case ProviderId::OpenAI: return "openai";
        case ProviderId::Anthropic: return "anthropic";
        case ProviderId::Gemini: return "gemini";
        case ProviderId::DeepSeek: return "deepseek";
        case ProviderId::OpenAICompatible: return "openai_compatible";
    }
    return "openai";
}

std::string providerLabel(ProviderId provider) {
    switch (provider) {
        case ProviderId::OpenAI: return "OpenAI";
        case ProviderId::Anthropic: return "Anthropic";
        case ProviderId::Gemini: return "Gemini";
        case ProviderId::DeepSeek: return "DeepSeek";
        case ProviderId::OpenAICompatible: return "OpenAI-compatible";
    }
    return "OpenAI";
}

ProviderId providerFromString(const std::string& value) {
    if (value == "anthropic") return ProviderId::Anthropic;
    if (value == "gemini") return ProviderId::Gemini;
    if (value == "deepseek") return ProviderId::DeepSeek;
    if (value == "openai_compatible") return ProviderId::OpenAICompatible;
    return ProviderId::OpenAI;
}

std::string defaultModelForProvider(ProviderId provider) {
    switch (provider) {
        case ProviderId::OpenAI: return "gpt-5.6-terra"; // GPT-6 calls tools only through /v1/responses
        case ProviderId::Anthropic: return "claude-sonnet-5-5";
        case ProviderId::Gemini: return "gemini-3.8-flash"; // Flash has a free tier; Pro does not
        case ProviderId::DeepSeek: return "deepseek-flash";
        case ProviderId::OpenAICompatible: return ""; // whatever the endpoint serves
    }
    return "gpt-5.6-terra";
}

std::string accountKey(ProviderId provider, const std::string& endpointId) {
    if (provider != ProviderId::OpenAICompatible || endpointId.empty()) {
        return toString(provider);
    }
    return toString(provider) + ":" + endpointId;
}

std::string accountKey(const Settings& settings) {
    return accountKey(settings.provider, settings.endpointId);
}

const CustomEndpoint* findEndpoint(const Settings& settings, const std::string& endpointId) {
    for (const CustomEndpoint& endpoint : settings.customEndpoints) {
        if (endpoint.id == endpointId) return &endpoint;
    }
    return nullptr;
}

std::string activeEndpointUrl(const Settings& settings) {
    const CustomEndpoint* endpoint = findEndpoint(settings, settings.endpointId);
    return endpoint ? endpoint->url : std::string();
}

std::vector<ProviderAccount> listAccounts(const Settings& settings) {
    std::vector<ProviderAccount> accounts;
    for (ProviderId provider : {ProviderId::OpenAI, ProviderId::Anthropic,
                                ProviderId::Gemini, ProviderId::DeepSeek}) {
        ProviderAccount account;
        account.provider = provider;
        account.label = providerLabel(provider);
        account.id = accountKey(provider, "");
        accounts.push_back(account);
    }
    for (const CustomEndpoint& endpoint : settings.customEndpoints) {
        ProviderAccount account;
        account.provider = ProviderId::OpenAICompatible;
        account.endpointId = endpoint.id;
        account.label = endpoint.label.empty() ? endpoint.id : endpoint.label;
        account.id = accountKey(ProviderId::OpenAICompatible, endpoint.id);
        account.url = endpoint.url;
        accounts.push_back(account);
    }
    return accounts;
}

const std::vector<EndpointPreset>& endpointPresets() {
    static const std::vector<EndpointPreset> presets = {
        {"opencode", "OpenCode Zen", "https://opencode.ai/zen/v1/chat/completions"},
        {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1/chat/completions"},
        {"ollama", "Ollama (local)", "http://localhost:11434/v1/chat/completions"}
    };
    return presets;
}

std::string makeEndpointId(const Settings& settings, const std::string& label) {
    std::string base;
    for (char c : label) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) {
            base.push_back(static_cast<char>(std::tolower(uc)));
        } else if (!base.empty() && base.back() != '-') {
            base.push_back('-');
        }
    }
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "endpoint";

    std::string candidate = base;
    for (int suffix = 2; findEndpoint(settings, candidate) != nullptr; ++suffix) {
        candidate = base + "-" + std::to_string(suffix);
    }
    return candidate;
}

std::string toString(ChatRole role) {
    switch (role) {
        case ChatRole::System: return "system";
        case ChatRole::User: return "user";
        case ChatRole::Assistant: return "assistant";
        case ChatRole::Tool: return "tool";
    }
    return "user";
}

ChatRole chatRoleFromString(const std::string& value) {
    if (value == "system") return ChatRole::System;
    if (value == "assistant") return ChatRole::Assistant;
    if (value == "tool") return ChatRole::Tool;
    return ChatRole::User;
}

std::string toString(ApprovalMode mode) {
    switch (mode) {
        case ApprovalMode::PreviewThenApprove: return "preview_then_approve";
        case ApprovalMode::SafeAutoRun: return "safe_auto_run";
        case ApprovalMode::FullAgent: return "full_agent";
    }
    return "preview_then_approve";
}

ApprovalMode approvalModeFromString(const std::string& value) {
    if (value == "safe_auto_run") return ApprovalMode::SafeAutoRun;
    if (value == "full_agent") return ApprovalMode::FullAgent;
    return ApprovalMode::PreviewThenApprove;
}

} // namespace doriax::editor::ai
