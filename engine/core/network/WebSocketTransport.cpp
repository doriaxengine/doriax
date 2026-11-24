// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "WebSocketTransport.h"

#include "Log.h"
#include "util/Base64.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <map>
#include <random>

#ifdef _WIN32
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET SocketHandle;
    const SocketHandle invalidSocket = INVALID_SOCKET;
    const int shutdownSend = SD_SEND;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <unistd.h>
    #include <cerrno>
    typedef int SocketHandle;
    const SocketHandle invalidSocket = -1;
    const int shutdownSend = SHUT_WR;
#endif

using namespace doriax;

namespace {
    const size_t maxHeaderSize = 8192;
    const size_t maxMessageSize = 1 << 20;
    // a client that stopped reading is dropped past this
    const size_t maxPendingSize = 8 << 20;
    // wait for a peer to read the last messages before the socket closes
    const std::chrono::seconds closeTimeout(3);

    const uint8_t opcodeBinary = 0x2;
    const uint8_t opcodeClose = 0x8;
    const uint8_t opcodePing = 0x9;
    const uint8_t opcodePong = 0xA;

#ifdef MSG_NOSIGNAL
    const int sendFlags = MSG_NOSIGNAL;
#else
    const int sendFlags = 0;
#endif

    bool startSockets(){
#ifdef _WIN32
        static bool started = false;
        if (!started){
            WSADATA data;
            started = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
        }
        return started;
#else
        return true;
#endif
    }

    void closeSocket(SocketHandle socket){
#ifdef _WIN32
        closesocket(socket);
#else
        ::close(socket);
#endif
    }

    bool wouldBlock(){
#ifdef _WIN32
        return WSAGetLastError() == WSAEWOULDBLOCK;
#else
        return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR;
#endif
    }

    void prepareSocket(SocketHandle socket){
#ifdef _WIN32
        u_long nonBlocking = 1;
        ioctlsocket(socket, FIONBIO, &nonBlocking);
#else
        fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#endif
        int on = 1;
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&on, sizeof(on));
#ifdef SO_NOSIGPIPE
        setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, (const char*)&on, sizeof(on));
#endif
    }

    // 1 connected, 0 still connecting, -1 failed
    int getConnectResult(SocketHandle socket){
#ifdef _WIN32
        fd_set writeSet, errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(socket, &writeSet);
        FD_SET(socket, &errorSet);
        timeval timeout = {0, 0};
        if (select(0, NULL, &writeSet, &errorSet, &timeout) <= 0){
            return 0;
        }
        if (FD_ISSET(socket, &errorSet)){
            return -1;
        }
#else
        pollfd descriptor = {socket, POLLOUT, 0};
        if (::poll(&descriptor, 1, 0) <= 0){
            return 0;
        }
#endif
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (char*)&error, &size) != 0 || error != 0){
            return -1;
        }

        return 1;
    }

    void sha1(const std::string& input, uint8_t digest[20]){
        uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

        std::string data = input;
        uint64_t bits = (uint64_t)input.size() * 8;
        data += (char)0x80;
        while (data.size() % 64 != 56){
            data += (char)0;
        }
        for (int i = 7; i >= 0; i--){
            data += (char)(bits >> (8 * i));
        }

        for (size_t chunk = 0; chunk < data.size(); chunk += 64){
            uint32_t w[80];
            for (int i = 0; i < 16; i++){
                const uint8_t* p = (const uint8_t*)data.data() + chunk + i * 4;
                w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
            }
            for (int i = 16; i < 80; i++){
                uint32_t value = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
                w[i] = (value << 1) | (value >> 31);
            }

            uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
            for (int i = 0; i < 80; i++){
                uint32_t f, k;
                if (i < 20){
                    f = (b & c) | (~b & d);
                    k = 0x5A827999;
                }else if (i < 40){
                    f = b ^ c ^ d;
                    k = 0x6ED9EBA1;
                }else if (i < 60){
                    f = (b & c) | (b & d) | (c & d);
                    k = 0x8F1BBCDC;
                }else{
                    f = b ^ c ^ d;
                    k = 0xCA62C1D6;
                }
                uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
                e = d;
                d = c;
                c = (b << 30) | (b >> 2);
                b = a;
                a = temp;
            }
            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
        }

        for (int i = 0; i < 5; i++){
            digest[i * 4] = (uint8_t)(h[i] >> 24);
            digest[i * 4 + 1] = (uint8_t)(h[i] >> 16);
            digest[i * 4 + 2] = (uint8_t)(h[i] >> 8);
            digest[i * 4 + 3] = (uint8_t)h[i];
        }
    }

    // what the server answers to a key, RFC 6455
    std::string getAcceptKey(const std::string& key){
        uint8_t digest[20];
        sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", digest);

        return Base64::encode(digest, sizeof(digest));
    }

    std::string getHeader(const std::string& headers, const std::string& name){
        std::string lower = headers;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return (char)tolower(c); });

        size_t start = lower.find("\r\n" + name + ":");
        if (start == std::string::npos){
            return "";
        }
        start += name.size() + 3;
        size_t end = headers.find("\r\n", start);
        std::string value = headers.substr(start, end - start);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);

        return value;
    }
}

