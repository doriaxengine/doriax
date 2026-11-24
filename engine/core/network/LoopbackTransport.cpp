// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LoopbackTransport.h"

using namespace doriax;

std::map<uint16_t, LoopbackTransport*>& LoopbackTransport::getListeners(){
    static std::map<uint16_t, LoopbackTransport*> listeners;

    return listeners;
}

LoopbackTransport::~LoopbackTransport(){
    close();
}

bool LoopbackTransport::listen(uint16_t port, int maxConnections){
    if (getListeners().count(port)){
        return false;
    }

    close();
    getListeners()[port] = this;
    listenPort = port;
    this->maxConnections = maxConnections;

    return true;
}

bool LoopbackTransport::connect(const std::string& address, uint16_t port){
    close();

    uint32_t connection = ++lastConnection;

    auto it = getListeners().find(port);
    if (it == getListeners().end() || (int)it->second->links.size() >= it->second->maxConnections){
        pending.push_back({NetworkEventType::DISCONNECTED, connection, {}});
        return true;
    }

    LoopbackTransport* server = it->second;
    uint32_t serverConnection = ++server->lastConnection;

    links[connection] = {server, serverConnection};
    server->links[serverConnection] = {this, connection};

    pending.push_back({NetworkEventType::CONNECTED, connection, {}});
    server->pending.push_back({NetworkEventType::CONNECTED, serverConnection, {}});

    return true;
}

void LoopbackTransport::drop(uint32_t connection, bool notify){
    auto it = links.find(connection);
    if (it == links.end()){
        return;
    }

    Link link = it->second;
    links.erase(it);
    link.remote->links.erase(link.remoteConnection);
    link.remote->pending.push_back({NetworkEventType::DISCONNECTED, link.remoteConnection, {}});

    if (notify){
        pending.push_back({NetworkEventType::DISCONNECTED, connection, {}});
    }
}

void LoopbackTransport::close(){
    while (!links.empty()){
        drop(links.begin()->first, false);
    }
    pending.clear();

    if (listenPort != 0){
        getListeners().erase(listenPort);
        listenPort = 0;
    }
}

void LoopbackTransport::poll(std::vector<NetworkEvent>& events){
    for (NetworkEvent& event : pending){
        events.push_back(std::move(event));
    }
    pending.clear();
}

bool LoopbackTransport::send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery){
    auto it = links.find(connection);
    if (it == links.end()){
        return false;
    }

    NetworkEvent event{NetworkEventType::RECEIVED, it->second.remoteConnection, {}};
    event.data.assign(data, data + size);
    it->second.remote->pending.push_back(std::move(event));

    return true;
}

void LoopbackTransport::flush(){
}

void LoopbackTransport::disconnect(uint32_t connection){
    drop(connection, true);
}
