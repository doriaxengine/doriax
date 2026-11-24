// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "NetworkSession.h"

#include "Log.h"

#include <algorithm>
#include <chrono>
#include <random>

using namespace doriax;

namespace {
    // "DRXN", so another program's server is refused
    const uint32_t magic = 0x4E585244;
    // of the messages below, bumped when they change
    const uint16_t protocol = 1;

    const double connectTimeout = 10.0;
    const double handshakeTimeout = 5.0;
    const double pingInterval = 1.0;
    const size_t maxRpcSize = 1000000;
    // connections past maxPeers, to tell them the server is full
    const int spareConnections = 8;

    enum Message : uint8_t {
        HELLO = 1,
        WELCOME,
        REJECT,
        RPC,
        PING,
        PONG,
        REPLICATION
    };

    enum RejectReason : uint8_t {
        REJECT_PROTOCOL = 1,
        REJECT_VERSION,
        REJECT_FULL
    };

    float getRandom(){
        static std::mt19937 generator{std::random_device{}()};

        return std::uniform_real_distribution<float>(0.0f, 1.0f)(generator);
    }

    // FNV-1a, an RPC travels as the hash of its name
    uint32_t hashName(const std::string& name){
        uint32_t hash = 2166136261u;
        for (unsigned char c : name){
            hash = (hash ^ c) * 16777619u;
        }

        return hash;
    }
}

