// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "McpServer.h"

// Before other headers: on Windows it must include winsock2.h ahead of windows.h
#include "httplib.h"

#include "imgui.h"

#include "Base64.h"
#include "EditorActionExecutor.h"
#include "EditorActionRegistry.h"
#include "EditorHost.h"
#include "EditorVersion.h"
#include "HttpClient.h"
#include "Out.h"
#include "SecretStore.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <iterator>
#include <random>

namespace doriax::editor::ai {

namespace {

constexpr const char* kHost = "127.0.0.1";
constexpr const char* kPath = "/mcp";
constexpr const char* kTokenAccount = "mcp_server";

// Stateless revision: every request carries its version in _meta
constexpr const char* kModernVersion = "2026-07-28";
// Revisions that open with an initialize handshake, newest first
const char* const kLegacyVersions[] = {"2025-11-25", "2025-06-18", "2025-03-26"};

// How long a call may wait for the main thread, e.g. during a project load
constexpr auto kQueueTimeout = std::chrono::minutes(2);
constexpr auto kPollInterval = std::chrono::milliseconds(100);
// Main-thread time per frame for calls, as the chat's auto-run allows
constexpr auto kFrameBudget = std::chrono::milliseconds(50);

constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;
constexpr int kHeaderMismatch = -32020;
constexpr int kUnsupportedProtocolVersion = -32022;

struct RpcError {
    int httpStatus;
    int code;
    std::string message;
    Json data = nullptr;
};

std::string stringField(const Json& object, const char* key) {
    if (!object.is_object()) return {};
    auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool isLegacyVersion(const std::string& version) {
    return std::find(std::begin(kLegacyVersions), std::end(kLegacyVersions), version) != std::end(kLegacyVersions);
}

Json supportedVersions() {
    Json versions = Json::array({kModernVersion});
    for (const char* version : kLegacyVersions) {
        versions.push_back(version);
    }
    return versions;
}

Json errorResponse(const Json& id, int code, const std::string& message, const Json& data = nullptr) {
    Json error = {{"code", code}, {"message", message}};
    if (!data.is_null()) error["data"] = data;
    Json response = {{"jsonrpc", "2.0"}, {"error", error}};
    if (!id.is_null()) response["id"] = id;
    return response;
}

void reply(httplib::Response& res, int status, const Json& body) {
    res.status = status;
    // Tool results can carry invalid UTF-8 from files and logs
    res.set_content(body.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json");
}

// Browsers send the page's origin, so this refuses requests from websites
bool isLocalOrigin(const std::string& origin) {
    const size_t scheme = origin.find("://");
    if (scheme == std::string::npos) return false;
    std::string host = origin.substr(scheme + 3);
    host = host.rfind('[', 0) == 0 ? host.substr(0, host.find(']') + 1) : host.substr(0, host.find(':'));
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool authorize(const httplib::Request& req, httplib::Response& res) {
    if (req.has_header("Origin") && !isLocalOrigin(req.get_header_value("Origin"))) {
        reply(res, 403, errorResponse(nullptr, kInvalidRequest, "Origin not allowed."));
        return false;
    }
    // Read per request, so a replaced token applies at once
    const std::string token = McpServer::storedToken();
    if (token.empty() || !constantTimeEquals(httplib::get_bearer_token_auth(req), token)) {
        res.set_header("WWW-Authenticate", "Bearer");
        reply(res, 401, errorResponse(nullptr, kInvalidRequest,
            "Missing or wrong token. Copy the connection command from Editor Settings > AI > MCP Server in the Doriax editor."));
        return false;
    }
    return true;
}

// Header values that are not plain ASCII arrive as =?base64?...?=
std::string decodeHeaderValue(const std::string& value) {
    const std::string prefix = "=?base64?";
    const std::string suffix = "?=";
    if (value.size() < prefix.size() + suffix.size() || value.rfind(prefix, 0) != 0 ||
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) != 0) {
        return value;
    }
    std::vector<unsigned char> bytes = Base64::decode(value.substr(prefix.size(), value.size() - prefix.size() - suffix.size()));
    return std::string(bytes.begin(), bytes.end());
}

// 2026-07-28 mirrors body fields in headers, and they must match
void checkModernHeaders(const httplib::Request& req, const std::string& method, const Json& params,
                        const std::string& metaVersion, const std::string& headerVersion) {
    if (headerVersion != metaVersion) {
        throw RpcError{400, kHeaderMismatch, "MCP-Protocol-Version does not match the _meta protocol version."};
    }
    if (metaVersion != kModernVersion) {
        throw RpcError{400, kUnsupportedProtocolVersion, "Unsupported protocol version",
                       {{"supported", supportedVersions()}, {"requested", metaVersion}}};
    }
    if (req.get_header_value("Mcp-Method") != method) {
        throw RpcError{400, kHeaderMismatch, "Mcp-Method does not match the request method."};
    }
    if (method == "tools/call" && decodeHeaderValue(req.get_header_value("Mcp-Name")) != stringField(params, "name")) {
        throw RpcError{400, kHeaderMismatch, "Mcp-Name does not match the tool name."};
    }
}

constexpr const char* kPreamble =
    "You are connected to the Doriax game editor the user has open. These tools work on the open project "
    "through the editor, so changes show up there right away and scene edits go through its undo history.\n"
    "The editor keeps open scenes and project.yaml in memory and writes them back when it saves, so change "
    "scenes, entities, components and project settings only through these tools, never by editing those files on disk.\n"
    "Write scripts with update_script_file rather than editing the file on disk: it also refreshes the "
    "exposed properties of every entity that uses the script.\n";

// Claude Code keeps only the first 4096 characters, so the other rules come from get_editor_guidance
const std::string& instructions() {
    static const std::string text = std::string(kPreamble) +
        "These instructions hold only the editor's most important rules. Before your first change in a session, "
        "call get_editor_guidance once and follow everything it returns, which adds the rules for physics, "
        "scripts, shaders and builds.\n" +
        EditorActionRegistry::essentialGuidance();
    return text;
}

// What get_editor_guidance returns: the instructions with every rule
const std::string& fullInstructions() {
    static const std::string text = kPreamble + EditorActionRegistry::guidance();
    return text;
}

Json serverInfo() {
    return {{"name", "doriax-editor"}, {"title", "Doriax Editor"}, {"version", DORIAX_EDITOR_VERSION}};
}

Json initializeResult(const Json& params) {
    const std::string requested = stringField(params, "protocolVersion");
    return {
        {"protocolVersion", isLegacyVersion(requested) ? requested : std::string(kLegacyVersions[0])},
        {"capabilities", {{"tools", {{"listChanged", false}}}}},
        {"serverInfo", serverInfo()},
        {"instructions", instructions()}
    };
}

Json discoverResult() {
    return {
        {"supportedVersions", supportedVersions()},
        {"capabilities", {{"tools", Json::object()}}},
        {"_meta", {{"io.modelcontextprotocol/serverInfo", serverInfo()}}},
        {"instructions", instructions()}
    };
}

Json toolList(bool allowChanges) {
    Json tools = Json::array();
    for (const ToolDefinition& tool : EditorActionRegistry::tools()) {
        if (!tool.readOnly && !allowChanges) continue;
        tools.push_back({
            {"name", tool.name},
            {"description", tool.description},
            {"inputSchema", tool.parameters},
            {"annotations", {{"readOnlyHint", tool.readOnly}}}
        });
    }
    return tools;
}

ActionResult changesTurnedOff() {
    return {false, "Changes are turned off for MCP in the Doriax editor (Editor Settings > AI > MCP Server), "
                   "so only read-only tools can run."};
}

Json toolResult(const ActionResult& result) {
    return {
        {"content", Json::array({{{"type", "text"}, {"text", EditorActionExecutor::resultText(result)}}})},
        {"isError", !result.success}
    };
}

} // namespace

struct McpServer::PendingCall {
    std::string name;
    Json arguments;
    std::string description;
    Json requestId;
    std::promise<ActionResult> promise;
    // Taken by the main thread to run the call, or by its waiter to give up on it
    std::atomic<bool> claimed{false};
    // Asks a running action to stop early
    std::atomic<bool> cancel{false};
};

McpServer::McpServer(Project* project, ResourcesWindow* resourcesWindow)
    : project(project)
    , resourcesWindow(resourcesWindow) {
}

McpServer::~McpServer() {
    shutdown();
}

void McpServer::applySettings(const McpSettings& settings) {
    allowChanges = settings.allowChanges;
    if (settings.enabled && isRunning() && port == settings.port) {
        return;
    }

    const bool wasRunning = server != nullptr;
    shutdown();
    error.clear();
    if (settings.enabled) {
        start(settings.port);
    } else if (wasRunning) {
        Out::info("MCP server stopped");
    }
}

void McpServer::shutdown() {
    if (!server) return;

    // Handlers waiting on a queued call answer it; none is running while the main thread is here
    stopping = true;
    server->stop();
    listener.join();
    server.reset();
    stopping = false;
}

bool McpServer::isRunning() const {
    return server && server->is_running();
}

std::string McpServer::getError() const {
    if (server && !server->is_running()) {
        return "The server stopped on its own. Press OK to start it again.";
    }
    return error;
}

std::string McpServer::endpointUrl(int port) {
    return std::string("http://") + kHost + ":" + std::to_string(port) + kPath;
}

std::string McpServer::storedToken() {
    return SecretStore::getApiKey(kTokenAccount);
}

std::string McpServer::newToken() {
    static const char* digits = "0123456789abcdef";
    std::random_device device;
    std::string token;
    for (int i = 0; i < 32; ++i) {
        const unsigned int byte = device() & 0xFF;
        token.push_back(digits[byte >> 4]);
        token.push_back(digits[byte & 0x0F]);
    }
    SecretStore::setApiKey(kTokenAccount, token);
    return token;
}

void McpServer::start(int newPort) {
    port = newPort;

    auto next = std::make_unique<httplib::Server>();
    next->new_task_queue = [] { return new httplib::ThreadPool(2, 32); };
    // httplib's SO_REUSEPORT (SO_REUSEADDR on Windows) would let a second editor share the port
#ifdef _WIN32
    next->set_socket_options([](auto) {});
    // The default 1 ms keeps waking the accept loop
    next->set_idle_interval(std::chrono::milliseconds(100));
#else
    next->set_socket_options([](auto sock) {
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);
    });
#endif
    next->set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
        return authorize(req, res) ? httplib::Server::HandlerResponse::Unhandled
                                   : httplib::Server::HandlerResponse::Handled;
    });
    next->Post(kPath, [this](const httplib::Request& req, httplib::Response& res) {
        handlePost(req, res);
    });
    // Older revisions open a GET stream or DELETE a session, neither exists here
    auto notAllowed = [](const httplib::Request&, httplib::Response& res) {
        res.status = 405;
        res.set_header("Allow", "POST");
    };
    next->Get(kPath, notAllowed);
    next->Delete(kPath, notAllowed);

