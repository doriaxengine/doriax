// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "NetworkSystem.h"

#include "Log.h"
#include "Scene.h"
#include "component/Transform.h"
#include "manager/BundleManager.h"
#include "manager/SceneManager.h"
#include "network/Network.h"

#include <algorithm>
#include <cmath>

using namespace doriax;

namespace {
    // smaller ids are the entities saved with a scene
    const unsigned int firstSpawnId = 0x1000000;
    const size_t maxStates = 32;
    // an unreliable message has to fit in one UDP packet
    const size_t maxStateSize = 1100;
    // a clock that jumped is taken again from the next message
    const double maxOffsetError = 0.25;

    enum Message : uint8_t {
        JOIN = 1,
        LEAVE,
        SPAWN,
        DESPAWN,
        STATE
    };

    enum StateFlags : uint8_t {
        POSITION = 1,
        ROTATION = 2,
        SCALE = 4
    };

    // scenes may be created before main, in any order
    std::vector<NetworkSystem*>& getSystems(){
        static std::vector<NetworkSystem*> systems;

        return systems;
    }
}

NetworkSystem::NetworkSystem(Scene* scene): SubSystem(scene){
    signature.set(scene->getComponentId<NetworkComponent>());

    lastNetworkId = firstSpawnId;
    getSystems().push_back(this);
    setSession(&Network::getSession());
}

NetworkSystem::~NetworkSystem(){
    std::vector<NetworkSystem*>& systems = getSystems();
    systems.erase(std::remove(systems.begin(), systems.end(), this), systems.end());
}

void NetworkSystem::receive(NetworkSession* session, int sender, Packet& packet){
    uint32_t sceneId = packet.readUInt32();
    uint8_t type = packet.readUInt8();

    for (NetworkSystem* system : getSystems()){
        if (system->session != session || system->getSceneId() != sceneId){
            continue;
        }

        if (system->isServer()){
            if (type == JOIN){
                system->receiveJoin(sender);
            }else if (type == LEAVE){
                system->subscribers.erase(sender);
            }
        }else if (sender == NetworkSession::SERVER){
            if (type == SPAWN){
                system->receiveSpawn(packet);
            }else if (type == DESPAWN){
                system->receiveDespawn(packet);
            }else if (type == STATE){
                system->receiveState(packet);
            }
        }
        return;
    }
}

uint32_t NetworkSystem::getSceneId() const{
    return (sceneId != 0) ? sceneId : SceneManager::getScenePtrId(scene);
}

bool NetworkSystem::isServer() const{
    return session->getState() == NetworkState::HOSTING;
}

NetworkSystem::State NetworkSystem::readState(Entity entity) const{
    State state;

    Transform* transform = scene->findComponent<Transform>(entity);
    if (transform){
        state.position = transform->position;
        state.rotation = transform->rotation;
        state.scale = transform->scale;
    }

    return state;
}

void NetworkSystem::applyState(Entity entity, const NetworkComponent& component, const State& state){
    Transform* transform = scene->findComponent<Transform>(entity);
    if (!transform){
        return;
    }

    if (component.syncPosition){
        transform->position = state.position;
    }
    if (component.syncRotation){
        transform->rotation = state.rotation;
    }
    if (component.syncScale){
        transform->scale = state.scale;
    }
    transform->needUpdate = true;
}

void NetworkSystem::writeState(Packet& packet, unsigned int id, const State& state, const NetworkComponent& component) const{
    uint8_t flags = 0;
    if (component.syncPosition){
        flags |= POSITION;
    }
    if (component.syncRotation){
        flags |= ROTATION;
    }
    if (component.syncScale){
        flags |= SCALE;
    }

    packet.writeUInt32(id);
    packet.writeUInt8(flags);
    if (flags & POSITION){
        packet.writeVector3(state.position);
    }
    if (flags & ROTATION){
        packet.writeQuaternion(state.rotation);
    }
    if (flags & SCALE){
        packet.writeVector3(state.scale);
    }
}

