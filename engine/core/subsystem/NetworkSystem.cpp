// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "NetworkSystem.h"

#include "Log.h"
#include "Scene.h"
#include "component/Transform.h"
#include "manager/BundleManager.h"
#include "manager/SceneManager.h"
#include "networking/Network.h"
#ifdef DORIAX_PHYSICS_2D
#include "object/physics/Body2D.h"
#endif
#ifdef DORIAX_PHYSICS_3D
#include "object/physics/Body3D.h"
#include "component/Character3DComponent.h"
#endif

#include <algorithm>
#include <cmath>

using namespace doriax;

namespace {
    // smaller ids are the entities saved with a scene
    const unsigned int firstSpawnId = 0x1000000;
    const size_t maxStates = 32;
    // an unreliable message has to fit in one UDP packet
    const size_t maxStateSize = 1100;
    // a reliable one is split well before the limit of the transports
    const size_t maxReliableSize = 32768;
    // its size travels in 16 bits
    const size_t maxValueSize = 0xFFFF;
    // a clock that jumped is taken again from the next message
    const double maxOffsetError = 0.25;

    enum Message : uint8_t {
        JOIN = 1,
        LEAVE,
        SPAWN,
        DESPAWN,
        STATE,
        VALUES
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

        system->checkSession();
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
            }else if (type == VALUES){
                system->receiveValues(packet);
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

void NetworkSystem::checkSession(){
    if (generation == session->getGeneration()){
        return;
    }
    generation = session->getGeneration();

    // what the session before spawned and sent means nothing to this one
    removeSpawned();
    subscribers.clear();
    despawned.clear();
    for (auto& entry : replicas){
        entry.second.moving = false;
        entry.second.received = false;
    }
    for (Sync& sync : syncs){
        sync.sent.clear();
    }
    lastNetworkId = firstSpawnId;
    sendTime = 0;
    joined = false;
    hasOffset = false;
    lastStateTime = 0;
}

void NetworkSystem::setFollowing(Replica& replica, bool following){
    const NetworkComponent* component = scene->findComponent<NetworkComponent>(replica.entity);
    // an entity that only syncs values keeps its own physics
    if (following && component && !component->syncPosition && !component->syncRotation){
        return;
    }
    if (replica.following == following){
        return;
    }
    replica.following = following;

    // the server simulates, a copy is moved by its states
    BodyType type = following ? BodyType::KINEMATIC : BodyType::DYNAMIC;
#ifdef DORIAX_PHYSICS_2D
    Body2DComponent* body2D = scene->findComponent<Body2DComponent>(replica.entity);
    if (body2D && (following ? body2D->type == BodyType::DYNAMIC : replica.dynamic)){
        Body2D body(scene, replica.entity);
        body.setType(type);
        if (b2Body_IsValid(body2D->body)){
            body.setLinearVelocity(Vector2::ZERO);
            body.setAngularVelocity(0);
        }
        replica.dynamic = following;
    }
#endif
#ifdef DORIAX_PHYSICS_3D
    Body3DComponent* body3D = scene->findComponent<Body3DComponent>(replica.entity);
    if (body3D && (following ? body3D->type == BodyType::DYNAMIC : replica.dynamic)){
        Body3D body(scene, replica.entity);
        body.setType(type);
        if (!body3D->body.IsInvalid()){
            body.setLinearVelocity(Vector3::ZERO);
            body.setAngularVelocity(Vector3::ZERO);
        }
        replica.dynamic = following;
    }

    Character3DComponent* character = scene->findComponent<Character3DComponent>(replica.entity);
    if (character && (following ? !character->kinematic : replica.simulated)){
        character->kinematic = following;
        replica.simulated = following;
        // what it was told to do does not come back with the simulation
        character->moveVelocity = Vector3::ZERO;
        character->needJump = false;
        if (character->character){
            character->character->SetLinearVelocity(JPH::Vec3::sZero());
        }
    }
#endif
}

void NetworkSystem::registerAdded(){
    for (Entity entity : added){
        NetworkComponent* component = scene->findComponent<NetworkComponent>(entity);
        auto it = ids.find(entity);
        if (it != ids.end()){
            // a body it got after it started to follow is held too
            Replica& replica = replicas[it->second];
            if (replica.following){
                replica.following = false;
                setFollowing(replica, true);
            }
        }else if (component){
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
    replica.received = (bundle != 0);

    setFollowing(replica, joined);
}

void NetworkSystem::removeSpawned(){
    std::vector<Entity> spawned;
    for (auto& entry : replicas){
        entry.second.states.clear();
        if (entry.second.bundle != 0){
            spawned.push_back(entry.second.entity);
        }else{
            setFollowing(entry.second, false);
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
    packet.writeInt((int64_t)(NetworkSession::getTime() * 1000.0));

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

    double interval = 1.0 / sendRate;
    sendTime += dt;
    if (sendTime < interval){
        return;
    }
    sendTime = std::fmod(sendTime, interval);

    for (unsigned int id : despawned){
        Packet packet = createMessage(DESPAWN);
        packet.writeUInt32(id);
        sendToSubscribers(packet, NetworkDelivery::RELIABLE);
    }
    despawned.clear();

    // one time for the states of this send
    const Packet empty = createState();
    Packet moved = empty;
    Packet rested = empty;
    size_t emptySize = empty.getSize();

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
                moved = empty;
            }
        }else if (replica.moving){
            // where it stopped can't be lost
            writeState(rested, entry.first, state, component);
            replica.moving = false;

            if (rested.getSize() > maxReliableSize){
                sendToSubscribers(rested, NetworkDelivery::RELIABLE);
                rested = empty;
            }
        }
    }

    if (moved.getSize() > emptySize){
        sendToSubscribers(moved, NetworkDelivery::UNRELIABLE);
    }
    if (rested.getSize() > emptySize){
        sendToSubscribers(rested, NetworkDelivery::RELIABLE);
    }

    sendValues(0, false);
}

void NetworkSystem::sendValues(int peer, bool all){
    Packet packet = createMessage(VALUES);
    size_t emptySize = packet.getSize();

    auto send = [&](){
        if (peer != 0){
            session->send(peer, packet, NetworkDelivery::RELIABLE);
        }else{
            sendToSubscribers(packet, NetworkDelivery::RELIABLE);
        }
        packet = createMessage(VALUES);
    };

    for (Sync& sync : syncs){
        auto it = ids.find(sync.entity);
        if (it == ids.end()){
            continue;
        }

        Packet value;
        sync.read(value);
        std::vector<uint8_t> bytes(value.getData(), value.getData() + value.getSize());
        if ((!all && bytes == sync.sent) || bytes.size() > maxValueSize){
            continue;
        }
        if (!all){
            sync.sent = bytes;
        }

        packet.writeUInt32(it->second);
        packet.writeUInt32(sync.name);
        packet.writeUInt16((uint16_t)bytes.size());
        packet.writeBytes(bytes.data(), bytes.size());

        if (packet.getSize() > maxReliableSize){
            send();
        }
    }

    if (packet.getSize() > emptySize){
        send();
    }
}

void NetworkSystem::updateClient(){
    bool connected = (session->getState() == NetworkState::CONNECTED);

    if (connected && !joined){
        session->send(NetworkSession::SERVER, createMessage(JOIN), NetworkDelivery::RELIABLE);
        joined = true;
        for (auto& entry : replicas){
            setFollowing(entry.second, true);
        }
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
        if (replica.bundle == 0){
            writeState(state, entry.first, readState(replica.entity), scene->getComponent<NetworkComponent>(replica.entity));
            if (state.getSize() > maxReliableSize){
                session->send(sender, state, NetworkDelivery::RELIABLE);
                state = createState();
            }
        }else if (replica.announced){
            // one not announced yet goes to every subscriber at the next send
            session->send(sender, createSpawn(entry.first, replica), NetworkDelivery::RELIABLE);
        }
    }
    session->send(sender, state, NetworkDelivery::RELIABLE);

    sendValues(sender, true);
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
    double time = packet.readInt() / 1000.0;
    double now = NetworkSession::getTime();
    if (!packet.isValid()){
        return;
    }

    // this clock against the server's, trip included; a late message says nothing
    if (!hasOffset || time > lastStateTime){
        double sample = now - time;
        if (hasOffset && std::fabs(sample - offset) < maxOffsetError){
            offset += (sample - offset) * 0.1;
        }else{
            offset = sample;
            hasOffset = true;
        }
        lastStateTime = time;
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

        // from rest it moves from where it is shown, without a jump
        double shown = now - offset - interpolationDelay;
        if (states.empty() && it->second.received && shown < time){
            State current = readState(it->second.entity);
            current.time = shown;
            states.push_back(current);
        }
        it->second.received = true;

        state.time = time;
        states.push_back(state);
        if (states.size() > maxStates){
            states.pop_front();
        }
    }
}

void NetworkSystem::receiveValues(Packet& packet){
    while (packet.isValid() && packet.getRemaining() > 0){
        unsigned int id = packet.readUInt32();
        uint32_t name = packet.readUInt32();
        size_t size = packet.readUInt16();
        if (!packet.isValid() || size > packet.getRemaining()){
            return;
        }

        Packet value(packet.getData() + packet.getSize() - packet.getRemaining(), size);
        packet.skip(size);

        auto it = replicas.find(id);
        if (it == replicas.end()){
            continue;
        }
        for (Sync& sync : syncs){
            if (sync.entity == it->second.entity && sync.name == name){
                // a copy, the call may remove the sync
                std::function<void(Packet&)> write = sync.write;
                write(value);
                // the call closed the session
                if (generation != session->getGeneration()){
                    return;
                }
                break;
            }
        }
    }
}

void NetworkSystem::sync(Entity entity, const std::string& name, std::function<void(Packet&)> read, std::function<void(Packet&)> write, const std::string& tag){
    if (!scene->findComponent<NetworkComponent>(entity)){
        Log::error("Network: entity %u needs a NetworkComponent to sync %s", entity, name.c_str());
        return;
    }

    // registered again, the new one takes its place
    uint32_t hash = NetworkSession::hashName(name);
    syncs.erase(std::remove_if(syncs.begin(), syncs.end(), [&](const Sync& sync){
        return sync.entity == entity && sync.name == hash;
    }), syncs.end());

    syncs.push_back({entity, hash, tag, std::move(read), std::move(write), {}});
}

void NetworkSystem::removeSyncsByTag(const std::string& substring){
    for (NetworkSystem* system : getSystems()){
        std::vector<Sync>& syncs = system->syncs;
        syncs.erase(std::remove_if(syncs.begin(), syncs.end(), [&](const Sync& sync){
            return sync.tag.find(substring) != std::string::npos;
        }), syncs.end());
    }
}

void NetworkSystem::setSession(NetworkSession* session){
    this->session = session;
    generation = session->getGeneration();
    session->onReplication = [session](int sender, Packet& packet){ receive(session, sender, packet); };
}

void NetworkSystem::setSceneId(uint32_t sceneId){
    this->sceneId = sceneId;
}

void NetworkSystem::setSendRate(float sendRate){
    if (sendRate > 0){
        this->sendRate = sendRate;
    }
}

float NetworkSystem::getSendRate() const{
    return sendRate;
}

void NetworkSystem::setInterpolationDelay(float interpolationDelay){
    this->interpolationDelay = std::max(interpolationDelay, 0.0f);
}

float NetworkSystem::getInterpolationDelay() const{
    return interpolationDelay;
}

Entity NetworkSystem::spawn(const std::string& bundle, int owner){
    checkSession();
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

void NetworkSystem::bundleCreated(Entity root, uint32_t bundle, bool created, const std::vector<Entity>& entities){
    bool spawned = spawning && bundle == spawnBundle;

    // ids made now differ on each peer, only a spawned root follows
    added.erase(std::remove_if(added.begin(), added.end(), [&](Entity entity){
        bool member = std::find(entities.begin(), entities.end(), entity) != entities.end();
        return member && (entity != root || created);
    }), added.end());

    if (!spawned){
        return;
    }

    NetworkComponent* component = scene->findComponent<NetworkComponent>(root);
    if (!component){
        Log::error("Network: the root of bundle %s has no NetworkComponent", BundleManager::getBundleName(bundle).c_str());
        return;
    }

    // init() may create the same bundle, which is not this spawn
    spawning = false;

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
    checkSession();
    registerAdded();

    // the server sends what it is about to draw
    if (isServer()){
        updateServer(dt);
    }else{
        updateClient();
    }
}

void NetworkSystem::onComponentAdded(Entity entity, ComponentId componentId){
    // a replica is seen again for the body it may have got
    if (componentId == scene->getComponentId<NetworkComponent>() || ids.count(entity)){
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
    syncs.erase(std::remove_if(syncs.begin(), syncs.end(), [entity](const Sync& sync){ return sync.entity == entity; }), syncs.end());
    replicas.erase(it->second);
    ids.erase(it);
}
