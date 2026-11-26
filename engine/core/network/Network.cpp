// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "Network.h"

#include "Scene.h"
#include "subsystem/NetworkSystem.h"
#include "WebSocketTransport.h"
#ifndef __EMSCRIPTEN__
#include "ENetTransport.h"
#endif

using namespace doriax;

FunctionSubscribe<void(int)> Network::onPeerConnected;
FunctionSubscribe<void(int)> Network::onPeerDisconnected;
FunctionSubscribe<void()> Network::onConnected;
FunctionSubscribe<void(std::string)> Network::onConnectionFailed;
FunctionSubscribe<void()> Network::onDisconnected;

NetworkSession& Network::getSession(){
    static NetworkSession session;
    static bool started = false;

    if (!started){
        started = true;
        session.onPeerConnected = [](int peer){ onPeerConnected.call(peer); };
        session.onPeerDisconnected = [](int peer){ onPeerDisconnected.call(peer); };
        session.onConnected = [](){ onConnected.call(); };
        session.onConnectionFailed = [](const std::string& reason){ onConnectionFailed.call(reason); };
        session.onDisconnected = [](){ onDisconnected.call(); };
    }

    return session;
}

void Network::update(){
    getSession().update();
}

void Network::flush(){
    getSession().flush();
}

void Network::clearSubscriptions(){
    onPeerConnected.clear();
    onPeerDisconnected.clear();
    onConnected.clear();
    onConnectionFailed.clear();
    onDisconnected.clear();

    getSession().clearRpcs();
    NetworkSystem::removeSyncsByTag("");
}

void Network::removeSubscriptionsByTag(const std::string& substring){
    onPeerConnected.removeByTagSubstring(substring);
    onPeerDisconnected.removeByTagSubstring(substring);
    onConnected.removeByTagSubstring(substring);
    onConnectionFailed.removeByTagSubstring(substring);
    onDisconnected.removeByTagSubstring(substring);

    getSession().removeRpcsByTag(substring);
    NetworkSystem::removeSyncsByTag(substring);
}

void Network::reset(){
    getSession().close();
    getSession().setVersion("");
    getSession().setSimulation(0, 0, 0);
}

bool Network::host(uint16_t port, int maxPeers){
#ifdef __EMSCRIPTEN__
    Log::error("Network: a browser can't host, only hostOffline");
    return false;
#else
    std::vector<std::unique_ptr<NetworkTransport>> transports;
    transports.push_back(std::make_unique<ENetTransport>());
    transports.push_back(std::make_unique<WebSocketTransport>());

    return getSession().host(std::move(transports), port, maxPeers);
#endif
}

bool Network::hostOffline(){
    return getSession().host({}, 0, 0);
}

bool Network::connect(const std::string& address, uint16_t port){
    bool url = address.find("://") != std::string::npos;
    if (!url && port == 0){
        Log::error("Network: no port to connect to %s", address.c_str());
        return false;
    }

#ifndef __EMSCRIPTEN__
    if (!url){
        return getSession().connect(std::make_unique<ENetTransport>(), address, port);
    }
#endif

    return getSession().connect(std::make_unique<WebSocketTransport>(), address, port);
}

void Network::close(){
    getSession().close();
}

void Network::disconnect(int peer){
    getSession().disconnect(peer);
}

NetworkState Network::getState(){
    return getSession().getState();
}

bool Network::isServer(){
    return getSession().getState() == NetworkState::HOSTING;
}

bool Network::isConnected(){
    return getSession().getState() == NetworkState::CONNECTED;
}

int Network::getId(){
    return getSession().getId();
}

std::vector<int> Network::getPeers(){
    return getSession().getPeers();
}

float Network::getPing(int peer){
    return getSession().getPing(peer);
}

void Network::setVersion(const std::string& version){
    getSession().setVersion(version);
}

void Network::setSimulation(float latency, float jitter, float loss){
    getSession().setSimulation(latency, jitter, loss);
}

void Network::registerRawRpc(const std::string& name, NetworkSession::RpcHandler handler, const std::string& tag){
    getSession().registerRpc(name, std::move(handler), tag);
}

void Network::unregisterRpc(const std::string& name){
    getSession().unregisterRpc(name);
}

bool Network::sendRpc(int target, const std::string& name, const Packet& values, NetworkDelivery delivery){
    return getSession().rpc(target, name, values, delivery);
}

Entity Network::spawn(const std::string& bundle, Scene* scene, int owner){
    return scene ? scene->getSystem<NetworkSystem>()->spawn(bundle, owner) : NULL_ENTITY;
}

int Network::getOwner(Scene* scene, Entity entity){
    return scene ? scene->getSystem<NetworkSystem>()->getOwner(entity) : 0;
}

bool Network::isOwner(Scene* scene, Entity entity){
    int owner = getOwner(scene, entity);

    return owner != 0 && owner == getId();
}

void Network::syncRaw(Scene* scene, Entity entity, const std::string& name, std::function<void(Packet&)> read, std::function<void(Packet&)> write, const std::string& tag){
    if (scene){
        scene->getSystem<NetworkSystem>()->sync(entity, name, std::move(read), std::move(write), tag);
    }
}
