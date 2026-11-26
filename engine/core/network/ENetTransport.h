// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef ENETTRANSPORT_H
#define ENETTRANSPORT_H

#include "NetworkTransport.h"
#include <map>

struct _ENetHost;
struct _ENetPeer;

namespace doriax {

    // UDP by ENet, reliable and unreliable, IPv4 and IPv6
    class DORIAX_API ENetTransport : public NetworkTransport {
    private:
        _ENetHost* host = nullptr;
        uint32_t lastConnection = 0;
        std::map<uint32_t, _ENetPeer*> peers;

        bool create(uint16_t port, int maxConnections, bool server);

    public:
        virtual ~ENetTransport();

        virtual bool listen(uint16_t port, int maxConnections) override;
        virtual bool connect(const std::string& address, uint16_t port) override;
        virtual void close() override;

        virtual void poll(std::vector<NetworkEvent>& events) override;
        virtual bool send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery) override;
        virtual void flush() override;
        virtual void disconnect(uint32_t connection) override;
    };

}

#endif //ENETTRANSPORT_H