void NetworkSystem::registerAdded(){
    for (Entity entity : added){
        NetworkComponent* component = scene->findComponent<NetworkComponent>(entity);
        // a spawned bundle was registered by bundleCreated
        if (component && !ids.count(entity)){
            component->networkId = entity;
            registerReplica(entity, *component, 0);
        }
    }
    added.clear();
}

void NetworkSystem::registerReplica(Entity entity, NetworkComponent& component, uint32_t bundle){
    auto it = replicas.find(component.networkId);
    if (it != replicas.end()){
        Log::error("Network: entities %u and %u have the network id %u", it->second.entity, entity, component.networkId);
        return;
    }

    Replica& replica = replicas[component.networkId];
    replica.entity = entity;
    replica.bundle = bundle;
    replica.sent = readState(entity);
    ids[entity] = component.networkId;
}

void NetworkSystem::removeSpawned(){
    std::vector<Entity> spawned;
    for (auto& entry : replicas){
        entry.second.states.clear();
        if (entry.second.bundle != 0){
            spawned.push_back(entry.second.entity);
        }
    }

    for (Entity entity : spawned){
        BundleManager::destroyBundle(scene, entity);
    }
}

Packet NetworkSystem::createMessage(uint8_t type) const{
    Packet packet;
    packet.writeUInt32(getSceneId());
    packet.writeUInt8(type);

    return packet;
}

Packet NetworkSystem::createState() const{
    Packet packet = createMessage(STATE);
    packet.writeUInt32((uint32_t)(NetworkSession::getTime() * 1000.0));

    return packet;
}

void NetworkSystem::sendToSubscribers(const Packet& packet, NetworkDelivery delivery){
    for (int peer : subscribers){
        session->send(peer, packet, delivery);
    }
}

Packet NetworkSystem::createSpawn(unsigned int id, const Replica& replica) const{
    const NetworkComponent& component = scene->getComponent<NetworkComponent>(replica.entity);
    State state = readState(replica.entity);

    Packet packet = createMessage(SPAWN);
    packet.writeUInt32(id);
    packet.writeUInt32(replica.bundle);
    packet.writeInt(component.owner);
    packet.writeVector3(state.position);
    packet.writeQuaternion(state.rotation);
    packet.writeVector3(state.scale);

    return packet;
}

void NetworkSystem::updateServer(double dt){
    std::vector<int> peers = session->getPeers();
    for (auto it = subscribers.begin(); it != subscribers.end(); ){
        if (std::find(peers.begin(), peers.end(), *it) == peers.end()){
            it = subscribers.erase(it);
        }else{
            ++it;
        }
    }

    sendTime += dt;
    if (sendTime < 1.0 / sendRate){
        return;
    }
    sendTime = 0;

    for (unsigned int id : despawned){
        Packet packet = createMessage(DESPAWN);
        packet.writeUInt32(id);
        sendToSubscribers(packet, NetworkDelivery::RELIABLE);
    }
    despawned.clear();

    Packet moved = createState();
    Packet rested = createState();
    size_t emptySize = moved.getSize();

    for (auto& entry : replicas){
        Replica& replica = entry.second;
        const NetworkComponent& component = scene->getComponent<NetworkComponent>(replica.entity);
        State state = readState(replica.entity);

        if (replica.bundle != 0 && !replica.announced){
            replica.announced = true;
            replica.sent = state;
            sendToSubscribers(createSpawn(entry.first, replica), NetworkDelivery::RELIABLE);
            continue;
        }

        bool changed = (component.syncPosition && state.position != replica.sent.position) ||
                       (component.syncRotation && state.rotation != replica.sent.rotation) ||
                       (component.syncScale && state.scale != replica.sent.scale);

        if (changed){
            writeState(moved, entry.first, state, component);
            replica.sent = state;
            replica.moving = true;

            if (moved.getSize() > maxStateSize){
                sendToSubscribers(moved, NetworkDelivery::UNRELIABLE);
                moved = createState();
            }
        }else if (replica.moving){
            // where it stopped can't be lost
            writeState(rested, entry.first, state, component);
            replica.moving = false;
        }
    }

    if (moved.getSize() > emptySize){
        sendToSubscribers(moved, NetworkDelivery::UNRELIABLE);
    }
    if (rested.getSize() > emptySize){
        sendToSubscribers(rested, NetworkDelivery::RELIABLE);
    }
}

