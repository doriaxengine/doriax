#include "Rect.h"

//
// (c) 2018 Eduardo Doria.
//

using namespace doriax;

Rect::Rect(){
    x = 0;
    y = 0;
    width = 1;
    height = 1;
}

Rect::Rect(float x, float y, float width, float height){
    setRect(x, y, width, height);
}

Rect::Rect(const Rect& t){
    this->x = t.x;
    this->y = t.y;
    this->width = t.width;
    this->height = t.height;
}

Rect::Rect(const Vector4& v){
    this->x = v.x;
    this->y = v.y;
    this->width = v.z;
    this->height = v.w;
}

float Rect::operator [] ( const size_t i ) const{
    assert( i < 4 );

    return *(&x+i);
}

float& Rect::operator [] ( const size_t i ){
    assert( i < 4 );

    return *(&x+i);
}

Rect& Rect::operator = (const Rect& t){
    this->x = t.x;
    this->y = t.y;
    this->width = t.width;
    this->height = t.height;
    
    return *this;
}

Rect& Rect::operator = (const Vector4& v){
    this->x = v.x;
    this->y = v.y;
    this->width = v.z;
    this->height = v.w;

    return *this;
}

bool Rect::operator == (const Rect& t){
    return ((this->x == t.x) && (this->y == t.y) && (this->width == t.width) && (this->height == t.height));
}

bool Rect::operator != (const Rect& t){
    return ((this->x != t.x) || (this->y != t.y) || (this->width != t.width) || (this->height != t.height));
}

std::string Rect::toString() const{
    return "Rect(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(width) + ", " + std::to_string(height) + ")"; 
}

float Rect::getX() const{
    return x;
}

void Rect::setX(float x){
    this->x = x;
}

float Rect::getY() const{
    return y;
}

void Rect::setY(float y){
    this->y = y;
}

float Rect::getWidth() const{
    return width;
}

void Rect::setWidth(float width){
    this->width = width;
}

float Rect::getHeight() const{
    return height;
}

void Rect::setHeight(float height){
    this->height = height;
}

Vector4 Rect::getVector(){
    return Vector4(x, y, width, height);
}

float* Rect::ptr(){
    return &x;
}

void Rect::setRect(float x, float y, float width, float height){
    this->x = x;
    this->y = y;
    this->width = width;
    this->height = height;
}

void Rect::setRect(Rect rect){
    setRect(rect.getX(), rect.getY(), rect.getWidth(), rect.getHeight());
}

Rect& Rect::fitOnRect(Rect rect){
    if (x < rect.getX())
        x = rect.getX();

    if (y < rect.getY())
        y = rect.getY();

    if (x + width >= rect.getX() + rect.getWidth())
        width = rect.getX() + rect.getWidth() - x;

    if (y + height >= rect.getY() + rect.getHeight())
        height = rect.getY() + rect.getHeight() - y;

    if (width < 0)
        width = 0;

    if (height < 0)
        height = 0;

    return *this;
}

bool Rect::contains(Vector2 point){
    if (point.x >= x &&
        point.x <= (x + width) &&
        point.y >= y &&
        point.y <= (y + height)) {
        return true;
    }
    return false;
}

bool Rect::isNormalized() const{
    if ((x >= 0 && x <= 1) && (y >= 0 && y <= 1) && (width >= 0 && width <= 1) && (height >= 0 && height <= 1)){
        return true;
    }
    return false;
}

bool Rect::isZero() const{
    if ((x == 0) && (y == 0) && (width == 0) && (height == 0)) {
        return true;
    }
    return false;
}
