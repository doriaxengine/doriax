// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef NETWORK_H
#define NETWORK_H

#include "Export.h"
#include "Log.h"
#include "NetworkSession.h"
#include "util/FunctionSubscribe.h"
#include <tuple>
#include <utility>

// An RPC that calls a method of this script, removed with the script
#define REGISTER_RPC(NAME, METHOD) \
    ::doriax::Network::registerRpc(NAME, this, &std::remove_pointer_t<decltype(this)>::METHOD, DORIAX_EVENT_SUBSCRIPTION_TAG(METHOD))

namespace doriax {

    // Multiplayer by RPCs: a server, which is peer 1, and clients that only reach it
    class DORIAX_API Network {

        friend class Engine;

    private:
        static NetworkSession& getSession();

        // Called by Engine
        static void update();
        static void flush();
        static void clearSubscriptions();
        static void removeSubscriptionsByTag(const std::string& substring);
        static void reset();

        template<typename... Args>
        static void registerTyped(const std::string& name, std::function<void(int, Args...)> function, const std::string& tag){
            registerRawRpc(name, [name, function](int sender, Packet& packet){
                std::tuple<std::decay_t<Args>...> values;
                if (readValues(packet, values, std::index_sequence_for<Args...>{})){
                    std::apply([&](auto&... value){ function(sender, value...); }, values);
                }else{
                    Log::error("Network: wrong arguments in RPC %s from peer %i", name.c_str(), sender);
                }
            }, tag);
        }

        template<typename Tuple, size_t... I>
        static bool readValues(Packet& packet, Tuple& values, std::index_sequence<I...>){
            return (packet.readValue(std::get<I>(values)) && ...) && packet.getRemaining() == 0;
        }

    public:
        static const int ALL = NetworkSession::ALL;
        static const int SERVER = NetworkSession::SERVER;

        // UDP, and WebSocket (TCP) for web builds, on the same port
        static bool host(uint16_t port, int maxPeers = 32);
        // a server without sockets, for a single player
        static bool hostOffline();
        // UDP, or WebSocket in web builds and for ws:// addresses
        static bool connect(const std::string& address, uint16_t port = 0);
        static void close();
        // server only
        static void disconnect(int peer);

        static NetworkState getState();
        // hosting, also offline
        static bool isServer();
        static bool isConnected();
        // this peer: 1 on the server, 0 while not connected
        static int getId();
        static std::vector<int> getPeers();
        // round trip in milliseconds, 0 while unknown
        static float getPing(int peer = SERVER);

        // peers with different versions refuse each other
        static void setVersion(const std::string& version);
        // for tests: delay in milliseconds on what is sent, loss from 0 to 1
        static void setSimulation(float latency, float jitter, float loss);

        // function(int sender, values...), called by the peers
        template<typename Function>
        static void registerRpc(const std::string& name, Function function){
            registerTyped(name, std::function(function), "");
        }
        // a method of a script, see REGISTER_RPC
        template<class T, typename... Args>
        static void registerRpc(const std::string& name, T* instance, void (T::*method)(int, Args...), const std::string& tag){
            registerTyped(name, std::function<void(int, Args...)>([instance, method](int sender, Args... args){
                (instance->*method)(sender, args...);
            }), tag);
        }
        // the handler reads the values from the packet
        static void registerRawRpc(const std::string& name, NetworkSession::RpcHandler handler, const std::string& tag = "");
        static void unregisterRpc(const std::string& name);

        // to a peer, to ALL, or to this peer to call it here
        template<typename... Args>
        static bool rpc(int target, const std::string& name, const Args&... args){
            Packet values;
            (values.writeValue(args), ...);

            return sendRpc(target, name, values, NetworkDelivery::RELIABLE);
        }
        // may be lost, for values sent again every tick
        template<typename... Args>
        static bool rpcUnreliable(int target, const std::string& name, const Args&... args){
            Packet values;
            (values.writeValue(args), ...);

            return sendRpc(target, name, values, NetworkDelivery::UNRELIABLE);
        }
        static bool sendRpc(int target, const std::string& name, const Packet& values, NetworkDelivery delivery);

        // on the server
        static FunctionSubscribe<void(int)> onPeerConnected;
        static FunctionSubscribe<void(int)> onPeerDisconnected;
        // on a client
        static FunctionSubscribe<void()> onConnected;
        // unreachable, timeout, version, full or protocol
        static FunctionSubscribe<void(std::string)> onConnectionFailed;
        static FunctionSubscribe<void()> onDisconnected;
    };

}

#endif //NETWORK_H
