// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ENetTransport.h"

#include "Log.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define ENET_IMPLEMENTATION
#include "enet/enet.h"

using namespace doriax;

namespace {
    // reliable and unreliable messages don't wait for each other
    const int channelCount = 2;

    int hostCount = 0;
}

ENetTransport::~ENetTransport(){
    close();
}

bool ENetTransport::create(uint16_t port, int maxConnections, bool server){
    close();

    if (hostCount == 0 && enet_initialize() != 0){
        Log::error("Network: failed to initialize sockets");
        return false;
    }
    hostCount++;

    ENetAddress address = {};
    address.host = ENET_HOST_ANY;
    address.port = port;

    host = enet_host_create(server ? &address : NULL, (size_t)maxConnections, channelCount, 0, 0);
    if (!host){
        close();
        return false;
    }

    return true;
}

bool ENetTransport::listen(uint16_t port, int maxConnections){
    if (!create(port, maxConnections, true)){
        Log::error("Network: can't open UDP port %u", (unsigned int)port);
        return false;
    }

    return true;
}

bool ENetTransport::connect(const std::string& address, uint16_t port){
    if (!create(0, 1, false)){
        Log::error("Network: can't open a UDP socket");
        return false;
    }

    ENetAddress remote = {};
    remote.port = port;
    if (enet_address_set_host(&remote, address.c_str()) != 0){
        Log::error("Network: unknown host %s", address.c_str());
        close();
        return false;
    }

    ENetPeer* peer = enet_host_connect(host, &remote, channelCount, 0);
    if (!peer){
        close();
        return false;
    }

    uint32_t connection = ++lastConnection;
    peer->data = (void*)(uintptr_t)connection;
    peers[connection] = peer;

    return true;
}

void ENetTransport::close(){
    if (host){
        for (auto& peer : peers){
            enet_peer_disconnect_now(peer.second, 0);
        }
        enet_host_flush(host);
        enet_host_destroy(host);
        host = nullptr;

        hostCount--;
        if (hostCount == 0){
            enet_deinitialize();
        }
    }
    peers.clear();
}

void ENetTransport::poll(std::vector<NetworkEvent>& events){
    if (!host){
        return;
    }

    ENetEvent event;
    while (enet_host_service(host, &event, 0) > 0){
        uint32_t connection = (uint32_t)(uintptr_t)event.peer->data;

        if (event.type == ENET_EVENT_TYPE_CONNECT){
            if (connection == 0){
                connection = ++lastConnection;
                event.peer->data = (void*)(uintptr_t)connection;
                peers[connection] = event.peer;
            }
            events.push_back({NetworkEventType::CONNECTED, connection, {}});
        }else if (event.type == ENET_EVENT_TYPE_RECEIVE){
            NetworkEvent received{NetworkEventType::RECEIVED, connection, {}};
            received.data.assign(event.packet->data, event.packet->data + event.packet->dataLength);
            events.push_back(std::move(received));
            enet_packet_destroy(event.packet);
        }else if (event.type == ENET_EVENT_TYPE_DISCONNECT || event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT){
            event.peer->data = NULL;
            if (peers.erase(connection)){
                events.push_back({NetworkEventType::DISCONNECTED, connection, {}});
            }
        }
    }
}

bool ENetTransport::send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery){
    auto it = peers.find(connection);
    if (it == peers.end()){
        return false;
    }

    bool reliable = (delivery == NetworkDelivery::RELIABLE);
    ENetPacket* packet = enet_packet_create(data, size, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet){
        return false;
    }

    if (enet_peer_send(it->second, reliable ? 0 : 1, packet) != 0){
        enet_packet_destroy(packet);
        return false;
    }

    return true;
}

void ENetTransport::flush(){
    if (host){
        enet_host_flush(host);
    }
}

void ENetTransport::disconnect(uint32_t connection){
    auto it = peers.find(connection);
    if (it != peers.end()){
        enet_peer_disconnect_later(it->second, 0);
    }
}
