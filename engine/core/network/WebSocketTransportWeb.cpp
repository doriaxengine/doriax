// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "WebSocketTransport.h"

#include "Log.h"

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

using namespace doriax;

struct WebSocketTransport::State {
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    uint32_t connection = 0;
    bool reported = false;
    std::vector<NetworkEvent> pending;

    void end(){
        if (!reported){
            reported = true;
            pending.push_back({NetworkEventType::DISCONNECTED, connection, {}});
        }
    }

    static EM_BOOL onOpen(int type, const EmscriptenWebSocketOpenEvent* event, void* userData){
        State* state = (State*)userData;
        if (event->socket == state->socket){
            state->pending.push_back({NetworkEventType::CONNECTED, state->connection, {}});
        }
        return EM_TRUE;
    }

    static EM_BOOL onMessage(int type, const EmscriptenWebSocketMessageEvent* event, void* userData){
        State* state = (State*)userData;
        if (event->socket == state->socket && !event->isText){
            NetworkEvent received{NetworkEventType::RECEIVED, state->connection, {}};
            received.data.assign(event->data, event->data + event->numBytes);
            state->pending.push_back(std::move(received));
        }
        return EM_TRUE;
    }

    static EM_BOOL onClose(int type, const EmscriptenWebSocketCloseEvent* event, void* userData){
        State* state = (State*)userData;
        if (event->socket == state->socket){
            state->end();
        }
        return EM_TRUE;
    }

    static EM_BOOL onError(int type, const EmscriptenWebSocketErrorEvent* event, void* userData){
        State* state = (State*)userData;
        if (event->socket == state->socket){
            state->end();
        }
        return EM_TRUE;
    }
};

WebSocketTransport::WebSocketTransport(){
    state = std::make_unique<State>();
}

WebSocketTransport::~WebSocketTransport(){
    close();
}

bool WebSocketTransport::listen(uint16_t port, int maxConnections){
    Log::error("Network: a browser can't host");
    return false;
}

bool WebSocketTransport::connect(const std::string& address, uint16_t port){
    close();

    std::string url = address;
    if (url.find("://") == std::string::npos){
        // a secure page may only open secure sockets
        bool secure = EM_ASM_INT({ return location.protocol === 'https:' ? 1 : 0; });
        url = std::string(secure ? "wss://" : "ws://") + address + ":" + std::to_string(port);
    }

    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = url.c_str();

    EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
    if (socket <= 0){
        Log::error("Network: can't open %s", url.c_str());
        return false;
    }

    state->socket = socket;
    state->connection++;
    state->reported = false;

    emscripten_websocket_set_onopen_callback(socket, state.get(), State::onOpen);
    emscripten_websocket_set_onmessage_callback(socket, state.get(), State::onMessage);
    emscripten_websocket_set_onclose_callback(socket, state.get(), State::onClose);
    emscripten_websocket_set_onerror_callback(socket, state.get(), State::onError);

    return true;
}

void WebSocketTransport::close(){
    if (state->socket > 0){
        emscripten_websocket_close(state->socket, 1000, "");
        emscripten_websocket_delete(state->socket);
        state->socket = 0;
    }
    state->pending.clear();
}

void WebSocketTransport::poll(std::vector<NetworkEvent>& events){
    for (NetworkEvent& event : state->pending){
        events.push_back(std::move(event));
    }
    state->pending.clear();
}

bool WebSocketTransport::send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery){
    if (state->socket <= 0 || connection != state->connection || state->reported){
        return false;
    }

    return emscripten_websocket_send_binary(state->socket, (void*)data, (uint32_t)size) == EMSCRIPTEN_RESULT_SUCCESS;
}

void WebSocketTransport::flush(){
}

void WebSocketTransport::disconnect(uint32_t connection){
    if (connection == state->connection){
        close();
    }
}
