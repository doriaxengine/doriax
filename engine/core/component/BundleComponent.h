// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef BUNDLE_COMPONENT_H
#define BUNDLE_COMPONENT_H

#include <string>

namespace doriax{

    struct BundleComponent {
        std::string name;
        std::string path;
        // members wait for BundleManager::loadBundle
        bool onDemand = false;
    };

}

#endif //BUNDLE_COMPONENT_H
