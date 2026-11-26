// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef NETWORKSYSTEM_H
#define NETWORKSYSTEM_H

#include "SubSystem.h"
#include "component/NetworkComponent.h"
#include "networking/NetworkSession.h"
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace doriax{

    // Keeps the entities with a NetworkComponent of the clients like those of the server
    class DORIAX_API NetworkSystem : public SubSystem {

        friend class Network;
        friend class BundleManager;

    private:
        struct State {
            // of the server clock, in seconds
            double time = 0;
            Vector3 position;
            Quaternion rotation;
            Vector3 scale = Vector3(1.0, 1.0, 1.0);
        };

        struct Replica {
            Entity entity = NULL_ENTITY;
            // 0 for an entity of the scene, which every peer has
            uint32_t bundle = 0;
            // on the server: what the clients know
            State sent;
            bool announced = false;
            bool moving = false;
            // on a client: the states it moves between
            std::deque<State> states;
            // a first state places it, the next ones move it
            bool received = false;
            bool following = false;
            // a dynamic body and a simulated character are kinematic while they follow
            bool dynamic = false;
            bool simulated = false;
        };

        NetworkSession* session;
        unsigned int generation = 0;
        // 0 takes the id the scene has in SceneManager
        uint32_t sceneId = 0;

        // by network id
        std::map<unsigned int, Replica> replicas;
        std::map<Entity, unsigned int> ids;
        std::vector<Entity> added;
        unsigned int lastNetworkId;

        // on the server: the peers that loaded this scene
        std::set<int> subscribers;
        std::vector<unsigned int> despawned;
        double sendTime = 0;

        // on a client
        bool joined = false;
        bool hasOffset = false;
        double offset = 0;
        double lastStateTime = 0;

        // the bundle being created by spawn() or by the server's message
        bool spawning = false;
        uint32_t spawnBundle = 0;
        unsigned int spawnId = 0;
        int spawnOwner = 0;
        State spawnState;

        float sendRate = 20;
        float interpolationDelay = 0.1f;

        // a value of a script that the clients take from the server
        struct Sync {
            Entity entity;
            uint32_t name;
            // of the script that registered it
            std::string tag;
            std::function<void(Packet&)> read;
            std::function<void(Packet&)> write;
            std::vector<uint8_t> sent;
        };

        std::vector<Sync> syncs;

        static void receive(NetworkSession* session, int sender, Packet& packet);

        uint32_t getSceneId() const;
        bool isServer() const;

        State readState(Entity entity) const;
        void applyState(Entity entity, const NetworkComponent& component, const State& state);
        void writeState(Packet& packet, unsigned int id, const State& state, const NetworkComponent& component) const;

        void checkSession();
        void setFollowing(Replica& replica, bool following);
        void registerAdded();
        void registerReplica(Entity entity, NetworkComponent& component, uint32_t bundle);
        void removeSpawned();

        Packet createMessage(uint8_t type) const;
        Packet createState() const;
        Packet createSpawn(unsigned int id, const Replica& replica) const;
        void sendToSubscribers(const Packet& packet, NetworkDelivery delivery);

        void updateServer(double dt);
        void updateClient();
        void interpolate();

        void receiveJoin(int sender);
        void receiveSpawn(Packet& packet);
        void receiveDespawn(Packet& packet);
        void receiveState(Packet& packet);
        void receiveValues(Packet& packet);
        // to a peer, or to the subscribers when it is 0
        void sendValues(int peer, bool all);

        // Network's own
        void setSession(NetworkSession* session);

        // Called by Network
        Entity spawn(const std::string& bundle, int owner);
        int getOwner(Entity entity) const;
        // read writes the value to a packet on the server, write takes it on a client
        void sync(Entity entity, const std::string& name, std::function<void(Packet&)> read, std::function<void(Packet&)> write, const std::string& tag);
        static void removeSyncsByTag(const std::string& substring);

        // Called by BundleManager before the scripts of a new instance start
        void bundleCreated(Entity root, uint32_t bundle, bool created, const std::vector<Entity>& entities);

    public:
        NetworkSystem(Scene* scene);
        virtual ~NetworkSystem();

        // for a scene that SceneManager did not create, the same on every peer
        void setSceneId(uint32_t sceneId);

        // states sent per second by the server
        void setSendRate(float sendRate);
        float getSendRate() const;

        // seconds a client shows the past, to always have two states to move between
        void setInterpolationDelay(float interpolationDelay);
        float getInterpolationDelay() const;

        void load() override;
        void destroy() override;
        void draw() override;
        void update(double dt) override;

        void onComponentAdded(Entity entity, ComponentId componentId) override;
        void onComponentRemoved(Entity entity, ComponentId componentId) override;
    };

}

#endif //NETWORKSYSTEM_H
