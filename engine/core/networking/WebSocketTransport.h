// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef WEBSOCKETTRANSPORT_H
#define WEBSOCKETTRANSPORT_H

#include "NetworkTransport.h"
#include <memory>

namespace doriax {

    // WebSocket for web builds, without TLS: a reverse proxy serves wss://
    class DORIAX_API WebSocketTransport : public NetworkTransport {
    private:
        struct State;
        std::unique_ptr<State> state;

    public:
        WebSocketTransport();
        virtual ~WebSocketTransport();

        virtual bool listen(uint16_t port, int maxConnections) override;
        // address is a host name or a ws:// or wss:// URL, which may hold the port
        virtual bool connect(const std::string& address, uint16_t port) override;
        virtual void close() override;

        virtual void poll(std::vector<NetworkEvent>& events) override;
        virtual bool send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery) override;
        virtual void flush() override;
        virtual void disconnect(uint32_t connection) override;
    };

}

#endif //WEBSOCKETTRANSPORT_H
