#ifndef RENDERUTIL_H
#define RENDERUTIL_H

namespace doriax::editor{

    enum class CursorSelected{
        POINTER,
        HAND
    };

    enum class GizmoSelected{
        TRANSLATE,
        ROTATE,
        SCALE,
        OBJECT2D
    };

    enum class GizmoSideSelected{
        NONE,
        X,
        Y,
        Z,
        XY,
        XZ,
        YZ,
        XYZ
    };

    enum class Gizmo2DSideSelected{
        NONE,
        NX_NY, // -X -Y
        NX, // -X
        NX_PY, // -X +Y
        PY, // +Y
        PX_PY, // +X +Y
        PX, // +X
        PX_NY, // +X -Y
        NY, // -Y
        CENTER
    };

}

#endif /* RENDERUTIL_H */