void NetworkSystem::updateClient(){
    bool connected = (session->getState() == NetworkState::CONNECTED);

    if (connected && !joined){
        session->send(NetworkSession::SERVER, createMessage(JOIN), NetworkDelivery::RELIABLE);
        joined = true;
    }else if (!connected && joined){
        joined = false;
        hasOffset = false;
        removeSpawned();
    }

    if (joined){
        interpolate();
    }
}

void NetworkSystem::interpolate(){
    double time = NetworkSession::getTime() - offset - interpolationDelay;

    for (auto& entry : replicas){
        std::deque<State>& states = entry.second.states;
        if (states.empty()){
            continue;
        }

        // the shown time stays between the first two
        while (states.size() > 1 && states[1].time <= time){
            states.pop_front();
        }

        State state = states[0];
        if (states.size() > 1 && time > states[0].time){
            float amount = (float)((time - states[0].time) / (states[1].time - states[0].time));
            state.position = states[0].position + (states[1].position - states[0].position) * amount;
            state.rotation = Quaternion::slerp(amount, states[0].rotation, states[1].rotation, true);
            state.scale = states[0].scale + (states[1].scale - states[0].scale) * amount;
        }
        applyState(entry.second.entity, scene->getComponent<NetworkComponent>(entry.second.entity), state);

        // arrived, the transform is left alone until the next state
        if (states.size() == 1 && time >= states[0].time){
            states.clear();
        }
    }
}

void NetworkSystem::receiveJoin(int sender){
    registerAdded();
    subscribers.insert(sender);

    Packet state = createState();
    for (auto& entry : replicas){
        Replica& replica = entry.second;
        if (replica.bundle != 0){
            session->send(sender, createSpawn(entry.first, replica), NetworkDelivery::RELIABLE);
        }else{
            writeState(state, entry.first, readState(replica.entity), scene->getComponent<NetworkComponent>(replica.entity));
        }
    }
    session->send(sender, state, NetworkDelivery::RELIABLE);
}

void NetworkSystem::receiveSpawn(Packet& packet){
    unsigned int id = packet.readUInt32();
    uint32_t bundle = packet.readUInt32();
    int owner = (int)packet.readInt();

    State state;
    state.position = packet.readVector3();
    state.rotation = packet.readQuaternion();
    state.scale = packet.readVector3();

    // sent again to a scene that joined twice
    if (!packet.isValid() || replicas.count(id)){
        return;
    }

    spawning = true;
    spawnBundle = bundle;
    spawnId = id;
    spawnOwner = owner;
    spawnState = state;
    Entity root = BundleManager::createBundle(bundle, scene);
    spawning = false;

    if (!replicas.count(id)){
        Log::error("Network: can't create the bundle %u that the server spawned", bundle);
        if (root != NULL_ENTITY){
            BundleManager::destroyBundle(scene, root);
        }
    }
}

void NetworkSystem::receiveDespawn(Packet& packet){
    auto it = replicas.find(packet.readUInt32());
    if (it != replicas.end() && it->second.bundle != 0){
        BundleManager::destroyBundle(scene, it->second.entity);
    }
}

