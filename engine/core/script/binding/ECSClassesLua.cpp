// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "ecs/Entity.h"
#include "ecs/Signature.h"
#include "ecs/EntityManager.h"
#include "ecs/ComponentArray.h"

#include "subsystem/ActionSystem.h"
#include "subsystem/AudioSystem.h"
#include "subsystem/MeshSystem.h"
#include "subsystem/PhysicsSystem.h"
#include "subsystem/RenderSystem.h"
#include "subsystem/UISystem.h"

//TODO: Add all components and properties
#include "component/ActionComponent.h"
#include "component/TimedActionComponent.h"
#include "component/UIComponent.h"
#include "component/UILayoutComponent.h"
#include "component/UIContainerComponent.h"
#include "component/ButtonComponent.h"
#include "component/TextEditComponent.h"
#include "component/ParticlesComponent.h"
#include "component/SoundComponent.h"
#include "component/SpriteComponent.h"
#include "component/FogComponent.h"
#include "component/TilemapComponent.h"

using namespace doriax;

void LuaBinding::registerECSClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("SoundState")
        .addVariable("Playing", SoundState::Playing)
        .addVariable("Paused", SoundState::Paused)
        .addVariable("Stopped", SoundState::Stopped)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("SoundAttenuation")
        .addVariable("NO_ATTENUATION", SoundAttenuation::NO_ATTENUATION)
        .addVariable("INVERSE_DISTANCE", SoundAttenuation::INVERSE_DISTANCE)
        .addVariable("LINEAR_DISTANCE", SoundAttenuation::LINEAR_DISTANCE)
        .addVariable("EXPONENTIAL_DISTANCE", SoundAttenuation::EXPONENTIAL_DISTANCE)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("AnchorPreset")
        .addVariable("NONE", AnchorPreset::NONE)
        .addVariable("TOP_LEFT", AnchorPreset::TOP_LEFT)
        .addVariable("TOP_RIGHT", AnchorPreset::TOP_RIGHT)
        .addVariable("BOTTOM_LEFT", AnchorPreset::BOTTOM_LEFT)
        .addVariable("BOTTOM_RIGHT", AnchorPreset::BOTTOM_RIGHT)
        .addVariable("CENTER_LEFT", AnchorPreset::CENTER_LEFT)
        .addVariable("CENTER_TOP", AnchorPreset::CENTER_TOP)
        .addVariable("CENTER_RIGHT", AnchorPreset::CENTER_RIGHT)
        .addVariable("CENTER_BOTTOM", AnchorPreset::CENTER_BOTTOM)
        .addVariable("CENTER", AnchorPreset::CENTER)
        .addVariable("LEFT_WIDE", AnchorPreset::LEFT_WIDE)
        .addVariable("TOP_WIDE", AnchorPreset::TOP_WIDE)
        .addVariable("RIGHT_WIDE", AnchorPreset::RIGHT_WIDE)
        .addVariable("BOTTOM_WIDE", AnchorPreset::BOTTOM_WIDE)
        .addVariable("VERTICAL_CENTER_WIDE", AnchorPreset::VERTICAL_CENTER_WIDE)
        .addVariable("HORIZONTAL_CENTER_WIDE", AnchorPreset::HORIZONTAL_CENTER_WIDE)
        .addVariable("FULL_LAYOUT", AnchorPreset::FULL_LAYOUT)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("PivotPreset")
        .addVariable("CENTER", PivotPreset::CENTER)
        .addVariable("TOP_CENTER", PivotPreset::TOP_CENTER)
        .addVariable("BOTTOM_CENTER", PivotPreset::BOTTOM_CENTER)
        .addVariable("LEFT_CENTER", PivotPreset::LEFT_CENTER)
        .addVariable("RIGHT_CENTER", PivotPreset::RIGHT_CENTER)
        .addVariable("TOP_LEFT", PivotPreset::TOP_LEFT)
        .addVariable("BOTTOM_LEFT", PivotPreset::BOTTOM_LEFT)
        .addVariable("TOP_RIGHT", PivotPreset::TOP_RIGHT)
        .addVariable("BOTTOM_RIGHT", PivotPreset::BOTTOM_RIGHT)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ContainerType")
        .addVariable("VERTICAL", ContainerType::VERTICAL)
        .addVariable("HORIZONTAL", ContainerType::HORIZONTAL)
        .addVariable("VERTICAL_WRAP", ContainerType::VERTICAL_WRAP)
        .addVariable("HORIZONTAL_WRAP", ContainerType::HORIZONTAL_WRAP)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ScrollbarType")
        .addVariable("VERTICAL", ScrollbarType::VERTICAL)
        .addVariable("HORIZONTAL", ScrollbarType::HORIZONTAL)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ProgressbarType")
        .addVariable("VERTICAL", ProgressbarType::VERTICAL)
        .addVariable("HORIZONTAL", ProgressbarType::HORIZONTAL)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("FogType")
        .addVariable("LINEAR", FogType::LINEAR)
        .addVariable("EXPONENTIAL", FogType::EXPONENTIAL)
        .addVariable("EXPONENTIALSQUARED", FogType::EXPONENTIALSQUARED)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ParticleEmitterShape")
        .addVariable("Box", ParticleEmitterShape::Box)
        .addVariable("Sphere", ParticleEmitterShape::Sphere)
        .addVariable("Hemisphere", ParticleEmitterShape::Hemisphere)
        .addVariable("Circle", ParticleEmitterShape::Circle)
        .addVariable("Cone", ParticleEmitterShape::Cone)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginClass<ActionSystem>("ActionSystem")
        .addFunction("actionStart", &ActionSystem::actionStart)
        .addFunction("actionStop", &ActionSystem::actionStop)
        .addFunction("actionPause", &ActionSystem::actionPause)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<AudioSystem>("AudioSystem")
        .addStaticFunction("stopAll", &AudioSystem::stopAll)
        .addStaticFunction("pauseAll", &AudioSystem::pauseAll)
        .addStaticFunction("resumeAll", &AudioSystem::resumeAll)
        .addStaticFunction("checkActive", &AudioSystem::checkActive)
        .addStaticProperty("globalVolume", &AudioSystem::getGlobalVolume,  &AudioSystem::setGlobalVolume)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<MeshSystem>("MeshSystem")
        .addFunction("createPlane", &MeshSystem::createPlane)
        .addFunction("createBox", &MeshSystem::createBox)
        .addFunction("createSphere", &MeshSystem::createSphere)
        .addFunction("createCylinder", &MeshSystem::createCylinder)
        .addFunction("createCapsule", &MeshSystem::createCapsule)
        .addFunction("createTorus", &MeshSystem::createTorus)
        .addFunction("loadGLTF", &MeshSystem::loadGLTF)
        .addFunction("loadOBJ", &MeshSystem::loadOBJ)
        .addFunction("createInstancedMesh", &MeshSystem::createInstancedMesh)
        .addFunction("removeInstancedMesh", &MeshSystem::removeInstancedMesh)
        .endClass();