double NetworkSession::getTime(){
    static const auto start = std::chrono::steady_clock::now();

    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

NetworkSession::NetworkSession(){
}

NetworkSession::~NetworkSession(){
    close();
}

NetworkSession::Peer* NetworkSession::findPeer(NetworkTransport* transport, uint32_t connection){
    for (Peer& peer : peers){
        if (peer.transport == transport && peer.connection == connection){
            return &peer;
        }
    }

    return nullptr;
}

NetworkSession::Peer* NetworkSession::findPeer(int id){
    for (Peer& peer : peers){
        if (peer.id == id && id != 0){
            return &peer;
        }
    }

    return nullptr;
}

void NetworkSession::removePeer(NetworkTransport* transport, uint32_t connection){
    peers.erase(std::remove_if(peers.begin(), peers.end(), [&](const Peer& peer){
        return peer.transport == transport && peer.connection == connection;
    }), peers.end());
}

int NetworkSession::countPeers() const{
    int count = 0;
    for (const Peer& peer : peers){
        if (peer.id != 0){
            count++;
        }
    }

    return count;
}

void NetworkSession::sendTo(const Peer& peer, const Packet& packet, NetworkDelivery delivery){
    if (latency <= 0 && jitter <= 0 && loss <= 0){
        peer.transport->send(peer.connection, packet.getData(), packet.getSize(), delivery);
        return;
    }

    // a lost reliable message is sent again, so it is only late
    if (delivery == NetworkDelivery::UNRELIABLE && getRandom() < loss){
        return;
    }

    double time = getTime() + (latency + jitter * (getRandom() * 2.0f - 1.0f)) / 1000.0;
    if (delivery == NetworkDelivery::RELIABLE){
        time = std::max(time, lastDelayedTime);
        lastDelayedTime = time;
    }

    Delayed message{time, peer.transport, peer.connection, delivery, {}};
    message.data.assign(packet.getData(), packet.getData() + packet.getSize());
    delayed.push_back(std::move(message));
}

void NetworkSession::fail(const std::string& reason){
    close();
    if (onConnectionFailed){
        onConnectionFailed(reason);
    }
}

void NetworkSession::reject(const Peer& peer, uint8_t reason){
    Packet packet;
    packet.writeUInt8(REJECT);
    packet.writeUInt8(reason);
    peer.transport->send(peer.connection, packet.getData(), packet.getSize(), NetworkDelivery::RELIABLE);
    peer.transport->disconnect(peer.connection);

    removePeer(peer.transport, peer.connection);
}

void NetworkSession::handleEvent(NetworkTransport* transport, NetworkEvent& event){
    Peer* peer = findPeer(transport, event.connection);

    if (event.type == NetworkEventType::CONNECTED){
        if (peer){
            return;
        }
        peers.push_back({0, transport, event.connection, getTime(), 0});

        if (state == NetworkState::CONNECTING){
            Packet hello;
            hello.writeUInt8(HELLO);
            hello.writeUInt32(magic);
            hello.writeUInt16(protocol);
            hello.writeString(version);
            sendTo(peers.back(), hello, NetworkDelivery::RELIABLE);
        }
    }else if (event.type == NetworkEventType::DISCONNECTED){
        if (state == NetworkState::CONNECTING){
            fail("unreachable");
        }else if (state == NetworkState::CONNECTED){
            close();
            if (onDisconnected){
                onDisconnected();
            }
        }else if (peer){
            int peerId = peer->id;
            removePeer(transport, event.connection);
            if (peerId != 0 && onPeerDisconnected){
                onPeerDisconnected(peerId);
            }
        }
    }else if (peer && !event.data.empty()){
        // a callback may change the peer list
        Peer sender = *peer;
        Packet packet(event.data.data(), event.data.size());
        handleMessage(sender, packet);
    }
}

void NetworkSession::handleMessage(const Peer& peer, Packet& packet){
    uint8_t type = packet.readUInt8();

    if (state == NetworkState::HOSTING && peer.id == 0){
        if (type != HELLO || packet.readUInt32() != magic || packet.readUInt16() != protocol){
            reject(peer, REJECT_PROTOCOL);
        }else if (packet.readString() != version || !packet.isValid()){
            reject(peer, REJECT_VERSION);
        }else if (countPeers() >= maxPeers){
            reject(peer, REJECT_FULL);
        }else{
            Peer* stored = findPeer(peer.transport, peer.connection);
            int peerId = ++lastPeerId;
            stored->id = peerId;

            Packet welcome;
            welcome.writeUInt8(WELCOME);
            welcome.writeUInt32((uint32_t)peerId);
            sendTo(*stored, welcome, NetworkDelivery::RELIABLE);

            if (onPeerConnected){
                onPeerConnected(peerId);
            }
        }
        return;
    }

    if (state == NetworkState::CONNECTING){
        if (type == WELCOME){
            int assigned = (int)packet.readUInt32();
            Peer* server = findPeer(peer.transport, peer.connection);
            if (!packet.isValid() || assigned <= SERVER || !server){
                fail("protocol");
                return;
            }

            id = assigned;
            server->id = SERVER;
            state = NetworkState::CONNECTED;
            if (onConnected){
                onConnected();
            }
        }else{
            uint8_t reason = (type == REJECT) ? packet.readUInt8() : (uint8_t)REJECT_PROTOCOL;
            fail(reason == REJECT_VERSION ? "version" : (reason == REJECT_FULL ? "full" : "protocol"));
        }
        return;
    }

    if (type == RPC){
        callRpc(peer.id, packet);
    }else if (type == REPLICATION){
        if (onReplication){
            onReplication(peer.id, packet);
        }
    }else if (type == PING){
        Packet pong;
        pong.writeUInt8(PONG);
        pong.writeUInt32(packet.readUInt32());
        if (packet.isValid()){
            sendTo(peer, pong, NetworkDelivery::UNRELIABLE);
        }
    }else if (type == PONG){
        uint32_t elapsed = (uint32_t)(getTime() * 1000.0) - packet.readUInt32();
        Peer* stored = findPeer(peer.transport, peer.connection);
        if (packet.isValid() && stored && elapsed < 60000){
            stored->ping = (stored->ping > 0) ? stored->ping * 0.8f + (float)elapsed * 0.2f : (float)elapsed;
        }
    }
}

void NetworkSession::callRpc(int sender, Packet& packet){
    uint32_t hash = packet.readUInt32();

    auto it = rpcs.find(hash);
    if (!packet.isValid() || it == rpcs.end()){
        Log::warn("Network: peer %i called an RPC that is not registered here", sender);
        return;
    }

    // a copy, the handler may unregister itself
    RpcHandler handler = it->second.handler;
    handler(sender, packet);
}

bool NetworkSession::host(std::vector<std::unique_ptr<NetworkTransport>> transports, uint16_t port, int maxPeers){
    close();

    for (auto& transport : transports){
        if (!transport->listen(port, maxPeers + spareConnections)){
            return false;
        }
    }

    this->transports = std::move(transports);
    this->maxPeers = maxPeers;
    state = NetworkState::HOSTING;
    id = SERVER;
    since = getTime();

    return true;
}

bool NetworkSession::connect(std::unique_ptr<NetworkTransport> transport, const std::string& address, uint16_t port){
    close();

    if (!transport->connect(address, port)){
        return false;
    }

    transports.push_back(std::move(transport));
    state = NetworkState::CONNECTING;
    since = getTime();

    return true;
}

void NetworkSession::close(){
    generation++;

    for (auto& transport : transports){
        transport->close();
    }
    transports.clear();
    peers.clear();
    delayed.clear();
    localCalls.clear();

    state = NetworkState::OFFLINE;
    id = 0;
    lastPeerId = SERVER;
    lastPing = 0;
}

void NetworkSession::disconnect(int peer){
    Peer* found = (state == NetworkState::HOSTING) ? findPeer(peer) : nullptr;
    if (!found){
        Log::error("Network: no peer %i to disconnect", peer);
        return;
    }

    found->transport->disconnect(found->connection);
    removePeer(found->transport, found->connection);
    if (onPeerDisconnected){
        onPeerDisconnected(peer);
    }
}

void NetworkSession::update(){
    if (state == NetworkState::OFFLINE){
        return;
    }

    unsigned int current = generation;

    std::vector<std::pair<NetworkTransport*, NetworkEvent>> events;
    std::vector<NetworkEvent> batch;
    for (auto& transport : transports){
        batch.clear();
        transport->poll(batch);
        for (NetworkEvent& event : batch){
            events.emplace_back(transport.get(), std::move(event));
        }
    }

    for (auto& event : events){
        handleEvent(event.first, event.second);
        if (generation != current){
            return;
        }
    }

    std::vector<std::vector<uint8_t>> calls;
    calls.swap(localCalls);
    for (auto& call : calls){
        Packet packet(call.data(), call.size());
        callRpc(id, packet);
        if (generation != current){
            return;
        }
    }

    double now = getTime();

    if (state == NetworkState::CONNECTING && now - since > connectTimeout){
        fail("timeout");
        return;
    }

    for (size_t i = 0; i < peers.size(); ){
        if (state == NetworkState::HOSTING && peers[i].id == 0 && now - peers[i].since > handshakeTimeout){
            peers[i].transport->disconnect(peers[i].connection);
            peers.erase(peers.begin() + i);
        }else{
            i++;
        }
    }

    if (now - lastPing >= pingInterval){
        lastPing = now;

        Packet ping;
        ping.writeUInt8(PING);
        ping.writeUInt32((uint32_t)(now * 1000.0));
        for (const Peer& peer : peers){
            if (peer.id != 0){
                sendTo(peer, ping, NetworkDelivery::UNRELIABLE);
            }
        }
    }

    flush();
}

void NetworkSession::flush(){
    if (!delayed.empty()){
        double now = getTime();
        size_t kept = 0;
        for (size_t i = 0; i < delayed.size(); i++){
            Delayed& message = delayed[i];
            if (message.time <= now){
                message.transport->send(message.connection, message.data.data(), message.data.size(), message.delivery);
            }else{
                if (kept != i){
                    delayed[kept] = std::move(message);
                }
                kept++;
            }
        }
        delayed.resize(kept);
    }

    for (auto& transport : transports){
        transport->flush();
    }
}

NetworkState NetworkSession::getState() const{
    return state;
}

int NetworkSession::getId() const{
    return id;
}

std::vector<int> NetworkSession::getPeers() const{
    std::vector<int> ids;
    for (const Peer& peer : peers){
        if (peer.id != 0){
            ids.push_back(peer.id);
        }
    }

    return ids;
}

float NetworkSession::getPing(int peer) const{
    for (const Peer& found : peers){
        if (found.id == peer && peer != 0){
            return found.ping;
        }
    }

    return 0;
}

void NetworkSession::setVersion(const std::string& version){
    this->version = version;
}

void NetworkSession::setSimulation(float latency, float jitter, float loss){
    this->latency = std::max(latency, 0.0f);
    this->jitter = std::max(jitter, 0.0f);
    this->loss = std::max(loss, 0.0f);
}

bool NetworkSession::registerRpc(const std::string& name, RpcHandler handler, const std::string& tag){
    uint32_t hash = hashName(name);

    auto it = rpcs.find(hash);
    if (it != rpcs.end() && it->second.name != name){
        Log::error("Network: RPC %s has the hash of %s, rename one of them", name.c_str(), it->second.name.c_str());
        return false;
    }

    rpcs[hash] = {name, tag, std::move(handler)};

    return true;
}

void NetworkSession::unregisterRpc(const std::string& name){
    rpcs.erase(hashName(name));
}

void NetworkSession::removeRpcsByTag(const std::string& substring){
    for (auto it = rpcs.begin(); it != rpcs.end(); ){
        if (it->second.tag.find(substring) != std::string::npos){
            it = rpcs.erase(it);
        }else{
            ++it;
        }
    }
}

void NetworkSession::clearRpcs(){
    rpcs.clear();
}

bool NetworkSession::rpc(int target, const std::string& name, const Packet& values, NetworkDelivery delivery){
    if (state != NetworkState::CONNECTED && state != NetworkState::HOSTING){
        Log::error("Network: RPC %s without a connection", name.c_str());
        return false;
    }
    if (values.getSize() > maxRpcSize){
        Log::error("Network: RPC %s is too large", name.c_str());
        return false;
    }

    Packet packet;
    packet.writeUInt8(RPC);
    packet.writeUInt32(hashName(name));
    packet.writeBytes(values.getData(), values.getSize());

    if (target == id){
        localCalls.emplace_back(packet.getData() + 1, packet.getData() + packet.getSize());
        return true;
    }

    if (target == ALL){
        for (const Peer& peer : peers){
            if (peer.id != 0){
                sendTo(peer, packet, delivery);
            }
        }
        return true;
    }

    Peer* peer = findPeer(target);
    if (!peer){
        Log::error("Network: peer %i is not connected", target);
        return false;
    }
    sendTo(*peer, packet, delivery);

    return true;
}

bool NetworkSession::send(int target, const Packet& data, NetworkDelivery delivery){
    Peer* peer = findPeer(target);
    if (!peer){
        return false;
    }

    Packet packet;
    packet.writeUInt8(REPLICATION);
    packet.writeBytes(data.getData(), data.getSize());
    sendTo(*peer, packet, delivery);

    return true;
}
