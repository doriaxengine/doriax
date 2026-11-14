// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SHADERTEXTURES_H
#define SHADERTEXTURES_H

#include "texture/Texture.h"
#include <string>
#include <vector>
#include <utility>

namespace doriax{

    // shader texture parameters by name; an empty entry counts as none
    typedef std::vector<std::pair<std::string, Texture>> ShaderTextureValues;

    class ShaderTextures{
    public:
        static void set(ShaderTextureValues& textures, const std::string& name, const Texture& texture){
            for (auto& entry : textures){
                if (entry.first == name){
                    entry.second = texture;
                    return;
                }
            }
            textures.push_back({name, texture});
        }

        // null when it has no entry
        static const Texture* find(const ShaderTextureValues& textures, const std::string& name){
            for (const auto& entry : textures){
                if (entry.first == name)
                    return &entry.second;
            }

            return nullptr;
        }

        static Texture get(const ShaderTextureValues& textures, const std::string& name){
            const Texture* texture = find(textures, name);

            return texture ? *texture : Texture();
        }

        // the same textures in any order, an empty entry being none
        static bool equals(const ShaderTextureValues& a, const ShaderTextureValues& b){
            static const Texture none;
            for (const auto& entry : a){
                const Texture* other = find(b, entry.first);
                if ((other ? *other : none) != entry.second)
                    return false;
            }
            for (const auto& entry : b){
                const Texture* other = find(a, entry.first);
                if ((other ? *other : none) != entry.second)
                    return false;
            }

            return true;
        }

        static bool isFramebufferOutdated(const ShaderTextureValues& textures){
            for (const auto& entry : textures){
                if (entry.second.isFramebufferOutdated())
                    return true;
            }

            return false;
        }
    };

}

#endif //SHADERTEXTURES_H