#if defined(DORIAX_PHYSICS_2D) || defined(DORIAX_PHYSICS_3D)
    luabridge::getGlobalNamespace(L)
        .beginClass<PhysicsSystem>("PhysicsSystem")
        .addProperty("gravity", (Vector3(PhysicsSystem::*)() const)&PhysicsSystem::getGravity, (void(PhysicsSystem::*)(Vector3))&PhysicsSystem::setGravity)
        .addFunction("setGravity",
            luabridge::overload<Vector3>(&PhysicsSystem::setGravity),
            luabridge::overload<float, float>(&PhysicsSystem::setGravity),
            luabridge::overload<float, float, float>(&PhysicsSystem::setGravity))
        .addProperty("gravity2D", (Vector2(PhysicsSystem::*)() const)&PhysicsSystem::getGravity2D, (void(PhysicsSystem::*)(Vector2))&PhysicsSystem::setGravity2D)
        .addFunction("setGravity2D",
            luabridge::overload<Vector2>(&PhysicsSystem::setGravity2D),
            luabridge::overload<float, float>(&PhysicsSystem::setGravity2D))
        .addProperty("gravity3D", (Vector3(PhysicsSystem::*)() const)&PhysicsSystem::getGravity3D, (void(PhysicsSystem::*)(Vector3))&PhysicsSystem::setGravity3D)
        .addFunction("setGravity3D",
            luabridge::overload<Vector3>(&PhysicsSystem::setGravity3D),
            luabridge::overload<float, float, float>(&PhysicsSystem::setGravity3D))