struct WebSocketTransport::State {
    struct Connection {
        SocketHandle socket = invalidSocket;
        // opened by this side, so its frames are masked
        bool client = false;
        bool connecting = false;
        bool open = false;
        bool closing = false;
        bool shut = false;
        bool failed = false;
        std::chrono::steady_clock::time_point closeDeadline;
        std::string key;
        std::string request;
        std::vector<uint8_t> in;
        std::vector<uint8_t> out;
        std::vector<uint8_t> message;
    };

    SocketHandle listener = invalidSocket;
    int maxConnections = 0;
    uint32_t lastConnection = 0;
    std::map<uint32_t, Connection> connections;
    std::mt19937 random{std::random_device{}()};

    void queueFrame(Connection& connection, uint8_t opcode, const uint8_t* data, size_t size){
        std::vector<uint8_t>& out = connection.out;

        out.push_back(0x80 | opcode);
        uint8_t maskBit = connection.client ? 0x80 : 0;
        if (size < 126){
            out.push_back(maskBit | (uint8_t)size);
        }else if (size <= 0xFFFF){
            out.push_back(maskBit | 126);
            out.push_back((uint8_t)(size >> 8));
            out.push_back((uint8_t)size);
        }else{
            out.push_back(maskBit | 127);
            for (int i = 7; i >= 0; i--){
                out.push_back((uint8_t)((uint64_t)size >> (8 * i)));
            }
        }

        if (connection.client){
            uint32_t maskValue = random();
            uint8_t mask[4];
            memcpy(mask, &maskValue, 4);
            out.insert(out.end(), mask, mask + 4);
            size_t start = out.size();
            out.insert(out.end(), data, data + size);
            for (size_t i = 0; i < size; i++){
                out[start + i] ^= mask[i % 4];
            }
        }else{
            out.insert(out.end(), data, data + size);
        }
    }

    bool flushOut(Connection& connection){
        size_t sent = 0;
        while (sent < connection.out.size()){
            int result = (int)::send(connection.socket, (const char*)connection.out.data() + sent, (int)std::min(connection.out.size() - sent, (size_t)65536), sendFlags);
            if (result <= 0){
                if (!wouldBlock()){
                    return false;
                }
                break;
            }
            sent += (size_t)result;
        }
        connection.out.erase(connection.out.begin(), connection.out.begin() + sent);

        return true;
    }

    // false when the headers are not a WebSocket handshake
    bool openConnection(Connection& connection, const std::string& headers){
        if (connection.client){
            return headers.compare(0, 12, "HTTP/1.1 101") == 0 && getHeader(headers, "sec-websocket-accept") == getAcceptKey(connection.key);
        }

        std::string key = getHeader(headers, "sec-websocket-key");
        if (key.empty() || headers.compare(0, 4, "GET ") != 0){
            std::string response = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
            ::send(connection.socket, response.data(), (int)response.size(), sendFlags);
            return false;
        }

        std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + getAcceptKey(key) + "\r\n\r\n";
        connection.out.insert(connection.out.end(), response.begin(), response.end());

        return true;
    }

