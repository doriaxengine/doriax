// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef NETWORKSESSION_H
#define NETWORKSESSION_H

#include "NetworkTransport.h"
#include "Packet.h"
#include <functional>
#include <map>
#include <memory>

namespace doriax {

    enum class NetworkState {
        OFFLINE,
        CONNECTING,
        CONNECTED, // client of a server
        HOSTING
    };

    // One end of a multiplayer session, which projects use through Network
    class DORIAX_API NetworkSession {
    public:
        using RpcHandler = std::function<void(int, Packet&)>;

        static const int ALL = 0;
        static const int SERVER = 1;

    private:
        struct Peer {
            // 0 until its handshake ends
            int id;
            NetworkTransport* transport;
            uint32_t connection;
            double since;
            float ping;
        };

        struct Delayed {
            double time;
            NetworkTransport* transport;
            uint32_t connection;
            NetworkDelivery delivery;
            std::vector<uint8_t> data;
        };

        NetworkState state = NetworkState::OFFLINE;
        std::vector<std::unique_ptr<NetworkTransport>> transports;
        std::vector<Peer> peers;
        int id = 0;
        int lastPeerId = SERVER;
        int maxPeers = 0;
        std::string version;
        // changes at every close(), which a callback may call
        unsigned int generation = 0;
        double since = 0;
        double lastPing = 0;

        struct Rpc {
            std::string name;
            // of the script that registered it, see removeRpcsByTag
            std::string tag;
            RpcHandler handler;
        };

        // by the hash of the name
        std::map<uint32_t, Rpc> rpcs;
        std::vector<std::vector<uint8_t>> localCalls;

        float latency = 0;
        float jitter = 0;
        float loss = 0;
        double lastDelayedTime = 0;
        std::vector<Delayed> delayed;

        Peer* findPeer(NetworkTransport* transport, uint32_t connection);
        Peer* findPeer(int id);
        void removePeer(NetworkTransport* transport, uint32_t connection);
        int countPeers() const;

        void sendTo(const Peer& peer, const Packet& packet, NetworkDelivery delivery);
        void fail(const std::string& reason);
        void reject(const Peer& peer, uint8_t reason);
        void handleEvent(NetworkTransport* transport, NetworkEvent& event);
        void handleMessage(const Peer& peer, Packet& packet);
        void callRpc(int sender, Packet& packet);

    public:
        std::function<void(int)> onPeerConnected;
        std::function<void(int)> onPeerDisconnected;
        std::function<void()> onConnected;
        std::function<void(const std::string&)> onConnectionFailed;
        std::function<void()> onDisconnected;
        // the messages of NetworkSystem
        std::function<void(int, Packet&)> onReplication;

        // seconds of the clock the sessions of this process share
        static double getTime();

        NetworkSession();
        ~NetworkSession();

        NetworkSession(const NetworkSession&) = delete;
        NetworkSession& operator=(const NetworkSession&) = delete;

        // every transport listens on the port, none hosts offline
        bool host(std::vector<std::unique_ptr<NetworkTransport>> transports, uint16_t port, int maxPeers);
        bool connect(std::unique_ptr<NetworkTransport> transport, const std::string& address, uint16_t port);
        void close();
        // server only
        void disconnect(int peer);

        // receives, calls the handlers and sends
        void update();
        void flush();

        NetworkState getState() const;
        int getId() const;
        std::vector<int> getPeers() const;
        // round trip in milliseconds, 0 while unknown
        float getPing(int peer) const;

        // sessions with different versions refuse each other
        void setVersion(const std::string& version);
        // milliseconds, and loss from 0 to 1
        void setSimulation(float latency, float jitter, float loss);

        bool registerRpc(const std::string& name, RpcHandler handler, const std::string& tag = "");
        void unregisterRpc(const std::string& name);
        void removeRpcsByTag(const std::string& substring);
        void clearRpcs();
        bool rpc(int target, const std::string& name, const Packet& values, NetworkDelivery delivery);
        // to the onReplication of a peer
        bool send(int target, const Packet& data, NetworkDelivery delivery);
    };

}

#endif //NETWORKSESSION_H
