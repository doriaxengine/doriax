// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef NETWORK_COMPONENT_H
#define NETWORK_COMPONENT_H

namespace doriax{

    struct NetworkComponent {
        // the same on every peer: the entity in its scene, or the server's for a spawn
        unsigned int networkId = 0;
        // the peer it belongs to, the server by default
        int owner = 1;

        bool syncPosition = true;
        bool syncRotation = true;
        bool syncScale = false;
    };

}

#endif //NETWORK_COMPONENT_H
