// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef NETWORKTRANSPORT_H
#define NETWORKTRANSPORT_H

#include "Export.h"
#include <cstdint>
#include <string>
#include <vector>

namespace doriax {

    enum class NetworkDelivery {
        RELIABLE, // arrives, in the order sent
        UNRELIABLE // may be lost or arrive late, never waits for a resend
    };

    enum class NetworkEventType {
        CONNECTED,
        DISCONNECTED,
        RECEIVED
    };

    struct NetworkEvent {
        NetworkEventType type;
        // connection id, unique inside its transport
        uint32_t connection;
        std::vector<uint8_t> data;
    };

    // Carries whole messages without blocking, results arrive as events from poll()
    class DORIAX_API NetworkTransport {
    public:
        virtual ~NetworkTransport() = default;

        virtual bool listen(uint16_t port, int maxConnections) = 0;
        // CONNECTED or DISCONNECTED tells how it went
        virtual bool connect(const std::string& address, uint16_t port) = 0;
        virtual void close() = 0;

        virtual void poll(std::vector<NetworkEvent>& events) = 0;
        virtual bool send(uint32_t connection, const uint8_t* data, size_t size, NetworkDelivery delivery) = 0;
        // puts on the wire what send() queued
        virtual void flush() = 0;
        // after delivering what was already sent
        virtual void disconnect(uint32_t connection) = 0;
    };

}

#endif //NETWORKTRANSPORT_H
