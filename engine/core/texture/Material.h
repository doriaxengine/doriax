//
// (c) 2026 Eduardo Doria.
//

#ifndef MATERIAL_H
#define MATERIAL_H

#include "math/Vector3.h"
#include "texture/Texture.h"

namespace doriax{

    struct DORIAX_API Material{
        // --- start shader part
        Vector4 baseColorFactor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);  // linear color
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        uint8_t _pad_24[8];
        Vector3 emissiveFactor = Vector3(0.0f, 0.0f, 0.0f);  // linear color
        uint8_t _pad_44[4];
        // --- end shader part

        Texture baseColorTexture;
        Texture emissiveTexture;
        Texture metallicRoughnessTexture;
        Texture occlusionTexture;
        Texture normalTexture;

        // material name or file
        std::string name = "";

        bool operator == (const Material& other) const {
            return baseColorFactor == other.baseColorFactor &&
                   metallicFactor == other.metallicFactor &&
                   roughnessFactor == other.roughnessFactor &&
                   emissiveFactor == other.emissiveFactor &&
                   baseColorTexture == other.baseColorTexture &&
                   emissiveTexture == other.emissiveTexture &&
                   metallicRoughnessTexture == other.metallicRoughnessTexture &&
                   occlusionTexture == other.occlusionTexture &&
                   normalTexture == other.normalTexture &&
                   name == other.name;
        }

        bool operator != (const Material& other) const {
            return !(*this == other);
        }
    };

}

#endif //MATERIAL_H