#ifdef DORIAX_PHYSICS_2D
        .addProperty("pointsToMeterScale2D", &PhysicsSystem::getPointsToMeterScale2D,  &PhysicsSystem::setPointsToMeterScale2D)
        .addProperty("beginContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->beginContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->beginContact2D = L; })
        .addProperty("endContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->endContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->endContact2D = L; })
        .addProperty("beginSensorContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->beginSensorContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->beginSensorContact2D = L; })
        .addProperty("endSensorContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->endSensorContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->endSensorContact2D = L; })
        .addProperty("hitContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->hitContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->hitContact2D = L; })
        .addProperty("preSolve2D", [] (PhysicsSystem* self, lua_State* L) { return &self->preSolve2D; }, [] (PhysicsSystem* self, lua_State* L) { self->preSolve2D = L; })
        .addProperty("shouldCollide2D", [] (PhysicsSystem* self, lua_State* L) { return &self->shouldCollide2D; }, [] (PhysicsSystem* self, lua_State* L) { self->shouldCollide2D = L; })
#endif
#ifdef DORIAX_PHYSICS_3D
        .addProperty("lock3DBodies", &PhysicsSystem::isLock3DBodies, &PhysicsSystem::setLock3DBodies)
        .addProperty("steppingWorld3D", &PhysicsSystem::isSteppingWorld3D)
        .addProperty("onBodyActivated3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onBodyActivated3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onBodyActivated3D = L; })
        .addProperty("onBodyDeactivated3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onBodyDeactivated3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onBodyDeactivated3D = L; })
        .addProperty("onContactAdded3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onContactAdded3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onContactAdded3D = L; })
        .addProperty("onContactPersisted3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onContactPersisted3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onContactPersisted3D = L; })
        .addProperty("onContactRemoved3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onContactRemoved3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onContactRemoved3D = L; })
        .addProperty("shouldCollide3D", [] (PhysicsSystem* self, lua_State* L) { return &self->shouldCollide3D; }, [] (PhysicsSystem* self, lua_State* L) { self->shouldCollide3D = L; })
#endif
#ifdef DORIAX_PHYSICS_2D
        .addFunction("createBody2D", &PhysicsSystem::createBody2D)
        .addFunction("removeBody2D", &PhysicsSystem::removeBody2D)
        .addFunction("loadBody2D", &PhysicsSystem::loadBody2D)
        .addFunction("destroyBody2D", &PhysicsSystem::destroyBody2D)
        .addFunction("destroyShape2D", &PhysicsSystem::destroyShape2D)
        .addFunction("loadDistanceJoint2D", &PhysicsSystem::loadDistanceJoint2D)
        .addFunction("loadRevoluteJoint2D", &PhysicsSystem::loadRevoluteJoint2D)
        .addFunction("loadPrismaticJoint2D", &PhysicsSystem::loadPrismaticJoint2D)
        .addFunction("loadMouseJoint2D", &PhysicsSystem::loadMouseJoint2D)
        .addFunction("loadWheelJoint2D", &PhysicsSystem::loadWheelJoint2D)
        .addFunction("loadWeldJoint2D", &PhysicsSystem::loadWeldJoint2D)
        .addFunction("loadMotorJoint2D", &PhysicsSystem::loadMotorJoint2D)
        .addFunction("destroyJoint2D", &PhysicsSystem::destroyJoint2D)
#endif
#ifdef DORIAX_PHYSICS_3D
        .addFunction("createBody3D", &PhysicsSystem::createBody3D)
        .addFunction("removeBody3D", &PhysicsSystem::removeBody3D)
        .addFunction("loadBody3D", &PhysicsSystem::loadBody3D)
        .addFunction("destroyBody3D", &PhysicsSystem::destroyBody3D)
        .addFunction("destroyShape3D", &PhysicsSystem::destroyShape3D)
        .addFunction("loadFixedJoint3D", &PhysicsSystem::loadFixedJoint3D)
        .addFunction("loadDistanceJoint3D", &PhysicsSystem::loadDistanceJoint3D)
        .addFunction("loadPointJoint3D", &PhysicsSystem::loadPointJoint3D)
        .addFunction("loadHingeJoint3D", &PhysicsSystem::loadHingeJoint3D)
        .addFunction("loadConeJoint3D", &PhysicsSystem::loadConeJoint3D)
        .addFunction("loadPrismaticJoint3D", &PhysicsSystem::loadPrismaticJoint3D)
        .addFunction("loadSwingTwistJoint3D", &PhysicsSystem::loadSwingTwistJoint3D)
        .addFunction("loadSixDOFJoint3D", &PhysicsSystem::loadSixDOFJoint3D)
        .addFunction("loadPathJoint3D", &PhysicsSystem::loadPathJoint3D)
        .addFunction("loadGearJoint3D", &PhysicsSystem::loadGearJoint3D)
        .addFunction("loadRackAndPinionJoint3D", &PhysicsSystem::loadRackAndPinionJoint3D)
        .addFunction("loadPulleyJoint3D", &PhysicsSystem::loadPulleyJoint3D)
        .addFunction("destroyJoint3D", &PhysicsSystem::destroyJoint3D)
        .addFunction("addBroadPhaseLayer3D",
            luabridge::overload<uint8_t, uint32_t>(&PhysicsSystem::addBroadPhaseLayer3D),
            luabridge::overload<uint8_t, uint32_t, uint32_t>(&PhysicsSystem::addBroadPhaseLayer3D))
#endif
        .endClass();
#else
    luabridge::getGlobalNamespace(L)
        .beginClass<PhysicsSystem>("PhysicsSystem")
        .addProperty("gravity", (Vector3(PhysicsSystem::*)() const)&PhysicsSystem::getGravity, (void(PhysicsSystem::*)(Vector3))&PhysicsSystem::setGravity)
        .addProperty("gravity2D", (Vector2(PhysicsSystem::*)() const)&PhysicsSystem::getGravity2D, (void(PhysicsSystem::*)(Vector2))&PhysicsSystem::setGravity2D)
        .addProperty("gravity3D", (Vector3(PhysicsSystem::*)() const)&PhysicsSystem::getGravity3D, (void(PhysicsSystem::*)(Vector3))&PhysicsSystem::setGravity3D)
        .endClass();
#endif

    luabridge::getGlobalNamespace(L)
        .beginClass<RenderSystem>("RenderSystem")
        .addFunction("updateCameraSize", &RenderSystem::updateCameraSize)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<UISystem>("UISystem")
        .addFunction("getAnchorReferenceRect", &UISystem::getAnchorReferenceRect)
        .addFunction("setAnchorReferenceSize", &UISystem::setAnchorReferenceSize)
        .addFunction("clearAnchorReferenceSize", &UISystem::clearAnchorReferenceSize)
        .addFunction("getAnchorReferenceWidth", &UISystem::getAnchorReferenceWidth)
        .addFunction("getAnchorReferenceHeight", &UISystem::getAnchorReferenceHeight)
        .addFunction("isTextEditFocused", &UISystem::isTextEditFocused)
        .addFunction("eventOnCharInput", &UISystem::eventOnCharInput)
        .addFunction("eventOnKeyDown", &UISystem::eventOnKeyDown)
        .addFunction("eventOnPointerDown", &UISystem::eventOnPointerDown)
        .addFunction("eventOnPointerUp", &UISystem::eventOnPointerUp)
        .addFunction("eventOnPointerMove", &UISystem::eventOnPointerMove)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<EntityManager>("EntityManager")
        .addConstructor <void (*) (void)> ()
        .addFunction("createUserEntity", &EntityManager::createUserEntity)
        .addFunction("createSystemEntity", &EntityManager::createSystemEntity)
        .addFunction("destroy", &EntityManager::destroy)
        .addFunction("setSignature", &EntityManager::setSignature)
        .addFunction("getSignature", &EntityManager::getSignature)
        .addFunction("setName", &EntityManager::setName)
        .addFunction("getName", &EntityManager::getName)
        .addFunction("isCreated", &EntityManager::isCreated)
        .addFunction("recreateEntity", &EntityManager::recreateEntity)
        .addFunction("getLastUserEntity", &EntityManager::getLastUserEntity)
        .endClass();

    luabridge::getGlobalNamespace(L)
        // Component fields are the raw data: writing one takes effect when its needUpdate
        // flag is set too, the way the object setters do it. State the systems own is read-only.
        .beginClass<ActionComponent>("ActionComponent")
        .addProperty("state", &ActionComponent::state)
        .addProperty("timecount", &ActionComponent::timecount)
        .addProperty("speed", &ActionComponent::speed, &ActionComponent::speed)
        .addProperty("startTrigger", &ActionComponent::startTrigger, &ActionComponent::startTrigger)
        .addProperty("stopTrigger", &ActionComponent::stopTrigger, &ActionComponent::stopTrigger)
        .addProperty("pauseTrigger", &ActionComponent::pauseTrigger, &ActionComponent::pauseTrigger)
        .addProperty("ownedTarget", &ActionComponent::ownedTarget, &ActionComponent::ownedTarget)
        .addProperty("target", &ActionComponent::target, &ActionComponent::target)
        .addProperty("onStart", [] (ActionComponent* self, lua_State* L) { return &self->onStart; }, [] (ActionComponent* self, lua_State* L) { self->onStart = L; })
        .addProperty("onPause", [] (ActionComponent* self, lua_State* L) { return &self->onPause; }, [] (ActionComponent* self, lua_State* L) { self->onPause = L; })
        .addProperty("onStop", [] (ActionComponent* self, lua_State* L) { return &self->onStop; }, [] (ActionComponent* self, lua_State* L) { self->onStop = L; })
        .addProperty("onStep", [] (ActionComponent* self, lua_State* L) { return &self->onStep; }, [] (ActionComponent* self, lua_State* L) { self->onStep = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ActionFrame>("ActionFrame")
        .addProperty("startTime", &ActionFrame::startTime, &ActionFrame::startTime)
        .addProperty("duration", &ActionFrame::duration, &ActionFrame::duration)
        .addProperty("action", &ActionFrame::action, &ActionFrame::action)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<AnimationComponent>("AnimationComponent")
        .addProperty("actions", &AnimationComponent::actions, &AnimationComponent::actions)
        .addProperty("ownedActions", &AnimationComponent::ownedActions, &AnimationComponent::ownedActions)
        .addProperty("loop", &AnimationComponent::loop, &AnimationComponent::loop)
        .addProperty("duration", &AnimationComponent::duration, &AnimationComponent::duration)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TimedActionComponent>("TimedActionComponent")
        .addProperty("time", &TimedActionComponent::time)
        .addProperty("value", &TimedActionComponent::value)
        .addProperty("duration", &TimedActionComponent::duration, &TimedActionComponent::duration)
        .addProperty("loop", &TimedActionComponent::loop, &TimedActionComponent::loop)
        .addProperty("function", [] (TimedActionComponent* self, lua_State* L) { return &self->function; }, [] (TimedActionComponent* self, lua_State* L) { self->function = L; })
        .endClass();
    
    luabridge::getGlobalNamespace(L)
        .beginClass<UILayoutComponent>("UILayoutComponent")
        .addProperty("width", &UILayoutComponent::width, &UILayoutComponent::width)
        .addProperty("height", &UILayoutComponent::height, &UILayoutComponent::height)
        .addProperty("anchorPointLeft", &UILayoutComponent::anchorPointLeft, &UILayoutComponent::anchorPointLeft)
        .addProperty("anchorPointTop", &UILayoutComponent::anchorPointTop, &UILayoutComponent::anchorPointTop)
        .addProperty("anchorPointRight", &UILayoutComponent::anchorPointRight, &UILayoutComponent::anchorPointRight)
        .addProperty("anchorPointBottom", &UILayoutComponent::anchorPointBottom, &UILayoutComponent::anchorPointBottom)
        .addProperty("anchorOffsetLeft", &UILayoutComponent::anchorOffsetLeft, &UILayoutComponent::anchorOffsetLeft)
        .addProperty("anchorOffsetTop", &UILayoutComponent::anchorOffsetTop, &UILayoutComponent::anchorOffsetTop)
        .addProperty("anchorOffsetRight", &UILayoutComponent::anchorOffsetRight, &UILayoutComponent::anchorOffsetRight)
        .addProperty("anchorOffsetBottom", &UILayoutComponent::anchorOffsetBottom, &UILayoutComponent::anchorOffsetBottom)
        .addProperty("positionOffset", &UILayoutComponent::positionOffset, &UILayoutComponent::positionOffset)
        .addProperty("anchorPreset", &UILayoutComponent::anchorPreset, &UILayoutComponent::anchorPreset)
        .addProperty("usingAnchors", &UILayoutComponent::usingAnchors, &UILayoutComponent::usingAnchors)
        .addProperty("panel", &UILayoutComponent::panel)
        .addProperty("containerBoxIndex", &UILayoutComponent::containerBoxIndex)
        .addProperty("scissor", &UILayoutComponent::scissor, &UILayoutComponent::scissor)
        .addProperty("ignoreScissor", &UILayoutComponent::ignoreScissor, &UILayoutComponent::ignoreScissor)
        .addProperty("ignoreEvents", &UILayoutComponent::ignoreEvents, &UILayoutComponent::ignoreEvents)
        .addProperty("needUpdateSizes", &UILayoutComponent::needUpdateSizes, &UILayoutComponent::needUpdateSizes)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<UIComponent>("UIComponent")
        .addProperty("loaded", &UIComponent::loaded)
        //.addProperty("buffer", &UIComponent::buffer)
        //.addProperty("indices", &UIComponent::indices)
        .addProperty("minBufferCount", &UIComponent::minBufferCount)
        .addProperty("minIndicesCount", &UIComponent::minIndicesCount)
        //.addProperty("render", &UIComponent::render)
        //.addProperty("shader", &UIComponent::shader)
        //.addProperty("shaderProperties", &UIComponent::shaderProperties)
        //.addProperty("slotVSParams", &UIComponent::slotVSParams)
        //.addProperty("slotFSParams", &UIComponent::slotFSParams)
        .addProperty("primitiveType", &UIComponent::primitiveType, &UIComponent::primitiveType)
        .addProperty("vertexCount", &UIComponent::vertexCount)
        .addProperty("texture", &UIComponent::texture, &UIComponent::texture)
        // linear, unlike Image::setColor which converts from sRGB
        .addProperty("color", &UIComponent::color, &UIComponent::color)
        .addProperty("onGetFocus", [] (UIComponent* self, lua_State* L) { return &self->onGetFocus; }, [] (UIComponent* self, lua_State* L) { self->onGetFocus = L; })
        .addProperty("onLostFocus", [] (UIComponent* self, lua_State* L) { return &self->onLostFocus; }, [] (UIComponent* self, lua_State* L) { self->onLostFocus = L; })
        .addProperty("onPointerEnter", [] (UIComponent* self, lua_State* L) { return &self->onPointerEnter; }, [] (UIComponent* self, lua_State* L) { self->onPointerEnter = L; })
        .addProperty("onPointerLeave", [] (UIComponent* self, lua_State* L) { return &self->onPointerLeave; }, [] (UIComponent* self, lua_State* L) { self->onPointerLeave = L; })
        .addProperty("onPointerMove", [] (UIComponent* self, lua_State* L) { return &self->onPointerMove; }, [] (UIComponent* self, lua_State* L) { self->onPointerMove = L; })
        .addProperty("onPointerDown", [] (UIComponent* self, lua_State* L) { return &self->onPointerDown; }, [] (UIComponent* self, lua_State* L) { self->onPointerDown = L; })
        .addProperty("onPointerUp", [] (UIComponent* self, lua_State* L) { return &self->onPointerUp; }, [] (UIComponent* self, lua_State* L) { self->onPointerUp = L; })
        .addProperty("onClick", [] (UIComponent* self, lua_State* L) { return &self->onClick; }, [] (UIComponent* self, lua_State* L) { self->onClick = L; })
        .addProperty("onDoubleClick", [] (UIComponent* self, lua_State* L) { return &self->onDoubleClick; }, [] (UIComponent* self, lua_State* L) { self->onDoubleClick = L; })
        .addProperty("onDragStart", [] (UIComponent* self, lua_State* L) { return &self->onDragStart; }, [] (UIComponent* self, lua_State* L) { self->onDragStart = L; })
        .addProperty("onDrag", [] (UIComponent* self, lua_State* L) { return &self->onDrag; }, [] (UIComponent* self, lua_State* L) { self->onDrag = L; })
        .addProperty("onDragEnd", [] (UIComponent* self, lua_State* L) { return &self->onDragEnd; }, [] (UIComponent* self, lua_State* L) { self->onDragEnd = L; })
        .addProperty("automaticFlipY", &UIComponent::automaticFlipY, &UIComponent::automaticFlipY)
        .addProperty("flipY", &UIComponent::flipY, &UIComponent::flipY)
        .addProperty("pointerMoved", &UIComponent::pointerMoved)
        .addProperty("focused", &UIComponent::focused)
        .addProperty("needReload", &UIComponent::needReload, &UIComponent::needReload)
        .addProperty("needUpdateBuffer", &UIComponent::needUpdateBuffer, &UIComponent::needUpdateBuffer)
        .addProperty("needUpdateTexture", &UIComponent::needUpdateTexture, &UIComponent::needUpdateTexture)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ButtonComponent>("ButtonComponent")
        .addProperty("label", &ButtonComponent::label)
        .addProperty("textureNormal", &ButtonComponent::textureNormal, &ButtonComponent::textureNormal)
        .addProperty("textureHovered", &ButtonComponent::textureHovered, &ButtonComponent::textureHovered)
        .addProperty("texturePressed", &ButtonComponent::texturePressed, &ButtonComponent::texturePressed)
        .addProperty("textureDisabled", &ButtonComponent::textureDisabled, &ButtonComponent::textureDisabled)
        // linear, unlike Button::setColorNormal and friends which convert from sRGB
        .addProperty("colorNormal", &ButtonComponent::colorNormal, &ButtonComponent::colorNormal)
        .addProperty("colorHovered", &ButtonComponent::colorHovered, &ButtonComponent::colorHovered)
        .addProperty("colorPressed", &ButtonComponent::colorPressed, &ButtonComponent::colorPressed)
        .addProperty("colorDisabled", &ButtonComponent::colorDisabled, &ButtonComponent::colorDisabled)
        .addProperty("onPress", [] (ButtonComponent* self, lua_State* L) { return &self->onPress; }, [] (ButtonComponent* self, lua_State* L) { self->onPress = L; })
        .addProperty("onRelease", [] (ButtonComponent* self, lua_State* L) { return &self->onRelease; }, [] (ButtonComponent* self, lua_State* L) { self->onRelease = L; })
        .addProperty("pressed", &ButtonComponent::pressed)
        .addProperty("hovered", &ButtonComponent::hovered)
        .addProperty("disabled", &ButtonComponent::disabled, &ButtonComponent::disabled)
        .addProperty("needUpdateButton", &ButtonComponent::needUpdateButton, &ButtonComponent::needUpdateButton)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PanelComponent>("PanelComponent")
        .addProperty("headerimage", &PanelComponent::headerimage)
        .addProperty("headercontainer", &PanelComponent::headercontainer)
        .addProperty("headertext", &PanelComponent::headertext)
        .addProperty("titleAnchorPreset", &PanelComponent::titleAnchorPreset, &PanelComponent::titleAnchorPreset)
        .addProperty("minWidth", &PanelComponent::minWidth, &PanelComponent::minWidth)
        .addProperty("minHeight", &PanelComponent::minHeight, &PanelComponent::minHeight)
        .addProperty("resizeMargin", &PanelComponent::resizeMargin, &PanelComponent::resizeMargin)
        .addProperty("canMove", &PanelComponent::canMove, &PanelComponent::canMove)
        .addProperty("canResize", &PanelComponent::canResize, &PanelComponent::canResize)
        .addProperty("canBringToFront", &PanelComponent::canBringToFront, &PanelComponent::canBringToFront)
        .addProperty("headerPointerDown", &PanelComponent::headerPointerDown)
        .addProperty("onMove", [] (PanelComponent* self, lua_State* L) { return &self->onMove; }, [] (PanelComponent* self, lua_State* L) { self->onMove = L; })
        .addProperty("onResize", [] (PanelComponent* self, lua_State* L) { return &self->onResize; }, [] (PanelComponent* self, lua_State* L) { self->onResize = L; })
        .addProperty("needUpdatePanel", &PanelComponent::needUpdatePanel, &PanelComponent::needUpdatePanel)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ScrollbarComponent>("ScrollbarComponent")
        .addProperty("bar", &ScrollbarComponent::bar)
        .addProperty("type", &ScrollbarComponent::type, &ScrollbarComponent::type)
        .addProperty("onChange", [] (ScrollbarComponent* self, lua_State* L) { return &self->onChange; }, [] (ScrollbarComponent* self, lua_State* L) { self->onChange = L; })
        .addProperty("barSize", &ScrollbarComponent::barSize, &ScrollbarComponent::barSize)
        .addProperty("step", &ScrollbarComponent::step, &ScrollbarComponent::step)
        .addProperty("barMarginLeft", &ScrollbarComponent::barMarginLeft, &ScrollbarComponent::barMarginLeft)
        .addProperty("barMarginRight", &ScrollbarComponent::barMarginRight, &ScrollbarComponent::barMarginRight)
        .addProperty("barMarginTop", &ScrollbarComponent::barMarginTop, &ScrollbarComponent::barMarginTop)
        .addProperty("barMarginBottom", &ScrollbarComponent::barMarginBottom, &ScrollbarComponent::barMarginBottom)
        .addProperty("barPointerDown", &ScrollbarComponent::barPointerDown)
        .addProperty("barPointerPos", &ScrollbarComponent::barPointerPos)
        .addProperty("needUpdateScrollbar", &ScrollbarComponent::needUpdateScrollbar, &ScrollbarComponent::needUpdateScrollbar)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ProgressbarComponent>("ProgressbarComponent")
        .addProperty("fill", &ProgressbarComponent::fill)
        .addProperty("type", &ProgressbarComponent::type, &ProgressbarComponent::type)
        .addProperty("value", &ProgressbarComponent::value, &ProgressbarComponent::value)
        .addProperty("fillMarginLeft", &ProgressbarComponent::fillMarginLeft, &ProgressbarComponent::fillMarginLeft)
        .addProperty("fillMarginRight", &ProgressbarComponent::fillMarginRight, &ProgressbarComponent::fillMarginRight)
        .addProperty("fillMarginTop", &ProgressbarComponent::fillMarginTop, &ProgressbarComponent::fillMarginTop)
        .addProperty("fillMarginBottom", &ProgressbarComponent::fillMarginBottom, &ProgressbarComponent::fillMarginBottom)
        .addProperty("needUpdateProgressbar", &ProgressbarComponent::needUpdateProgressbar, &ProgressbarComponent::needUpdateProgressbar)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TextEditComponent>("TextEditComponent")
        .addProperty("text", &TextEditComponent::text)
        .addProperty("selection", &TextEditComponent::selection)
        .addProperty("cursor", &TextEditComponent::cursor)
        .addProperty("cursorBlink", &TextEditComponent::cursorBlink, &TextEditComponent::cursorBlink)
        .addProperty("cursorWidth", &TextEditComponent::cursorWidth, &TextEditComponent::cursorWidth)
        // linear, unlike TextEdit::setCursorColor and friends which convert from sRGB
        .addProperty("cursorColor", &TextEditComponent::cursorColor, &TextEditComponent::cursorColor)
        .addProperty("selectionColor", &TextEditComponent::selectionColor, &TextEditComponent::selectionColor)
        .addProperty("placeholderColor", &TextEditComponent::placeholderColor, &TextEditComponent::placeholderColor)
        .addProperty("placeholder", &TextEditComponent::placeholder, &TextEditComponent::placeholder)
        .addProperty("passwordChar", &TextEditComponent::passwordChar, &TextEditComponent::passwordChar)
        .addProperty("cursorIndex", &TextEditComponent::cursorIndex, &TextEditComponent::cursorIndex)
        .addProperty("selectionAnchor", &TextEditComponent::selectionAnchor, &TextEditComponent::selectionAnchor)
        .addProperty("scrollOffset", &TextEditComponent::scrollOffset, &TextEditComponent::scrollOffset)
        .addProperty("disabled", &TextEditComponent::disabled, &TextEditComponent::disabled)
        .addProperty("password", &TextEditComponent::password, &TextEditComponent::password)
        .addProperty("onChange", [] (TextEditComponent* self, lua_State* L) { return &self->onChange; }, [] (TextEditComponent* self, lua_State* L) { self->onChange = L; })
        .addProperty("onSubmit", [] (TextEditComponent* self, lua_State* L) { return &self->onSubmit; }, [] (TextEditComponent* self, lua_State* L) { self->onSubmit = L; })
        .addProperty("needUpdateTextEdit", &TextEditComponent::needUpdateTextEdit, &TextEditComponent::needUpdateTextEdit)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleLifeInitializer>("ParticleLifeInitializer")
        .addProperty("minLife", &ParticleLifeInitializer::minLife, &ParticleLifeInitializer::minLife)
        .addProperty("maxLife", &ParticleLifeInitializer::maxLife, &ParticleLifeInitializer::maxLife)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticlePositionInitializer>("ParticlePositionInitializer")
        .addProperty("shape", &ParticlePositionInitializer::shape, &ParticlePositionInitializer::shape)
        .addProperty("minPosition", &ParticlePositionInitializer::minPosition, &ParticlePositionInitializer::minPosition)
        .addProperty("maxPosition", &ParticlePositionInitializer::maxPosition, &ParticlePositionInitializer::maxPosition)
        .addProperty("radius", &ParticlePositionInitializer::radius, &ParticlePositionInitializer::radius)
        .addProperty("innerRadius", &ParticlePositionInitializer::innerRadius, &ParticlePositionInitializer::innerRadius)
        .addProperty("coneAngle", &ParticlePositionInitializer::coneAngle, &ParticlePositionInitializer::coneAngle)
        .addProperty("coneHeight", &ParticlePositionInitializer::coneHeight, &ParticlePositionInitializer::coneHeight)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleBurst>("ParticleBurst")
        .addProperty("time", &ParticleBurst::time, &ParticleBurst::time)
        .addProperty("minCount", &ParticleBurst::minCount, &ParticleBurst::minCount)
        .addProperty("maxCount", &ParticleBurst::maxCount, &ParticleBurst::maxCount)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticlePositionModifier>("ParticlePositionModifier")
        .addProperty("fromTime", &ParticlePositionModifier::fromTime, &ParticlePositionModifier::fromTime)
        .addProperty("toTime", &ParticlePositionModifier::toTime, &ParticlePositionModifier::toTime)
        .addProperty("fromPosition", &ParticlePositionModifier::fromPosition, &ParticlePositionModifier::fromPosition)
        .addProperty("toPosition", &ParticlePositionModifier::toPosition, &ParticlePositionModifier::toPosition)
        .addProperty("function", [] (ParticlePositionModifier* self, lua_State* L) { return &self->function; }, [] (ParticlePositionModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleVelocityInitializer>("ParticleVelocityInitializer")
        .addProperty("minVelocity", &ParticleVelocityInitializer::minVelocity, &ParticleVelocityInitializer::minVelocity)
        .addProperty("maxVelocity", &ParticleVelocityInitializer::maxVelocity, &ParticleVelocityInitializer::maxVelocity)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleVelocityModifier>("ParticleVelocityModifier")
        .addProperty("fromTime", &ParticleVelocityModifier::fromTime, &ParticleVelocityModifier::fromTime)
        .addProperty("toTime", &ParticleVelocityModifier::toTime, &ParticleVelocityModifier::toTime)
        .addProperty("fromVelocity", &ParticleVelocityModifier::fromVelocity, &ParticleVelocityModifier::fromVelocity)
        .addProperty("toVelocity", &ParticleVelocityModifier::toVelocity, &ParticleVelocityModifier::toVelocity)
        .addProperty("function", [] (ParticleVelocityModifier* self, lua_State* L) { return &self->function; }, [] (ParticleVelocityModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAccelerationInitializer>("ParticleAccelerationInitializer")
        .addProperty("minAcceleration", &ParticleAccelerationInitializer::minAcceleration, &ParticleAccelerationInitializer::minAcceleration)
        .addProperty("maxAcceleration", &ParticleAccelerationInitializer::maxAcceleration, &ParticleAccelerationInitializer::maxAcceleration)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAccelerationModifier>("ParticleAccelerationModifier")
        .addProperty("fromTime", &ParticleAccelerationModifier::fromTime, &ParticleAccelerationModifier::fromTime)
        .addProperty("toTime", &ParticleAccelerationModifier::toTime, &ParticleAccelerationModifier::toTime)
        .addProperty("fromAcceleration", &ParticleAccelerationModifier::fromAcceleration, &ParticleAccelerationModifier::fromAcceleration)
        .addProperty("toAcceleration", &ParticleAccelerationModifier::toAcceleration, &ParticleAccelerationModifier::toAcceleration)
        .addProperty("function", [] (ParticleAccelerationModifier* self, lua_State* L) { return &self->function; }, [] (ParticleAccelerationModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleColorInitializer>("ParticleColorInitializer")
        .addProperty("minColor", &ParticleColorInitializer::minColor, &ParticleColorInitializer::minColor)
        .addProperty("maxColor", &ParticleColorInitializer::maxColor, &ParticleColorInitializer::maxColor)
        .addProperty("useSRGB", &ParticleColorInitializer::useSRGB, &ParticleColorInitializer::useSRGB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleColorModifier>("ParticleColorModifier")
        .addProperty("fromTime", &ParticleColorModifier::fromTime, &ParticleColorModifier::fromTime)
        .addProperty("toTime", &ParticleColorModifier::toTime, &ParticleColorModifier::toTime)
        .addProperty("fromColor", &ParticleColorModifier::fromColor, &ParticleColorModifier::fromColor)
        .addProperty("toColor", &ParticleColorModifier::toColor, &ParticleColorModifier::toColor)
        .addProperty("function", [] (ParticleColorModifier* self, lua_State* L) { return &self->function; }, [] (ParticleColorModifier* self, lua_State* L) { self->function = L; })
        .addProperty("useSRGB", &ParticleColorModifier::useSRGB, &ParticleColorModifier::useSRGB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleColorGradientStop>("ParticleColorGradientStop")
        .addProperty("time", &ParticleColorGradientStop::time, &ParticleColorGradientStop::time)
        .addProperty("color", &ParticleColorGradientStop::color, &ParticleColorGradientStop::color)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleColorGradient>("ParticleColorGradient")
        .addProperty("stops", &ParticleColorGradient::stops, &ParticleColorGradient::stops)
        .addProperty("useSRGB", &ParticleColorGradient::useSRGB, &ParticleColorGradient::useSRGB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAlphaInitializer>("ParticleAlphaInitializer")
        .addProperty("minAlpha", &ParticleAlphaInitializer::minAlpha, &ParticleAlphaInitializer::minAlpha)
        .addProperty("maxAlpha", &ParticleAlphaInitializer::maxAlpha, &ParticleAlphaInitializer::maxAlpha)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAlphaModifier>("ParticleAlphaModifier")
        .addProperty("fromTime", &ParticleAlphaModifier::fromTime, &ParticleAlphaModifier::fromTime)
        .addProperty("toTime", &ParticleAlphaModifier::toTime, &ParticleAlphaModifier::toTime)
        .addProperty("fromAlpha", &ParticleAlphaModifier::fromAlpha, &ParticleAlphaModifier::fromAlpha)
        .addProperty("toAlpha", &ParticleAlphaModifier::toAlpha, &ParticleAlphaModifier::toAlpha)
        .addProperty("function", [] (ParticleAlphaModifier* self, lua_State* L) { return &self->function; }, [] (ParticleAlphaModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSizeInitializer>("ParticleSizeInitializer")
        .addProperty("minSize", &ParticleSizeInitializer::minSize, &ParticleSizeInitializer::minSize)
        .addProperty("maxSize", &ParticleSizeInitializer::maxSize, &ParticleSizeInitializer::maxSize)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSizeModifier>("ParticleSizeModifier")
        .addProperty("fromTime", &ParticleSizeModifier::fromTime, &ParticleSizeModifier::fromTime)
        .addProperty("toTime", &ParticleSizeModifier::toTime, &ParticleSizeModifier::toTime)
        .addProperty("fromSize", &ParticleSizeModifier::fromSize, &ParticleSizeModifier::fromSize)
        .addProperty("toSize", &ParticleSizeModifier::toSize, &ParticleSizeModifier::toSize)
        .addProperty("function", [] (ParticleSizeModifier* self, lua_State* L) { return &self->function; }, [] (ParticleSizeModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSpriteInitializer>("ParticleSpriteInitializer")
        .addProperty("frames", &ParticleSpriteInitializer::frames, &ParticleSpriteInitializer::frames)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSpriteModifier>("ParticleSpriteModifier")
        .addProperty("fromTime", &ParticleSpriteModifier::fromTime, &ParticleSpriteModifier::fromTime)
        .addProperty("toTime", &ParticleSpriteModifier::toTime, &ParticleSpriteModifier::toTime)
        .addProperty("frames", &ParticleSpriteModifier::frames, &ParticleSpriteModifier::frames)
        .addProperty("function", [] (ParticleSpriteModifier* self, lua_State* L) { return &self->function; }, [] (ParticleSpriteModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleRotationInitializer>("ParticleRotationInitializer")
        .addProperty("minRotation", &ParticleRotationInitializer::minRotation, &ParticleRotationInitializer::minRotation)
        .addProperty("maxRotation", &ParticleRotationInitializer::maxRotation, &ParticleRotationInitializer::maxRotation)
        .addProperty("shortestPath", &ParticleRotationInitializer::shortestPath, &ParticleRotationInitializer::shortestPath)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleRotationModifier>("ParticleRotationModifier")
        .addProperty("fromTime", &ParticleRotationModifier::fromTime, &ParticleRotationModifier::fromTime)
        .addProperty("toTime", &ParticleRotationModifier::toTime, &ParticleRotationModifier::toTime)
        .addProperty("fromRotation", &ParticleRotationModifier::fromRotation, &ParticleRotationModifier::fromRotation)
        .addProperty("toRotation", &ParticleRotationModifier::toRotation, &ParticleRotationModifier::toRotation)
        .addProperty("function", [] (ParticleRotationModifier* self, lua_State* L) { return &self->function; }, [] (ParticleRotationModifier* self, lua_State* L) { self->function = L; })
        .addProperty("shortestPath", &ParticleRotationModifier::shortestPath, &ParticleRotationModifier::shortestPath)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleScaleInitializer>("ParticleScaleInitializer")
        .addProperty("minScale", &ParticleScaleInitializer::minScale, &ParticleScaleInitializer::minScale)
        .addProperty("maxScale", &ParticleScaleInitializer::maxScale, &ParticleScaleInitializer::maxScale)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleScaleModifier>("ParticleScaleModifier")
        .addProperty("fromTime", &ParticleScaleModifier::fromTime, &ParticleScaleModifier::fromTime)
        .addProperty("toTime", &ParticleScaleModifier::toTime, &ParticleScaleModifier::toTime)
        .addProperty("fromScale", &ParticleScaleModifier::fromScale, &ParticleScaleModifier::fromScale)
        .addProperty("toScale", &ParticleScaleModifier::toScale, &ParticleScaleModifier::toScale)
        .addProperty("function", [] (ParticleScaleModifier* self, lua_State* L) { return &self->function; }, [] (ParticleScaleModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticlesComponent>("ParticlesComponent")
        .addProperty("newParticlesCount", &ParticlesComponent::newParticlesCount)
        .addProperty("lastUsedParticle", &ParticlesComponent::lastUsedParticle)
        .addProperty("emitter", &ParticlesComponent::emitter, &ParticlesComponent::emitter)
        .addProperty("loop", &ParticlesComponent::loop, &ParticlesComponent::loop)
        .addProperty("localSpace", &ParticlesComponent::localSpace, &ParticlesComponent::localSpace)
        .addProperty("rate", &ParticlesComponent::rate, &ParticlesComponent::rate)
        .addProperty("maxPerUpdate", &ParticlesComponent::maxPerUpdate, &ParticlesComponent::maxPerUpdate)
        .addProperty("bursts", &ParticlesComponent::bursts, &ParticlesComponent::bursts)
        .addProperty("currentBurst", &ParticlesComponent::currentBurst)
        .addProperty("lifeInitializer", &ParticlesComponent::lifeInitializer, &ParticlesComponent::lifeInitializer)
        .addProperty("positionInitializer", &ParticlesComponent::positionInitializer, &ParticlesComponent::positionInitializer)
        .addProperty("positionModifier", &ParticlesComponent::positionModifier, &ParticlesComponent::positionModifier)
        .addProperty("velocityInitializer", &ParticlesComponent::velocityInitializer, &ParticlesComponent::velocityInitializer)
        .addProperty("velocityModifier", &ParticlesComponent::velocityModifier, &ParticlesComponent::velocityModifier)
        .addProperty("accelerationInitializer", &ParticlesComponent::accelerationInitializer, &ParticlesComponent::accelerationInitializer)
        .addProperty("accelerationModifier", &ParticlesComponent::accelerationModifier, &ParticlesComponent::accelerationModifier)
        .addProperty("colorInitializer", &ParticlesComponent::colorInitializer, &ParticlesComponent::colorInitializer)
        .addProperty("colorModifier", &ParticlesComponent::colorModifier, &ParticlesComponent::colorModifier)
        .addProperty("colorGradient", &ParticlesComponent::colorGradient, &ParticlesComponent::colorGradient)
        .addProperty("alphaInitializer", &ParticlesComponent::alphaInitializer, &ParticlesComponent::alphaInitializer)
        .addProperty("alphaModifier", &ParticlesComponent::alphaModifier, &ParticlesComponent::alphaModifier)
        .addProperty("sizeInitializer", &ParticlesComponent::sizeInitializer, &ParticlesComponent::sizeInitializer)
        .addProperty("sizeModifier", &ParticlesComponent::sizeModifier, &ParticlesComponent::sizeModifier)
        .addProperty("spriteInitializer", &ParticlesComponent::spriteInitializer, &ParticlesComponent::spriteInitializer)
        .addProperty("spriteModifier", &ParticlesComponent::spriteModifier, &ParticlesComponent::spriteModifier)
        .addProperty("rotationInitializer", &ParticlesComponent::rotationInitializer, &ParticlesComponent::rotationInitializer)
        .addProperty("rotationModifier", &ParticlesComponent::rotationModifier, &ParticlesComponent::rotationModifier)
        .addProperty("scaleInitializer", &ParticlesComponent::scaleInitializer, &ParticlesComponent::scaleInitializer)
        .addProperty("scaleModifier", &ParticlesComponent::scaleModifier, &ParticlesComponent::scaleModifier)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<SoundComponent>("SoundComponent")
        .addProperty("handle", &SoundComponent::handle)
        .addProperty("state", &SoundComponent::state)
        .addProperty("filename", &SoundComponent::filename, &SoundComponent::filename)
        .addProperty("loaded", &SoundComponent::loaded)
        .addProperty("enableClocked", &SoundComponent::enableClocked, &SoundComponent::enableClocked)
        .addProperty("lastPosition", &SoundComponent::lastPosition)
        .addProperty("startTrigger", &SoundComponent::startTrigger, &SoundComponent::startTrigger)
        .addProperty("stopTrigger", &SoundComponent::stopTrigger, &SoundComponent::stopTrigger)
        .addProperty("pauseTrigger", &SoundComponent::pauseTrigger, &SoundComponent::pauseTrigger)
        .addProperty("onStart", [] (SoundComponent* self, lua_State* L) { return &self->onStart; }, [] (SoundComponent* self, lua_State* L) { self->onStart = L; })
        .addProperty("onPause", [] (SoundComponent* self, lua_State* L) { return &self->onPause; }, [] (SoundComponent* self, lua_State* L) { self->onPause = L; })
        .addProperty("onStop", [] (SoundComponent* self, lua_State* L) { return &self->onStop; }, [] (SoundComponent* self, lua_State* L) { self->onStop = L; })
        .addProperty("volume", &SoundComponent::volume, &SoundComponent::volume)
        .addProperty("pan", &SoundComponent::pan, &SoundComponent::pan)
        .addProperty("speed", &SoundComponent::speed, &SoundComponent::speed)
        .addProperty("looping", &SoundComponent::looping, &SoundComponent::looping)
        .addProperty("loopingPoint", &SoundComponent::loopingPoint, &SoundComponent::loopingPoint)
        .addProperty("protectVoice", &SoundComponent::protectVoice, &SoundComponent::protectVoice)
        .addProperty("inaudibleBehaviorMustTick", &SoundComponent::inaudibleBehaviorMustTick, &SoundComponent::inaudibleBehaviorMustTick)
        .addProperty("inaudibleBehaviorKill", &SoundComponent::inaudibleBehaviorKill, &SoundComponent::inaudibleBehaviorKill)
        .addProperty("minDistance", &SoundComponent::minDistance, &SoundComponent::minDistance)
        .addProperty("maxDistance", &SoundComponent::maxDistance, &SoundComponent::maxDistance)
        .addProperty("attenuationModel", &SoundComponent::attenuationModel, &SoundComponent::attenuationModel)
        .addProperty("attenuationRolloffFactor", &SoundComponent::attenuationRolloffFactor, &SoundComponent::attenuationRolloffFactor)
        .addProperty("dopplerFactor", &SoundComponent::dopplerFactor, &SoundComponent::dopplerFactor)
        .addProperty("length", &SoundComponent::length)
        .addProperty("playingTime", &SoundComponent::playingTime)
        .addProperty("needUpdate", &SoundComponent::needUpdate, &SoundComponent::needUpdate)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TileRectData>("TileRectData")
        .addProperty("name", &TileRectData::name, &TileRectData::name)
        .addProperty("submeshId", &TileRectData::submeshId, &TileRectData::submeshId)
        .addProperty("rect", &TileRectData::rect, &TileRectData::rect)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TileData>("TileData")
        .addProperty("name", &TileData::name, &TileData::name)
        .addProperty("rectId", &TileData::rectId, &TileData::rectId)
        .addProperty("position", &TileData::position, &TileData::position)
        .addProperty("width", &TileData::width, &TileData::width)
        .addProperty("height", &TileData::height, &TileData::height)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<LineData>("LineData")
        .addProperty("pointA", &LineData::pointA, &LineData::pointA)
        .addProperty("pointB", &LineData::pointB, &LineData::pointB)
        .addProperty("colorA", &LineData::colorA, &LineData::colorA)
        .addProperty("colorB", &LineData::colorB, &LineData::colorB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TilemapComponent>("TilemapComponent")
        .addProperty("width", &TilemapComponent::width, &TilemapComponent::width)
        .addProperty("height", &TilemapComponent::height, &TilemapComponent::height)
        .addProperty("automaticFlipY", &TilemapComponent::automaticFlipY, &TilemapComponent::automaticFlipY)
        .addProperty("flipY", &TilemapComponent::flipY, &TilemapComponent::flipY)
        .addProperty("textureScaleFactor", &TilemapComponent::textureScaleFactor, &TilemapComponent::textureScaleFactor)
        .addProperty("reserveTiles", &TilemapComponent::reserveTiles, &TilemapComponent::reserveTiles)
        .addProperty("numTiles", &TilemapComponent::numTiles, &TilemapComponent::numTiles)
        //.addProperty("tilesRect", &TilemapComponent::tilesRect, &TilemapComponent::tilesRect)
        //.addProperty("tiles", &TilemapComponent::tiles, &TilemapComponent::tiles)
        .addProperty("needUpdateTilemap", &TilemapComponent::needUpdateTilemap, &TilemapComponent::needUpdateTilemap)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PointData>("PointData")
        .addProperty("position", &PointData::position, &PointData::position)
        .addProperty("color", &PointData::color, &PointData::color)
        .addProperty("size", &PointData::size, &PointData::size)
        .addProperty("rotation", &PointData::rotation, &PointData::rotation)
        .addProperty("textureRect", &PointData::textureRect, &PointData::textureRect)
        .addProperty("visible", &PointData::visible, &PointData::visible)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<InstanceData>("InstanceData")
        .addProperty("position", &InstanceData::position, &InstanceData::position)
        .addProperty("rotation", &InstanceData::rotation, &InstanceData::rotation)
        .addProperty("scale", &InstanceData::scale, &InstanceData::scale)
        .addProperty("color", &InstanceData::color, &InstanceData::color)
        .addProperty("textureRect", &InstanceData::textureRect, &InstanceData::textureRect)
        .addProperty("visible", &InstanceData::visible, &InstanceData::visible)
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}
