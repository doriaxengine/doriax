
#ifndef Rect_h
#define Rect_h

//
// (c) 2018 Eduardo Doria.
//

#include "Vector2.h"
#include "Vector4.h"
#include <string>

namespace doriax {

    class DORIAX_API Rect{
    private:
        
        float x, y, width, height;
        
    public:
        
        Rect();
        Rect(float x, float y, float width, float height);
        Rect(const Rect& t);
        Rect(const Vector4& v);

        std::string toString() const;
        
        float operator [] ( const size_t i ) const;
        float& operator [] ( const size_t i );
        Rect& operator = (const Rect& t);
        Rect& operator = (const Vector4& v);
        bool operator == (const Rect& t);
        bool operator != (const Rect& t);
        
        float getX() const;
        void setX(float x);
        float getY() const;
        void setY(float y);
        float getWidth() const;
        void setWidth(float width);
        float getHeight() const;
        void setHeight(float height);

        Vector4 getVector();
        
        float* ptr();
        
        void setRect(float x, float y, float width, float height);
        void setRect(Rect rect);

        Rect& fitOnRect(Rect rect);

        bool contains(Vector2 point);

        bool isNormalized() const;
        bool isZero() const;
    };
        
}

#endif /* Rect_h */
