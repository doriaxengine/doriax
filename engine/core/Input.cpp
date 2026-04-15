//
// (c) 2026 Eduardo Doria.
//


#include "Input.h"

using namespace doriax;

std::map<int,bool> Input::keyPressed;
std::map<int,bool> Input::mousePressed;
Vector2 Input::mousePosition;
Vector2 Input::mouseScroll;
std::vector<Touch> Input::touches;
bool Input::mousedEntered;
int Input::modifiers;

void Input::addKeyPressed(int key){
    keyPressed[key] = true;
}

void Input::releaseKeyPressed(int key){
    keyPressed[key] = false;
}

void Input::addMousePressed(int button){
    mousePressed[button] = true;
}

void Input::releaseMousePressed(int button){
    mousePressed[button] = false;
}

void Input::setMousePosition(float x, float y){
    mousePosition.x = x;
    mousePosition.y = y;
}

void Input::setMouseScroll(float xoffset, float yoffset){
    mouseScroll.x = xoffset;
    mouseScroll.y = yoffset;
}

void Input::addTouch(int pointer, float x, float y){
    if (findTouchIndex(pointer) == -1)
        touches.push_back({pointer, Vector2(x, y)});
}

void Input::setTouchPosition(int pointer, float x, float y){
    size_t index = findTouchIndex(pointer);
    if (index >=0 && index < touches.size())
        touches[index].position = Vector2(x, y);
}

void Input::removeTouch(int pointer){
    size_t index = findTouchIndex(pointer);
    if (index != -1)
        touches.erase (touches.begin()+index);
}

void Input::clearTouches(){
    touches.clear();
}

void Input::addMouseEntered(){
    mousedEntered = true;
}

void Input::releaseMouseEntered(){
    mousedEntered = false;
}

void Input::setModifiers(int mods){
    modifiers = mods;
}

bool Input::isKeyPressed(int key){
    return keyPressed[key];
}

bool Input::isMousePressed(int button){
    return mousePressed[button];
}

bool Input::isTouch(){
    return (touches.size() > 0);
}

bool Input::isMouseEntered(){
    return mousedEntered;
}

Vector2 Input::getMousePosition(){
    return mousePosition;
}

Vector2 Input::getMouseScroll(){
    return mouseScroll;
}

Vector2 Input::getTouchPosition(int pointer){
    size_t index = findTouchIndex(pointer);
    if (index >=0 && index < touches.size())
        return touches[index].position;
    
    return Vector2(-1, -1); 
}

std::vector<Touch> Input::getTouches(){
    return touches;
}

size_t Input::numTouches(){
    return touches.size();
}

int Input::getModifiers(){
    return modifiers;
}

size_t Input::findTouchIndex(int pointer){
    size_t index = -1;
    for(int i = 0; i < touches.size(); i++){
        if (touches[i].pointer == pointer)
            index = i;
    }
    
    return index;
}
