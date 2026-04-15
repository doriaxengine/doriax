//
// (c) 2026 Eduardo Doria.
//

#ifndef LINES_H
#define LINES_H

#include "Object.h"

namespace doriax{

    class DORIAX_API Lines: public Object{
    public:
        Lines(Scene* scene);
        Lines(Scene* scene, Entity entity);
        virtual ~Lines();

        bool load();

        void setMaxLines(unsigned int maxLines);
        unsigned int getMaxLines() const;

        void addLine(LineData line);
        void addLine(Vector3 pointA, Vector3 pointB);
        void addLine(Vector3 pointA, Vector3 pointB, Vector3 color);
        void addLine(Vector3 pointA, Vector3 pointB, Vector4 color);
        void addLine(Vector3 pointA, Vector3 pointB, Vector4 colorA, Vector4 colorB);

        LineData& getLine(size_t index);

        void updateLine(size_t index, LineData line);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector3 color);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector4 color);
        void updateLine(size_t index, Vector3 pointA, Vector3 pointB, Vector4 colorA, Vector4 colorB);
        void updateLine(size_t index, Vector3 color);
        void updateLine(size_t index, Vector4 color);
        void updateLine(size_t index, Vector4 colorA, Vector4 colorB);

        void removeLine(size_t index);

        void updateLines();
        size_t getNumLines();

        void clearLines();
    };
}

#endif //LINES_H