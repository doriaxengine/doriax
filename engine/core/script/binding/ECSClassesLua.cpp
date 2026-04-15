//
// (c) 2026 Eduardo Doria.
//

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
#include "component/ParticlesComponent.h"
#include "component/AudioComponent.h"
#include "component/SpriteComponent.h"
#include "component/FogComponent.h"
#include "component/TilemapComponent.h"

using namespace doriax;

void LuaBinding::registerECSClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("AudioState")
        .addVariable("Playing", AudioState::Playing)
        .addVariable("Paused", AudioState::Paused)
        .addVariable("Stopped", AudioState::Stopped)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("AudioAttenuation")
        .addVariable("NO_ATTENUATION", AudioAttenuation::NO_ATTENUATION)
        .addVariable("INVERSE_DISTANCE", AudioAttenuation::INVERSE_DISTANCE)
        .addVariable("LINEAR_DISTANCE", AudioAttenuation::LINEAR_DISTANCE)
        .addVariable("EXPONENTIAL_DISTANCE", AudioAttenuation::EXPONENTIAL_DISTANCE)
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
        .beginNamespace("FogType")
        .addVariable("LINEAR", FogType::LINEAR)
        .addVariable("EXPONENTIAL", FogType::EXPONENTIAL)
        .addVariable("EXPONENTIALSQUARED", FogType::EXPONENTIALSQUARED)
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

    luabridge::getGlobalNamespace(L)
        .beginClass<PhysicsSystem>("PhysicsSystem")
        .addProperty("gravity", (Vector3(PhysicsSystem::*)() const)&PhysicsSystem::getGravity, (void(PhysicsSystem::*)(Vector3))&PhysicsSystem::setGravity)
        .addFunction("setGravity",
            luabridge::overload<Vector3>(&PhysicsSystem::setGravity),
            luabridge::overload<float, float>(&PhysicsSystem::setGravity),
            luabridge::overload<float, float, float>(&PhysicsSystem::setGravity))
        .addProperty("pointsToMeterScale2D", &PhysicsSystem::getPointsToMeterScale2D,  &PhysicsSystem::setPointsToMeterScale2D)
        .addProperty("lock3DBodies", &PhysicsSystem::isLock3DBodies, &PhysicsSystem::setLock3DBodies)
        .addProperty("beginContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->beginContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->beginContact2D = L; })
        .addProperty("endContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->endContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->endContact2D = L; })
        .addProperty("beginSensorContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->beginSensorContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->beginSensorContact2D = L; })
        .addProperty("endSensorContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->endSensorContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->endSensorContact2D = L; })
        .addProperty("hitContact2D", [] (PhysicsSystem* self, lua_State* L) { return &self->hitContact2D; }, [] (PhysicsSystem* self, lua_State* L) { self->hitContact2D = L; })
        .addProperty("preSolve2D", [] (PhysicsSystem* self, lua_State* L) { return &self->preSolve2D; }, [] (PhysicsSystem* self, lua_State* L) { self->preSolve2D = L; })
        .addProperty("shouldCollide2D", [] (PhysicsSystem* self, lua_State* L) { return &self->shouldCollide2D; }, [] (PhysicsSystem* self, lua_State* L) { self->shouldCollide2D = L; })

        .addProperty("onBodyActivated3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onBodyActivated3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onBodyActivated3D = L; })
        .addProperty("onBodyDeactivated3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onBodyDeactivated3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onBodyDeactivated3D = L; })
        .addProperty("onContactAdded3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onContactAdded3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onContactAdded3D = L; })
        .addProperty("onContactPersisted3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onContactPersisted3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onContactPersisted3D = L; })
        .addProperty("onContactRemoved3D", [] (PhysicsSystem* self, lua_State* L) { return &self->onContactRemoved3D; }, [] (PhysicsSystem* self, lua_State* L) { self->onContactRemoved3D = L; })
        .addProperty("shouldCollide3D", [] (PhysicsSystem* self, lua_State* L) { return &self->shouldCollide3D; }, [] (PhysicsSystem* self, lua_State* L) { self->shouldCollide3D = L; })

        .addFunction("createBody2D", &PhysicsSystem::createBody2D)
        .addFunction("removeBody2D", &PhysicsSystem::removeBody2D)

        .addFunction("createBody3D", &PhysicsSystem::createBody3D)
        .addFunction("removeBody3D", &PhysicsSystem::removeBody3D)

        .addFunction("loadBody2D", &PhysicsSystem::loadBody2D)
        .addFunction("destroyBody2D", &PhysicsSystem::destroyBody2D)
        .addFunction("loadBody3D", &PhysicsSystem::loadBody3D)
        .addFunction("destroyBody3D", &PhysicsSystem::destroyBody3D)
        //.addFunction("loadShape2D", &PhysicsSystem::loadShape2D) // has body2d components
        .addFunction("destroyShape2D", &PhysicsSystem::destroyShape2D)
        //.addFunction("loadShape3D", &PhysicsSystem::loadShape3D) // has jolt components
        .addFunction("destroyShape3D", &PhysicsSystem::destroyShape3D)

        .addFunction("loadDistanceJoint2D", &PhysicsSystem::loadDistanceJoint2D)
        .addFunction("loadRevoluteJoint2D", &PhysicsSystem::loadRevoluteJoint2D)
        .addFunction("loadPrismaticJoint2D", &PhysicsSystem::loadPrismaticJoint2D)
        //.addFunction("loadPulleyJoint2D", &PhysicsSystem::loadPulleyJoint2D)
        //.addFunction("loadGearJoint2D", &PhysicsSystem::loadGearJoint2D)
        .addFunction("loadMouseJoint2D", &PhysicsSystem::loadMouseJoint2D)
        .addFunction("loadWheelJoint2D", &PhysicsSystem::loadWheelJoint2D)
        .addFunction("loadWeldJoint2D", &PhysicsSystem::loadWeldJoint2D)
        .addFunction("loadMotorJoint2D", &PhysicsSystem::loadMotorJoint2D)
        .addFunction("destroyJoint2D", &PhysicsSystem::destroyJoint2D)

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
        .endClass();

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
        .beginClass<ActionComponent>("ActionComponent")
        .addProperty("state", &ActionComponent::state)
        .addProperty("timecount", &ActionComponent::timecount)
        .addProperty("speed", &ActionComponent::speed)
        .addProperty("startTrigger", &ActionComponent::startTrigger)
        .addProperty("stopTrigger", &ActionComponent::stopTrigger)
        .addProperty("pauseTrigger", &ActionComponent::pauseTrigger)
        .addProperty("ownedTarget", &ActionComponent::ownedTarget)
        .addProperty("target", &ActionComponent::target)
        .addProperty("onStart", [] (ActionComponent* self, lua_State* L) { return &self->onStart; }, [] (ActionComponent* self, lua_State* L) { self->onStart = L; })
        .addProperty("onPause", [] (ActionComponent* self, lua_State* L) { return &self->onPause; }, [] (ActionComponent* self, lua_State* L) { self->onPause = L; })
        .addProperty("onStop", [] (ActionComponent* self, lua_State* L) { return &self->onStop; }, [] (ActionComponent* self, lua_State* L) { self->onStop = L; })
        .addProperty("onStep", [] (ActionComponent* self, lua_State* L) { return &self->onStep; }, [] (ActionComponent* self, lua_State* L) { self->onStep = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ActionFrame>("ActionFrame")
        .addProperty("startTime", &ActionFrame::startTime)
        .addProperty("duration", &ActionFrame::duration)
        .addProperty("action", &ActionFrame::action)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<AnimationComponent>("AnimationComponent")
        .addProperty("actions", &AnimationComponent::actions)
        .addProperty("ownedActions", &AnimationComponent::ownedActions)
        .addProperty("loop", &AnimationComponent::loop)
        .addProperty("m", &AnimationComponent::name)
        .addProperty("duration", &AnimationComponent::duration)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TimedActionComponent>("TimedActionComponent")
        .addProperty("time", &TimedActionComponent::time)
        .addProperty("value", &TimedActionComponent::value)
        .addProperty("duration", &TimedActionComponent::duration)
        .addProperty("loop", &TimedActionComponent::loop)
        .addProperty("function", [] (TimedActionComponent* self, lua_State* L) { return &self->function; }, [] (TimedActionComponent* self, lua_State* L) { self->function = L; })
        .endClass();
    
    luabridge::getGlobalNamespace(L)
        .beginClass<UILayoutComponent>("UILayoutComponent")
        .addProperty("width", &UILayoutComponent::width)
        .addProperty("height", &UILayoutComponent::height)
        .addProperty("anchorPointLeft", &UILayoutComponent::anchorPointLeft)
        .addProperty("anchorPointTop", &UILayoutComponent::anchorPointTop)
        .addProperty("anchorPointRight", &UILayoutComponent::anchorPointRight)
        .addProperty("anchorPointBottom", &UILayoutComponent::anchorPointBottom)
        .addProperty("anchorOffsetLeft", &UILayoutComponent::anchorOffsetLeft)
        .addProperty("anchorOffsetTop", &UILayoutComponent::anchorOffsetTop)
        .addProperty("anchorOffsetRight", &UILayoutComponent::anchorOffsetRight)
        .addProperty("anchorOffsetBottom", &UILayoutComponent::anchorOffsetBottom)
        .addProperty("positionOffset", &UILayoutComponent::positionOffset)
        .addProperty("anchorPreset", &UILayoutComponent::anchorPreset)
        .addProperty("usingAnchors", &UILayoutComponent::usingAnchors)
        .addProperty("panel", &UILayoutComponent::panel)
        .addProperty("containerBoxIndex", &UILayoutComponent::containerBoxIndex)
        .addProperty("scissor", &UILayoutComponent::scissor)
        .addProperty("ignoreScissor", &UILayoutComponent::ignoreScissor)
        .addProperty("ignoreEvents", &UILayoutComponent::ignoreEvents)
        .addProperty("needUpdateSizes", &UILayoutComponent::needUpdateSizes)
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
        .addProperty("primitiveType", &UIComponent::primitiveType)
        .addProperty("vertexCount", &UIComponent::vertexCount)
        .addProperty("texture", &UIComponent::texture)
        .addProperty("color", &UIComponent::color)
        .addProperty("onGetFocus", [] (UIComponent* self, lua_State* L) { return &self->onGetFocus; }, [] (UIComponent* self, lua_State* L) { self->onGetFocus = L; })
        .addProperty("onLostFocus", [] (UIComponent* self, lua_State* L) { return &self->onLostFocus; }, [] (UIComponent* self, lua_State* L) { self->onLostFocus = L; })
        .addProperty("onPointerMove", [] (UIComponent* self, lua_State* L) { return &self->onPointerMove; }, [] (UIComponent* self, lua_State* L) { self->onPointerMove = L; })
        .addProperty("onPointerDown", [] (UIComponent* self, lua_State* L) { return &self->onPointerDown; }, [] (UIComponent* self, lua_State* L) { self->onPointerDown = L; })
        .addProperty("onPointerUp", [] (UIComponent* self, lua_State* L) { return &self->onPointerUp; }, [] (UIComponent* self, lua_State* L) { self->onPointerUp = L; })
        .addProperty("automaticFlipY", &UIComponent::automaticFlipY)
        .addProperty("flipY", &UIComponent::flipY)
        .addProperty("pointerMoved", &UIComponent::pointerMoved)
        .addProperty("focused", &UIComponent::focused)
        .addProperty("needReload", &UIComponent::needReload)
        .addProperty("needUpdateBuffer", &UIComponent::needUpdateBuffer)
        .addProperty("needUpdateTexture", &UIComponent::needUpdateTexture)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ButtonComponent>("ButtonComponent")
        .addProperty("label", &ButtonComponent::label)
        .addProperty("textureNormal", &ButtonComponent::textureNormal)
        .addProperty("texturePressed", &ButtonComponent::texturePressed)
        .addProperty("textureDisabled", &ButtonComponent::textureDisabled)
        .addProperty("colorNormal", &ButtonComponent::colorNormal)
        .addProperty("colorPressed", &ButtonComponent::colorPressed)
        .addProperty("colorDisabled", &ButtonComponent::colorDisabled)
        .addProperty("onPress", [] (ButtonComponent* self, lua_State* L) { return &self->onPress; }, [] (ButtonComponent* self, lua_State* L) { self->onPress = L; })
        .addProperty("onRelease", [] (ButtonComponent* self, lua_State* L) { return &self->onRelease; }, [] (ButtonComponent* self, lua_State* L) { self->onRelease = L; })
        .addProperty("pressed", &ButtonComponent::pressed)
        .addProperty("disabled", &ButtonComponent::disabled)
        .addProperty("needUpdateButton", &ButtonComponent::needUpdateButton)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PanelComponent>("PanelComponent")
        .addProperty("headerimage", &PanelComponent::headerimage)
        .addProperty("headercontainer", &PanelComponent::headercontainer)
        .addProperty("headertext", &PanelComponent::headertext)
        .addProperty("titleAnchorPreset", &PanelComponent::titleAnchorPreset)
        .addProperty("minWidth", &PanelComponent::minWidth)
        .addProperty("minHeight", &PanelComponent::minHeight)
        .addProperty("resizeMargin", &PanelComponent::resizeMargin)
        .addProperty("canMove", &PanelComponent::canMove)
        .addProperty("canResize", &PanelComponent::canResize)
        .addProperty("canBringToFront", &PanelComponent::canBringToFront)
        .addProperty("headerPointerDown", &PanelComponent::headerPointerDown)
        .addProperty("onMove", [] (PanelComponent* self, lua_State* L) { return &self->onMove; }, [] (PanelComponent* self, lua_State* L) { self->onMove = L; })
        .addProperty("onResize", [] (PanelComponent* self, lua_State* L) { return &self->onResize; }, [] (PanelComponent* self, lua_State* L) { self->onResize = L; })
        .addProperty("needUpdatePanel", &PanelComponent::needUpdatePanel)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ScrollbarComponent>("ScrollbarComponent")
        .addProperty("bar", &ScrollbarComponent::bar)
        .addProperty("type", &ScrollbarComponent::type)
        .addProperty("onChange", [] (ScrollbarComponent* self, lua_State* L) { return &self->onChange; }, [] (ScrollbarComponent* self, lua_State* L) { self->onChange = L; })
        .addProperty("barSize", &ScrollbarComponent::barSize)
        .addProperty("step", &ScrollbarComponent::step)
        .addProperty("barPointerDown", &ScrollbarComponent::barPointerDown)
        .addProperty("barPointerPos", &ScrollbarComponent::barPointerPos)
        .addProperty("needUpdateScrollbar", &ScrollbarComponent::needUpdateScrollbar)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleLifeInitializer>("ParticleLifeInitializer")
        .addProperty("minLife", &ParticleLifeInitializer::minLife)
        .addProperty("maxLife", &ParticleLifeInitializer::maxLife)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticlePositionInitializer>("ParticlePositionInitializer")
        .addProperty("minPosition", &ParticlePositionInitializer::minPosition)
        .addProperty("maxPosition", &ParticlePositionInitializer::maxPosition)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticlePositionModifier>("ParticlePositionModifier")
        .addProperty("fromTime", &ParticlePositionModifier::fromTime)
        .addProperty("toTime", &ParticlePositionModifier::toTime)
        .addProperty("fromPosition", &ParticlePositionModifier::fromPosition)
        .addProperty("toPosition", &ParticlePositionModifier::toPosition)
        .addProperty("function", [] (ParticlePositionModifier* self, lua_State* L) { return &self->function; }, [] (ParticlePositionModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleVelocityInitializer>("ParticleVelocityInitializer")
        .addProperty("minVelocity", &ParticleVelocityInitializer::minVelocity)
        .addProperty("maxVelocity", &ParticleVelocityInitializer::maxVelocity)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleVelocityModifier>("ParticleVelocityModifier")
        .addProperty("fromTime", &ParticleVelocityModifier::fromTime)
        .addProperty("toTime", &ParticleVelocityModifier::toTime)
        .addProperty("fromVelocity", &ParticleVelocityModifier::fromVelocity)
        .addProperty("toVelocity", &ParticleVelocityModifier::toVelocity)
        .addProperty("function", [] (ParticleVelocityModifier* self, lua_State* L) { return &self->function; }, [] (ParticleVelocityModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAccelerationInitializer>("ParticleAccelerationInitializer")
        .addProperty("minAcceleration", &ParticleAccelerationInitializer::minAcceleration)
        .addProperty("maxAcceleration", &ParticleAccelerationInitializer::maxAcceleration)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAccelerationModifier>("ParticleAccelerationModifier")
        .addProperty("fromTime", &ParticleAccelerationModifier::fromTime)
        .addProperty("toTime", &ParticleAccelerationModifier::toTime)
        .addProperty("fromAcceleration", &ParticleAccelerationModifier::fromAcceleration)
        .addProperty("toAcceleration", &ParticleAccelerationModifier::toAcceleration)
        .addProperty("function", [] (ParticleAccelerationModifier* self, lua_State* L) { return &self->function; }, [] (ParticleAccelerationModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleColorInitializer>("ParticleColorInitializer")
        .addProperty("minColor", &ParticleColorInitializer::minColor)
        .addProperty("maxColor", &ParticleColorInitializer::maxColor)
        .addProperty("useSRGB", &ParticleColorInitializer::useSRGB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleColorModifier>("ParticleColorModifier")
        .addProperty("fromTime", &ParticleColorModifier::fromTime)
        .addProperty("toTime", &ParticleColorModifier::toTime)
        .addProperty("fromColor", &ParticleColorModifier::fromColor)
        .addProperty("toColor", &ParticleColorModifier::toColor)
        .addProperty("function", [] (ParticleColorModifier* self, lua_State* L) { return &self->function; }, [] (ParticleColorModifier* self, lua_State* L) { self->function = L; })
        .addProperty("useSRGB", &ParticleColorModifier::useSRGB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAlphaInitializer>("ParticleAlphaInitializer")
        .addProperty("minAlpha", &ParticleAlphaInitializer::minAlpha)
        .addProperty("maxAlpha", &ParticleAlphaInitializer::maxAlpha)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleAlphaModifier>("ParticleAlphaModifier")
        .addProperty("fromTime", &ParticleAlphaModifier::fromTime)
        .addProperty("toTime", &ParticleAlphaModifier::toTime)
        .addProperty("fromAlpha", &ParticleAlphaModifier::fromAlpha)
        .addProperty("toAlpha", &ParticleAlphaModifier::toAlpha)
        .addProperty("function", [] (ParticleAlphaModifier* self, lua_State* L) { return &self->function; }, [] (ParticleAlphaModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSizeInitializer>("ParticleSizeInitializer")
        .addProperty("minSize", &ParticleSizeInitializer::minSize)
        .addProperty("maxSize", &ParticleSizeInitializer::maxSize)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSizeModifier>("ParticleSizeModifier")
        .addProperty("fromTime", &ParticleSizeModifier::fromTime)
        .addProperty("toTime", &ParticleSizeModifier::toTime)
        .addProperty("fromSize", &ParticleSizeModifier::fromSize)
        .addProperty("toSize", &ParticleSizeModifier::toSize)
        .addProperty("function", [] (ParticleSizeModifier* self, lua_State* L) { return &self->function; }, [] (ParticleSizeModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSpriteInitializer>("ParticleSpriteInitializer")
        .addProperty("frames", &ParticleSpriteInitializer::frames)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleSpriteModifier>("ParticleSpriteModifier")
        .addProperty("fromTime", &ParticleSpriteModifier::fromTime)
        .addProperty("toTime", &ParticleSpriteModifier::toTime)
        .addProperty("frames", &ParticleSpriteModifier::frames)
        .addProperty("function", [] (ParticleSpriteModifier* self, lua_State* L) { return &self->function; }, [] (ParticleSpriteModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleRotationInitializer>("ParticleRotationInitializer")
        .addProperty("minRotation", &ParticleRotationInitializer::minRotation)
        .addProperty("maxRotation", &ParticleRotationInitializer::maxRotation)
        .addProperty("shortestPath", &ParticleRotationInitializer::shortestPath)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleRotationModifier>("ParticleRotationModifier")
        .addProperty("fromTime", &ParticleRotationModifier::fromTime)
        .addProperty("toTime", &ParticleRotationModifier::toTime)
        .addProperty("fromRotation", &ParticleRotationModifier::fromRotation)
        .addProperty("toRotation", &ParticleRotationModifier::toRotation)
        .addProperty("function", [] (ParticleRotationModifier* self, lua_State* L) { return &self->function; }, [] (ParticleRotationModifier* self, lua_State* L) { self->function = L; })
        .addProperty("shortestPath", &ParticleRotationModifier::shortestPath)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleScaleInitializer>("ParticleScaleInitializer")
        .addProperty("minScale", &ParticleScaleInitializer::minScale)
        .addProperty("maxScale", &ParticleScaleInitializer::maxScale)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticleScaleModifier>("ParticleScaleModifier")
        .addProperty("fromTime", &ParticleScaleModifier::fromTime)
        .addProperty("toTime", &ParticleScaleModifier::toTime)
        .addProperty("fromScale", &ParticleScaleModifier::fromScale)
        .addProperty("toScale", &ParticleScaleModifier::toScale)
        .addProperty("function", [] (ParticleScaleModifier* self, lua_State* L) { return &self->function; }, [] (ParticleScaleModifier* self, lua_State* L) { self->function = L; })
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ParticlesComponent>("ParticlesComponent")
        .addProperty("newParticlesCount", &ParticlesComponent::newParticlesCount)
        .addProperty("lastUsedParticle", &ParticlesComponent::lastUsedParticle)
        .addProperty("emitter", &ParticlesComponent::emitter)
        .addProperty("loop", &ParticlesComponent::loop)
        .addProperty("rate", &ParticlesComponent::rate)
        .addProperty("lifeInitializer", &ParticlesComponent::lifeInitializer)
        .addProperty("lifeInitializer", &ParticlesComponent::lifeInitializer)
        .addProperty("positionInitializer", &ParticlesComponent::positionInitializer)
        .addProperty("positionModifier", &ParticlesComponent::positionModifier)
        .addProperty("velocityInitializer", &ParticlesComponent::velocityInitializer)
        .addProperty("velocityModifier", &ParticlesComponent::velocityModifier)
        .addProperty("accelerationInitializer", &ParticlesComponent::accelerationInitializer)
        .addProperty("accelerationModifier", &ParticlesComponent::accelerationModifier)
        .addProperty("colorInitializer", &ParticlesComponent::colorInitializer)
        .addProperty("colorModifier", &ParticlesComponent::colorModifier)
        .addProperty("alphaInitializer", &ParticlesComponent::alphaInitializer)
        .addProperty("alphaModifier", &ParticlesComponent::alphaModifier)
        .addProperty("sizeInitializer", &ParticlesComponent::sizeInitializer)
        .addProperty("sizeModifier", &ParticlesComponent::sizeModifier)
        .addProperty("spriteInitializer", &ParticlesComponent::spriteInitializer)
        .addProperty("spriteModifier", &ParticlesComponent::spriteModifier)
        .addProperty("rotationInitializer", &ParticlesComponent::rotationInitializer)
        .addProperty("rotationModifier", &ParticlesComponent::rotationModifier)
        .addProperty("scaleInitializer", &ParticlesComponent::scaleInitializer)
        .addProperty("scaleModifier", &ParticlesComponent::scaleModifier)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<AudioComponent>("AudioComponent")
        .addProperty("handle", &AudioComponent::handle)
        .addProperty("state", &AudioComponent::state)
        .addProperty("filename", &AudioComponent::filename)
        .addProperty("loaded", &AudioComponent::loaded)
        .addProperty("enableClocked", &AudioComponent::enableClocked)
        .addProperty("enable3D", &AudioComponent::enable3D)
        .addProperty("lastPosition", &AudioComponent::lastPosition)
        .addProperty("startTrigger", &AudioComponent::startTrigger)
        .addProperty("stopTrigger", &AudioComponent::stopTrigger)
        .addProperty("pauseTrigger", &AudioComponent::pauseTrigger)
        .addProperty("onStart", [] (AudioComponent* self, lua_State* L) { return &self->onStart; }, [] (AudioComponent* self, lua_State* L) { self->onStart = L; })
        .addProperty("onPause", [] (AudioComponent* self, lua_State* L) { return &self->onPause; }, [] (AudioComponent* self, lua_State* L) { self->onPause = L; })
        .addProperty("onStop", [] (AudioComponent* self, lua_State* L) { return &self->onStop; }, [] (AudioComponent* self, lua_State* L) { self->onStop = L; })
        .addProperty("volume", &AudioComponent::volume)
        .addProperty("pan", &AudioComponent::pan)
        .addProperty("looping", &AudioComponent::looping)
        .addProperty("loopingPoint", &AudioComponent::loopingPoint)
        .addProperty("protectVoice", &AudioComponent::protectVoice)
        .addProperty("inaudibleBehaviorMustTick", &AudioComponent::inaudibleBehaviorMustTick)
        .addProperty("inaudibleBehaviorKill", &AudioComponent::inaudibleBehaviorKill)
        .addProperty("minDistance", &AudioComponent::minDistance)
        .addProperty("maxDistance", &AudioComponent::maxDistance)
        .addProperty("attenuationModel", &AudioComponent::attenuationModel)
        .addProperty("attenuationRolloffFactor", &AudioComponent::attenuationRolloffFactor)
        .addProperty("dopplerFactor", &AudioComponent::dopplerFactor)
        .addProperty("length", &AudioComponent::length)
        .addProperty("playingTime", &AudioComponent::playingTime)
        .addProperty("needUpdate", &AudioComponent::needUpdate)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TileRectData>("TileRectData")
        .addProperty("name", &TileRectData::name)
        .addProperty("submeshId", &TileRectData::submeshId)
        .addProperty("rect", &TileRectData::rect)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TileData>("TileData")
        .addProperty("name", &TileData::name)
        .addProperty("rectId", &TileData::rectId)
        .addProperty("position", &TileData::position)
        .addProperty("width", &TileData::width)
        .addProperty("height", &TileData::height)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<LineData>("LineData")
        .addProperty("pointA", &LineData::pointA)
        .addProperty("pointB", &LineData::pointB)
        .addProperty("colorA", &LineData::colorA)
        .addProperty("colorB", &LineData::colorB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TilemapComponent>("TilemapComponent")
        .addProperty("width", &TilemapComponent::width)
        .addProperty("height", &TilemapComponent::height)
        .addProperty("automaticFlipY", &TilemapComponent::automaticFlipY)
        .addProperty("flipY", &TilemapComponent::flipY)
        .addProperty("textureScaleFactor", &TilemapComponent::textureScaleFactor)
        .addProperty("reserveTiles", &TilemapComponent::reserveTiles)
        .addProperty("numTiles", &TilemapComponent::numTiles)
        //.addProperty("tilesRect", &TilemapComponent::tilesRect)
        //.addProperty("tiles", &TilemapComponent::tiles)
        .addProperty("needUpdateTilemap", &TilemapComponent::needUpdateTilemap)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PointData>("PointData")
        .addProperty("position", &PointData::position)
        .addProperty("color", &PointData::color)
        .addProperty("size", &PointData::size)
        .addProperty("rotation", &PointData::rotation)
        .addProperty("textureRect", &PointData::textureRect)
        .addProperty("visible", &PointData::visible)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<InstanceData>("InstanceData")
        .addProperty("position", &InstanceData::position)
        .addProperty("rotation", &InstanceData::rotation)
        .addProperty("scale", &InstanceData::scale)
        .addProperty("color", &InstanceData::color)
        .addProperty("textureRect", &InstanceData::textureRect)
        .addProperty("visible", &InstanceData::visible)
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}