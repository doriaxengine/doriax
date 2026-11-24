// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef LOOPBACKTRANSPORT_H
#define LOOPBACKTRANSPORT_H

#include "NetworkTransport.h"
#include <map>

namespace doriax {

    // Links sessions of one process by memory, for tests. Ports are only names.
    class DORIAX_API LoopbackTransport : public NetworkTransport {
    private:
        struct Link {
            LoopbackTransport* remote;
            uint32_t remoteConnection;
        };

        uint16_t listenPort = 0;
        int maxConnections = 0;
        uint32_t lastConnection = 0;
        std::map<uint32_t, Link> links;
        std::vector<NetworkEvent> pending;

        static std::map<uint16_t, LoopbackTransport*>& getListeners();

        void drop(uint32_t connection, bool notify);

    public:
        virtual ~LoopbackTransport();

        virtual bool listen(uint16_t port, int maxConnections) override;
        virtual bool connect(const std::string& address, uint16_t port) override;
        virtual void close() override;

        virtual void poll(std::vector<NetworkEvent>& events) override;
        virtual bool send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery) override;
        virtual void flush() override;
        virtual void disconnect(uint32_t connection) override;
    };

}

#endif //LOOPBACKTRANSPORT_H
