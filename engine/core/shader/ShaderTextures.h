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

        static Texture get(const ShaderTextureValues& textures, const std::string& name){
            for (const auto& entry : textures){
                if (entry.first == name)
                    return entry.second;
            }

            return Texture();
        }

        // the same textures in any order
        static bool equals(const ShaderTextureValues& a, const ShaderTextureValues& b){
            for (const auto& entry : a){
                if (get(b, entry.first) != entry.second)
                    return false;
            }
            for (const auto& entry : b){
                if (get(a, entry.first) != entry.second)
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