    if (!next->bind_to_port(kHost, newPort)) {
        error = "Could not listen on port " + std::to_string(newPort) + ". Another program may be using it.";
        Out::error("MCP server: %s", error.c_str());
        return;
    }

    server = std::move(next);
    listener = std::thread([listening = server.get()]() {
        listening->listen_after_bind();
    });
    server->wait_until_ready();
    Out::info("MCP server listening on %s", endpointUrl(newPort).c_str());
}

void McpServer::handlePost(const httplib::Request& req, httplib::Response& res) {
    Json message = Json::parse(req.body, nullptr, false);
    if (message.is_discarded()) {
        reply(res, 400, errorResponse(nullptr, kParseError, "Parse error: the body is not valid JSON."));
        return;
    }

    const std::string method = stringField(message, "method");
    if (stringField(message, "jsonrpc") != "2.0" || method.empty()) {
        reply(res, 400, errorResponse(nullptr, kInvalidRequest, "Invalid JSON-RPC message."));
        return;
    }
    const Json params = message.contains("params") && message["params"].is_object()
        ? message["params"]
        : Json::object();

    if (!message.contains("id")) {
        // Notifications need no answer; legacy clients cancel a call with one
        if (method == "notifications/cancelled" && params.contains("requestId")) {
            cancelCall(params["requestId"]);
        }
        res.status = 202;
        return;
    }

    const Json id = message["id"];
    if (!id.is_string() && !id.is_number_integer()) {
        reply(res, 400, errorResponse(nullptr, kInvalidRequest, "The request id must be a string or an integer."));
        return;
    }

    const std::string metaVersion = stringField(params.value("_meta", Json()), "io.modelcontextprotocol/protocolVersion");
    const std::string headerVersion = req.get_header_value("MCP-Protocol-Version");
    const bool modern = !metaVersion.empty() || headerVersion == kModernVersion;

    try {
        if (modern) {
            checkModernHeaders(req, method, params, metaVersion, headerVersion);
        } else if (!headerVersion.empty() && !isLegacyVersion(headerVersion)) {
            throw RpcError{400, kInvalidRequest, "Unsupported MCP-Protocol-Version: " + headerVersion,
                           {{"supported", supportedVersions()}}};
        }

        Json result = dispatch(method, id, params, modern, req);
        if (modern) {
            result["resultType"] = "complete";
            if (method == "server/discover" || method == "tools/list") {
                // Always stale, since turning changes on or off alters the tool list
                result["ttlMs"] = 0;
                result["cacheScope"] = "private";
            }
        }
        reply(res, 200, {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
    } catch (const RpcError& e) {
        reply(res, e.httpStatus, errorResponse(id, e.code, e.message, e.data));
    }
}

Json McpServer::dispatch(const std::string& method, const Json& id, const Json& params,
                         bool modern, const httplib::Request& req) {
    if (method == "initialize" && !modern) return initializeResult(params);
    if (method == "server/discover") return discoverResult();
    if (method == "ping") return Json::object();
    if (method == "tools/list") return {{"tools", toolList(allowChanges)}};
    if (method == "tools/call") return callTool(id, params, req);

    // Only 2026-07-28 answers an unknown method with 404
    throw RpcError{modern ? 404 : 200, kMethodNotFound, "Method not found: " + method};
}

Json McpServer::callTool(const Json& id, const Json& params, const httplib::Request& req) {
    const std::string name = stringField(params, "name");
    if (!EditorActionRegistry::hasTool(name)) {
        throw RpcError{200, kInvalidParams, "Unknown tool: " + name};
    }
    // Static text, so it needs no turn on the main thread
    if (name == "get_editor_guidance") {
        return toolResult({true, fullInstructions()});
    }
    if (!allowChanges && !EditorActionRegistry::isReadOnly(name)) {
        return toolResult(changesTurnedOff());
    }
    Json arguments = params.value("arguments", Json::object());
    if (arguments.is_null()) {
        arguments = Json::object();
    }

    // Throws on a wrong-typed argument, which the chat also turns into an error
    std::string description;
    try {
        description = EditorActionRegistry::describe(name, arguments);
    } catch (const std::exception& e) {
        return toolResult({false, "Malformed arguments for " + name + ": " + e.what() +
                                  ". Re-send the call with each field in its documented type."});
    }
    return toolResult(runOnMainThread(name, arguments, description, id, req));
}

ActionResult McpServer::runOnMainThread(const std::string& name, const Json& arguments,
                                        const std::string& description, const Json& id,
                                        const httplib::Request& req) {
    auto call = std::make_shared<PendingCall>();
    call->name = name;
    call->arguments = arguments;
    call->description = description;
    call->requestId = id;
    std::future<ActionResult> future = call->promise.get_future();
    {
        std::lock_guard<std::mutex> lock(callsMutex);
        pendingCalls.push_back(call);
    }
    getEditorHost().enqueueMainThreadTask([this, call]() { runCall(call); });

    const auto queuedAt = std::chrono::steady_clock::now();
    std::string abandonReason;
    while (future.wait_for(kPollInterval) != std::future_status::ready) {
        std::string reason;
        if (stopping) {
            reason = "The MCP server stopped before the editor ran this call.";
        } else if (call->cancel) {
            reason = "The call was cancelled.";
        } else if (req.is_connection_closed()) {
            reason = "The client disconnected.";
        } else if (!call->claimed && std::chrono::steady_clock::now() - queuedAt > kQueueTimeout) {
            reason = "The editor was busy (for example loading a project) and did not start this call in time. Try again.";
        }
        if (reason.empty()) continue;

        if (!call->claimed.exchange(true)) {
            abandonReason = reason;
            break;
        }
        // Already running, so let it stop early and wait for it
        call->cancel = true;
    }

    {
        std::lock_guard<std::mutex> lock(callsMutex);
        pendingCalls.erase(std::remove(pendingCalls.begin(), pendingCalls.end(), call), pendingCalls.end());
    }
    if (!abandonReason.empty()) {
        return {false, abandonReason};
    }
    return future.get();
}

// Main thread. Past the frame budget, the call waits for the next frame so a
// batch of calls cannot freeze the editor.
void McpServer::runCall(const std::shared_ptr<PendingCall>& call) {
    const auto now = std::chrono::steady_clock::now();
    if (ImGui::GetFrameCount() != budgetFrame) {
        budgetFrame = ImGui::GetFrameCount();
        budgetStart = now;
    } else if (now - budgetStart > kFrameBudget) {
        getEditorHost().enqueueMainThreadTask([this, call]() { runCall(call); });
        return;
    }
    // Waits for the scene save instead of failing
    if (EditorActionExecutor::isBlockedBySave(project, call->name)) {
        getEditorHost().enqueueMainThreadTask([this, call]() { runCall(call); });
        return;
    }
    if (call->claimed.exchange(true)) return;

    // Changes may have been turned off while the call waited
    if (!allowChanges && !EditorActionRegistry::isReadOnly(call->name)) {
        call->promise.set_value(changesTurnedOff());
        return;
    }

    HttpClient httpClient;
    EditorActionExecutor executor(project, resourcesWindow, &httpClient);
    ActionResult result = executor.execute(call->name, call->arguments, &call->cancel);
    // Shows the user what an outside agent changed
    if (result.success && !EditorActionRegistry::isReadOnly(call->name)) {
        Out::info("MCP: %s", call->description.c_str());
    }
    EditorActionExecutor::logFailure(result, "MCP");
    call->promise.set_value(std::move(result));
}

void McpServer::cancelCall(const Json& requestId) {
    std::lock_guard<std::mutex> lock(callsMutex);
    // Ids are only unique per client, so a cancel that matches two calls is ignored
    std::shared_ptr<PendingCall> match;
    for (const std::shared_ptr<PendingCall>& call : pendingCalls) {
        if (call->requestId != requestId) continue;
        if (match) return;
        match = call;
    }
    if (match) {
        match->cancel = true;
    }
}

} // namespace doriax::editor::ai