    bool readFrames(Connection& connection, uint32_t id, std::vector<NetworkEvent>& events){
        std::vector<uint8_t>& in = connection.in;
        size_t consumed = 0;

        while (in.size() - consumed >= 2){
            uint8_t* frame = in.data() + consumed;
            size_t available = in.size() - consumed;

            bool fin = frame[0] & 0x80;
            uint8_t opcode = frame[0] & 0x0F;
            bool masked = frame[1] & 0x80;
            uint64_t length = frame[1] & 0x7F;
            size_t offset = 2;

            // no extension was negotiated, and a client always masks
            if ((frame[0] & 0x70) || (!connection.client && !masked)){
                return false;
            }

            if (length == 126){
                if (available < 4){
                    break;
                }
                length = ((uint64_t)frame[2] << 8) | frame[3];
                offset = 4;
            }else if (length == 127){
                if (available < 10){
                    break;
                }
                length = 0;
                for (int i = 0; i < 8; i++){
                    length = (length << 8) | frame[2 + i];
                }
                offset = 10;
            }
            if (length > maxMessageSize){
                return false;
            }

            size_t maskOffset = offset;
            if (masked){
                offset += 4;
            }
            if (available < offset + length){
                break;
            }

            uint8_t* payload = frame + offset;
            if (masked){
                for (size_t i = 0; i < length; i++){
                    payload[i] ^= frame[maskOffset + (i % 4)];
                }
            }

            if (opcode <= opcodeBinary){
                if (connection.message.size() + length > maxMessageSize){
                    return false;
                }
                connection.message.insert(connection.message.end(), payload, payload + length);
                if (fin){
                    events.push_back({NetworkEventType::RECEIVED, id, std::move(connection.message)});
                    connection.message.clear();
                }
            }else if (opcode == opcodePing){
                queueFrame(connection, opcodePong, payload, (size_t)length);
            }else if (opcode != opcodePong){
                return false;
            }

            consumed += offset + (size_t)length;
        }

        in.erase(in.begin(), in.begin() + consumed);

        return true;
    }

    // false when the connection ended
    bool service(Connection& connection, uint32_t id, std::vector<NetworkEvent>& events){
        if (connection.failed){
            return false;
        }
        if (connection.connecting){
            int result = getConnectResult(connection.socket);
            if (result <= 0){
                return result == 0;
            }
            connection.connecting = false;
            connection.out.insert(connection.out.end(), connection.request.begin(), connection.request.end());
        }

        if (!flushOut(connection)){
            return false;
        }
        char buffer[16384];

        if (connection.closing){
            if (connection.out.empty() && !connection.shut){
                shutdown(connection.socket, shutdownSend);
                connection.shut = true;
            }
            // closing with unread data would reset what the peer has not read yet
            int result;
            do{
                result = (int)recv(connection.socket, buffer, sizeof(buffer), 0);
            }while (result > 0);

            return result < 0 && wouldBlock() && std::chrono::steady_clock::now() < connection.closeDeadline;
        }

        while (true){
            int result = (int)recv(connection.socket, buffer, sizeof(buffer), 0);
            if (result > 0){
                connection.in.insert(connection.in.end(), buffer, buffer + result);
                if (connection.in.size() > maxMessageSize + maxHeaderSize){
                    break;
                }
            }else if (result < 0 && wouldBlock()){
                break;
            }else{
                return false;
            }
        }

        if (!connection.open){
            const char terminator[] = "\r\n\r\n";
            auto end = std::search(connection.in.begin(), connection.in.end(), terminator, terminator + 4);
            if (end == connection.in.end()){
                return connection.in.size() <= maxHeaderSize;
            }

            std::string headers(connection.in.begin(), end + 2);
            connection.in.erase(connection.in.begin(), end + 4);
            if (!openConnection(connection, headers)){
                return false;
            }
            connection.open = true;
            events.push_back({NetworkEventType::CONNECTED, id, {}});
        }

        if (!readFrames(connection, id, events)){
            return false;
        }

        return flushOut(connection);
    }

    void acceptConnections(){
        while (listener != invalidSocket){
            SocketHandle socket = accept(listener, NULL, NULL);
            if (socket == invalidSocket){
                break;
            }
            if ((int)connections.size() >= maxConnections){
                closeSocket(socket);
                continue;
            }

            prepareSocket(socket);
            connections[++lastConnection].socket = socket;
        }
    }
};

WebSocketTransport::WebSocketTransport(){
    state = std::make_unique<State>();
}

WebSocketTransport::~WebSocketTransport(){
    close();
}

bool WebSocketTransport::listen(uint16_t port, int maxConnections){
    close();

    if (!startSockets()){
        return false;
    }

    // one socket for IPv6 and IPv4 where the system has both
    bool ipv6 = true;
    SocketHandle socket = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (socket == invalidSocket){
        ipv6 = false;
        socket = ::socket(AF_INET, SOCK_STREAM, 0);
    }
    if (socket == invalidSocket){
        return false;
    }

    int on = 1;
    int off = 0;
#ifndef _WIN32
    setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));
