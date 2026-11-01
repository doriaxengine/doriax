// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef SKIN_COMPONENT_H
#define SKIN_COMPONENT_H

#include "Engine.h"
#include "util/HybridArray.h"
#include "math/Matrix4.h"
#include "math/AABB.h"
#include "render/BufferRender.h"
#include "render/TextureRender.h"
#include <vector>

namespace doriax{

    // Bone palette of a skinned mesh, on its own so rigid meshes do not carry MAX_BONES matrices.
    // MeshSystem adds it when a model loads a skinned mesh.
    struct SkinComponent{
        HybridArray<Matrix4, MAX_BONES> bonesMatrix;
        unsigned int numBones = 0; // joints of the skin, the first entries of bonesMatrix
        BufferRender bonesBuffer;
        TextureRender bonesTexture;
        bool needUpdateBones = false;

        std::vector<AABB> bonesAABB; // bind-pose bounds per bone index
    };

}

#endif //SKIN_COMPONENT_H