void NetworkSystem::receiveState(Packet& packet){
    double time = packet.readUInt32() / 1000.0;

    // how far this clock is from the server's, the trip included
    double sample = NetworkSession::getTime() - time;
    if (hasOffset && std::fabs(sample - offset) < maxOffsetError){
        offset += (sample - offset) * 0.1;
    }else{
        offset = sample;
        hasOffset = true;
    }

    while (packet.isValid() && packet.getRemaining() > 0){
        unsigned int id = packet.readUInt32();
        uint8_t flags = packet.readUInt8();

        auto it = replicas.find(id);
        State state;
        if (it != replicas.end()){
            state = it->second.states.empty() ? readState(it->second.entity) : it->second.states.back();
        }

        if (flags & POSITION){
            state.position = packet.readVector3();
        }
        if (flags & ROTATION){
            state.rotation = packet.readQuaternion();
        }
        if (flags & SCALE){
            state.scale = packet.readVector3();
        }

        if (it == replicas.end() || !packet.isValid()){
            continue;
        }

        // late, a newer one is already here
        std::deque<State>& states = it->second.states;
        if (!states.empty() && time <= states.back().time){
            continue;
        }

        state.time = time;
        states.push_back(state);
        if (states.size() > maxStates){
            states.pop_front();
        }
    }
}

void NetworkSystem::setSession(NetworkSession* session){
    this->session = session;
    session->onReplication = [session](int sender, Packet& packet){ receive(session, sender, packet); };
}

void NetworkSystem::setSceneId(uint32_t sceneId){
    this->sceneId = sceneId;
}

void NetworkSystem::setSendRate(float sendRate){
    this->sendRate = sendRate;
}

float NetworkSystem::getSendRate() const{
    return sendRate;
}

void NetworkSystem::setInterpolationDelay(float interpolationDelay){
    this->interpolationDelay = interpolationDelay;
}

float NetworkSystem::getInterpolationDelay() const{
    return interpolationDelay;
}

Entity NetworkSystem::spawn(const std::string& bundle, int owner){
    if (!isServer()){
        Log::error("Network: only the server spawns, %s was not created", bundle.c_str());
        return NULL_ENTITY;
    }

    spawning = true;
    spawnBundle = BundleManager::getBundleId(bundle);
    spawnId = ++lastNetworkId;
    spawnOwner = owner;
    Entity root = BundleManager::createBundle(bundle, scene);
    spawning = false;

    return root;
}

void NetworkSystem::bundleCreated(Entity root, uint32_t bundle, bool created){
    // not spawned, or a bundle inside the spawned one
    if (!spawning || bundle != spawnBundle){
        // a root made now has another id on each peer, it stays local
        if (created){
            added.erase(std::remove(added.begin(), added.end(), root), added.end());
        }
        return;
    }

    NetworkComponent* component = scene->findComponent<NetworkComponent>(root);
    if (!component){
        Log::error("Network: the root of bundle %s has no NetworkComponent", BundleManager::getBundleName(bundle).c_str());
        return;
    }

    component->networkId = spawnId;
    component->owner = spawnOwner;
    if (!isServer()){
        applyState(root, *component, spawnState);
    }
    registerReplica(root, *component, bundle);
}

int NetworkSystem::getOwner(Entity entity) const{
    NetworkComponent* component = scene->findComponent<NetworkComponent>(entity);

    return component ? component->owner : 0;
}

void NetworkSystem::load(){

}

void NetworkSystem::destroy(){
    if (joined && session->getState() == NetworkState::CONNECTED){
        session->send(NetworkSession::SERVER, createMessage(LEAVE), NetworkDelivery::RELIABLE);
    }
    joined = false;
}

void NetworkSystem::draw(){

}

void NetworkSystem::update(double dt){
    registerAdded();

    if (!isServer()){
        updateClient();
    }
}

void NetworkSystem::fixedUpdate(double dt){
    registerAdded();

    if (isServer()){
        updateServer(dt);
    }
}

void NetworkSystem::onComponentAdded(Entity entity, ComponentId componentId){
    if (componentId == scene->getComponentId<NetworkComponent>()){
        added.push_back(entity);
    }
}

void NetworkSystem::onComponentRemoved(Entity entity, ComponentId componentId){
    auto it = ids.find(entity);
    if (componentId != scene->getComponentId<NetworkComponent>() || it == ids.end()){
        return;
    }

    if (isServer() && replicas[it->second].announced){
        despawned.push_back(it->second);
    }
    replicas.erase(it->second);
    ids.erase(it);
}