#endif

    int bound;
    if (ipv6){
        setsockopt(socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&off, sizeof(off));

        sockaddr_in6 address = {};
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_any;
        address.sin6_port = htons(port);
        bound = bind(socket, (sockaddr*)&address, sizeof(address));
    }else{
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        bound = bind(socket, (sockaddr*)&address, sizeof(address));
    }

    if (bound != 0 || ::listen(socket, 16) != 0){
        Log::error("Network: can't open TCP port %u", (unsigned int)port);
        closeSocket(socket);
        return false;
    }

    prepareSocket(socket);
    state->listener = socket;
    state->maxConnections = maxConnections;

    return true;
}

bool WebSocketTransport::connect(const std::string& address, uint16_t port){
    close();

    if (!startSockets()){
        return false;
    }

    std::string host = address;
    std::string path = "/";
    if (host.compare(0, 6, "wss://") == 0){
        Log::error("Network: wss:// needs a web build, others have no TLS");
        return false;
    }
    if (host.compare(0, 5, "ws://") == 0){
        host.erase(0, 5);
    }
    size_t slash = host.find('/');
    if (slash != std::string::npos){
        path = host.substr(slash);
        host.erase(slash);
    }

    // host:port, or [IPv6]:port
    std::string service = std::to_string(port);
    size_t colon = host.rfind(':');
    if (!host.empty() && host[0] == '['){
        size_t bracket = host.find(']');
        if (bracket == std::string::npos){
            return false;
        }
        if (bracket + 1 < host.size() && host[bracket + 1] == ':'){
            service = host.substr(bracket + 2);
        }
        host = host.substr(1, bracket - 1);
    }else if (colon != std::string::npos && host.find(':') == colon){
        service = host.substr(colon + 1);
        host.erase(colon);
    }

    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = NULL;
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &list) != 0){
        Log::error("Network: unknown host %s", host.c_str());
        return false;
    }

    SocketHandle socket = invalidSocket;
    for (addrinfo* info = list; info; info = info->ai_next){
        socket = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);
        if (socket == invalidSocket){
            continue;
        }
        prepareSocket(socket);
        if (::connect(socket, info->ai_addr, (int)info->ai_addrlen) == 0 || wouldBlock()){
            break;
        }
        closeSocket(socket);
        socket = invalidSocket;
    }
    freeaddrinfo(list);

    if (socket == invalidSocket){
        Log::error("Network: can't reach %s", host.c_str());
        return false;
    }

    uint8_t keyBytes[16];
    for (uint8_t& byte : keyBytes){
        byte = (uint8_t)state->random();
    }

    State::Connection& connection = state->connections[++state->lastConnection];
    connection.socket = socket;
    connection.client = true;
    connection.connecting = true;
    connection.key = Base64::encode(keyBytes, sizeof(keyBytes));
    connection.request = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + service +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + connection.key +
        "\r\nSec-WebSocket-Version: 13\r\n\r\n";

    return true;
}

void WebSocketTransport::close(){
    for (auto& connection : state->connections){
        closeSocket(connection.second.socket);
    }
    state->connections.clear();

    if (state->listener != invalidSocket){
        closeSocket(state->listener);
        state->listener = invalidSocket;
    }
}

void WebSocketTransport::poll(std::vector<NetworkEvent>& events){
    state->acceptConnections();

    for (auto it = state->connections.begin(); it != state->connections.end(); ){
        State::Connection& connection = it->second;
        if (state->service(connection, it->first, events)){
            ++it;
            continue;
        }

        closeSocket(connection.socket);
        // the session already forgot a connection it closed itself
        if (!connection.closing && (connection.open || connection.client)){
            events.push_back({NetworkEventType::DISCONNECTED, it->first, {}});
        }
        it = state->connections.erase(it);
    }
}

bool WebSocketTransport::send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery){
    auto it = state->connections.find(connection);
    if (it == state->connections.end() || !it->second.open || it->second.closing){
        return false;
    }
    if (size > maxMessageSize || it->second.out.size() + size > maxPendingSize){
        // a gap would break the order, poll() ends the connection
        it->second.failed = true;
        return false;
    }

    state->queueFrame(it->second, opcodeBinary, data, size);

    return true;
}

void WebSocketTransport::flush(){
    for (auto& connection : state->connections){
        if (!connection.second.connecting){
            state->flushOut(connection.second);
        }
    }
}

void WebSocketTransport::disconnect(uint32_t connection){
    auto it = state->connections.find(connection);
    if (it == state->connections.end()){
        return;
    }

    if (it->second.open && !it->second.closing){
        const uint8_t normal[2] = {0x03, 0xE8};
        state->queueFrame(it->second, opcodeClose, normal, sizeof(normal));
    }
    it->second.closing = true;
    it->second.closeDeadline = std::chrono::steady_clock::now() + closeTimeout;
}
