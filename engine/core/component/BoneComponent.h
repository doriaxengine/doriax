//
// (c) 2026 Eduardo Doria.
//

#ifndef BONE_COMPONENT_H
#define BONE_COMPONENT_H

#include <string>

namespace doriax{

    struct DORIAX_API BoneComponent{
        Entity model = NULL_ENTITY;

        int index;

        Vector3 bindPosition;
        Quaternion bindRotation;
        Vector3 bindScale;

        Matrix4 offsetMatrix; // inverse bind matrix
    };

}

#endif //BONE_COMPONENT_H