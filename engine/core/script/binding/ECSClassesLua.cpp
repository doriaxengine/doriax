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
#include "component/Transform.h"
#include "component/CameraComponent.h"
#include "component/LightComponent.h"
#include "component/Light2DComponent.h"
#include "component/Occluder2DComponent.h"
#include "component/SkyComponent.h"
#include "component/MeshComponent.h"
#include "component/InstancedMeshComponent.h"
#include "component/ModelComponent.h"
#include "component/BoneComponent.h"
#include "component/MeshPolygonComponent.h"
#include "component/PolygonComponent.h"
#include "component/PointsComponent.h"
#include "component/LinesComponent.h"
#include "component/TerrainComponent.h"
#include "component/WaterComponent.h"
#include "component/WaterExclusionComponent.h"
#include "component/MirrorComponent.h"
#include "component/ReflectionProbeComponent.h"
#include "component/ImageComponent.h"
#include "component/TextComponent.h"
#include "component/PositionActionComponent.h"
#include "component/RotationActionComponent.h"
#include "component/ScaleActionComponent.h"
#include "component/ColorActionComponent.h"
#include "component/AlphaActionComponent.h"
#include "component/SpriteAnimationComponent.h"
#include "component/KeyframeTracksComponent.h"
#include "component/TranslateTracksComponent.h"
#include "component/RotateTracksComponent.h"
#include "component/ScaleTracksComponent.h"
#include "component/MorphTracksComponent.h"
#include "component/Body2DComponent.h"
#include "component/Joint2DComponent.h"
#include "component/Body3DComponent.h"
#include "component/Joint3DComponent.h"

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
        .beginClass<SubSystem>("SubSystem")
        .addProperty("paused", &SubSystem::isPaused, &SubSystem::setPaused)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ActionSystem, SubSystem>("ActionSystem")
        // keeps the default true when omitted, a nil bool reads as false
        .addFunction("actionStart", [] (ActionSystem* self, Entity entity, std::optional<bool> newRun) { self->actionStart(entity, newRun.value_or(true)); })
        .addFunction("actionStop", &ActionSystem::actionStop)
        .addFunction("actionPause", &ActionSystem::actionPause)
        .addFunction("particleActionReset", &ActionSystem::particleActionReset)
        .addFunction("getDuration", luabridge::overload<Entity>(&ActionSystem::getDuration))
        .addFunction("getFrameDuration", &ActionSystem::getFrameDuration)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<AudioSystem, SubSystem>("AudioSystem")
        .addStaticFunction("stopAll", &AudioSystem::stopAll)
        .addStaticFunction("pauseAll", &AudioSystem::pauseAll)
        .addStaticFunction("resumeAll", &AudioSystem::resumeAll)
        .addStaticFunction("checkActive", &AudioSystem::checkActive)
        .addStaticProperty("globalVolume", &AudioSystem::getGlobalVolume,  &AudioSystem::setGlobalVolume)
        .addFunction("stopSceneSounds", &AudioSystem::stopSceneSounds)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<MeshSystem, SubSystem>("MeshSystem")
        .addFunction("createPlane", &MeshSystem::createPlane)
        .addFunction("createWall", &MeshSystem::createWall)
        .addFunction("createBox", &MeshSystem::createBox)
        .addFunction("createSphere", &MeshSystem::createSphere)
        .addFunction("createCylinder", &MeshSystem::createCylinder)
        .addFunction("createCapsule", &MeshSystem::createCapsule)
        .addFunction("createTorus", &MeshSystem::createTorus)
        // keeps the default true when omitted, a nil bool reads as false
        .addFunction("loadGLTF", [] (MeshSystem* self, Entity entity, const std::string& filename, bool asyncLoad, bool skipEntities, std::optional<bool> changeRootTransform) {
            return self->loadGLTF(entity, filename, asyncLoad, skipEntities, changeRootTransform.value_or(true));
        })
        .addFunction("loadOBJ", &MeshSystem::loadOBJ)
        .addFunction("hasPendingAsyncModelLoads", &MeshSystem::hasPendingAsyncModelLoads)
        .addFunction("isAsyncModelLoadPending", luabridge::overload<Entity, const std::string&>(&MeshSystem::isAsyncModelLoadPending))
        .addFunction("cancelAsyncModelLoad", &MeshSystem::cancelAsyncModelLoad)
        .addFunction("cancelAsyncModelLoads", &MeshSystem::cancelAsyncModelLoads)
        .addFunction("createInstancedMesh", &MeshSystem::createInstancedMesh)
        .addFunction("removeInstancedMesh", &MeshSystem::removeInstancedMesh)
        .addFunction("hasInstancedMesh", &MeshSystem::hasInstancedMesh)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<PhysicsSystem, SubSystem>("PhysicsSystem")
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
        .addProperty("interpolation", &PhysicsSystem::isInterpolation, &PhysicsSystem::setInterpolation)
        .addFunction("clearSubscriptions", &PhysicsSystem::clearSubscriptions)
        .addFunction("removeSubscriptionsByTag", &PhysicsSystem::removeSubscriptionsByTag)
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
        // True inside a contact callback, where a body can be moved but not created
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
        //.addFunction("loadShape2D", &PhysicsSystem::loadShape2D) // has body2d components
        .addFunction("destroyShape2D", &PhysicsSystem::destroyShape2D)
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
#endif
#ifdef DORIAX_PHYSICS_3D
        .addFunction("createBody3D", &PhysicsSystem::createBody3D)
        .addFunction("removeBody3D", &PhysicsSystem::removeBody3D)
        .addFunction("loadBody3D", &PhysicsSystem::loadBody3D)
        .addFunction("destroyBody3D", &PhysicsSystem::destroyBody3D)
        //.addFunction("loadShape3D", &PhysicsSystem::loadShape3D) // has jolt components
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

    luabridge::getGlobalNamespace(L)
        .deriveClass<RenderSystem, SubSystem>("RenderSystem")
        .addFunction("updateCameraSize", &RenderSystem::updateCameraSize)
        .addFunction("isAllLoaded", &RenderSystem::isAllLoaded)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<UISystem, SubSystem>("UISystem")
        .addFunction("getAnchorReferenceRect", &UISystem::getAnchorReferenceRect)
        .addFunction("setAnchorReferenceSize", &UISystem::setAnchorReferenceSize)
        .addFunction("clearAnchorReferenceSize", &UISystem::clearAnchorReferenceSize)
        .addFunction("getAnchorReferenceWidth", &UISystem::getAnchorReferenceWidth)
        .addFunction("getAnchorReferenceHeight", &UISystem::getAnchorReferenceHeight)
        .addFunction("parentToLayoutSpace", &UISystem::parentToLayoutSpace)
        .addFunction("layoutToParentSpace", &UISystem::layoutToParentSpace)
        .addFunction("isTextEditFocused", &UISystem::isTextEditFocused)
        .addFunction("hasPendingImages", &UISystem::hasPendingImages)
        .addFunction("resetButtonStates", &UISystem::resetButtonStates)
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
        .addProperty("startOffset", &ActionComponent::startOffset, &ActionComponent::startOffset)
        .addProperty("randomStart", &ActionComponent::randomStart, &ActionComponent::randomStart)
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
        .addProperty("pingPong", &TimedActionComponent::pingPong, &TimedActionComponent::pingPong)
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
        .addProperty("pivot", &UILayoutComponent::pivot, &UILayoutComponent::pivot)
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
        .addProperty("scaleHovered", &ButtonComponent::scaleHovered, &ButtonComponent::scaleHovered)
        .addProperty("scalePressed", &ButtonComponent::scalePressed, &ButtonComponent::scalePressed)
        .addProperty("transitionTime", &ButtonComponent::transitionTime, &ButtonComponent::transitionTime)
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
        .addProperty("loadFailed", &SoundComponent::loadFailed)
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

    luabridge::getGlobalNamespace(L)
        .beginClass<Transform>("Transform")
        .addProperty("position", &Transform::position, &Transform::position)
        .addProperty("rotation", &Transform::rotation, &Transform::rotation)
        .addProperty("scale", &Transform::scale, &Transform::scale)
        .addProperty("worldPosition", &Transform::worldPosition)
        .addProperty("worldRotation", &Transform::worldRotation)
        .addProperty("worldScale", &Transform::worldScale)
        .addProperty("localMatrix", &Transform::localMatrix)
        .addProperty("modelMatrix", &Transform::modelMatrix)
        .addProperty("visible", &Transform::visible, &Transform::visible)
        // reparent with addChild
        .addProperty("parent", &Transform::parent)
        .addProperty("distanceToCamera", &Transform::distanceToCamera)
        .addProperty("billboardRotation", &Transform::billboardRotation, &Transform::billboardRotation)
        .addProperty("billboard", &Transform::billboard, &Transform::billboard)
        .addProperty("fakeBillboard", &Transform::fakeBillboard, &Transform::fakeBillboard)
        .addProperty("cylindricalBillboard", &Transform::cylindricalBillboard, &Transform::cylindricalBillboard)
        .addProperty("needUpdateChildVisibility", &Transform::needUpdateChildVisibility, &Transform::needUpdateChildVisibility)
        .addProperty("needUpdate", &Transform::needUpdate, &Transform::needUpdate)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<CameraComponent>("CameraComponent")
        .addProperty("type", &CameraComponent::type, &CameraComponent::type)
        .addProperty("projectionMatrix", &CameraComponent::projectionMatrix)
        .addProperty("viewMatrix", &CameraComponent::viewMatrix)
        .addProperty("viewProjectionMatrix", &CameraComponent::viewProjectionMatrix)
        .addProperty("target", &CameraComponent::target, &CameraComponent::target)
        .addProperty("worldTarget", &CameraComponent::worldTarget)
        .addProperty("up", &CameraComponent::up, &CameraComponent::up)
        .addProperty("direction", &CameraComponent::direction)
        .addProperty("right", &CameraComponent::right)
        .addProperty("worldUp", &CameraComponent::worldUp)
        .addProperty("worldDirection", &CameraComponent::worldDirection)
        .addProperty("worldRight", &CameraComponent::worldRight)
        .addProperty("leftClip", &CameraComponent::leftClip, &CameraComponent::leftClip)
        .addProperty("rightClip", &CameraComponent::rightClip, &CameraComponent::rightClip)
        .addProperty("bottomClip", &CameraComponent::bottomClip, &CameraComponent::bottomClip)
        .addProperty("topClip", &CameraComponent::topClip, &CameraComponent::topClip)
        .addProperty("yfov", &CameraComponent::yfov, &CameraComponent::yfov)
        .addProperty("aspect", &CameraComponent::aspect, &CameraComponent::aspect)
        .addProperty("nearClip", &CameraComponent::nearClip, &CameraComponent::nearClip)
        .addProperty("farClip", &CameraComponent::farClip, &CameraComponent::farClip)
        .addProperty("renderToTexture", &CameraComponent::renderToTexture, &CameraComponent::renderToTexture)
        .addProperty("framebuffer", &CameraComponent::framebuffer)
        .addProperty("framebufferWidth", &CameraComponent::framebufferWidth, &CameraComponent::framebufferWidth)
        .addProperty("framebufferHeight", &CameraComponent::framebufferHeight, &CameraComponent::framebufferHeight)
        .addProperty("framebufferFilter", &CameraComponent::framebufferFilter, &CameraComponent::framebufferFilter)
        .addProperty("transparentSort", &CameraComponent::transparentSort, &CameraComponent::transparentSort)
        .addProperty("depthTest", &CameraComponent::depthTest, &CameraComponent::depthTest)
        .addProperty("useTarget", &CameraComponent::useTarget, &CameraComponent::useTarget)
        .addProperty("autoResize", &CameraComponent::autoResize, &CameraComponent::autoResize)
        .addProperty("hasCustomViewMatrix", &CameraComponent::hasCustomViewMatrix, &CameraComponent::hasCustomViewMatrix)
        .addProperty("customViewMatrix", &CameraComponent::customViewMatrix, &CameraComponent::customViewMatrix)
        .addProperty("invertCulling", &CameraComponent::invertCulling, &CameraComponent::invertCulling)
        .addProperty("needUpdate", &CameraComponent::needUpdate, &CameraComponent::needUpdate)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<LightComponent>("LightComponent")
        .addProperty("type", &LightComponent::type, &LightComponent::type)
        .addProperty("direction", &LightComponent::direction, &LightComponent::direction)
        .addProperty("worldDirection", &LightComponent::worldDirection)
        // linear, unlike Light::setColor which converts from sRGB
        .addProperty("color", &LightComponent::color, &LightComponent::color)
        .addProperty("range", &LightComponent::range, &LightComponent::range)
        .addProperty("intensity", &LightComponent::intensity, &LightComponent::intensity)
        .addProperty("innerConeCos", &LightComponent::innerConeCos, &LightComponent::innerConeCos)
        .addProperty("outerConeCos", &LightComponent::outerConeCos, &LightComponent::outerConeCos)
        .addProperty("spotMask", &LightComponent::spotMask)
        .addProperty("shadows", &LightComponent::shadows, &LightComponent::shadows)
        .addProperty("automaticShadowCamera", &LightComponent::automaticShadowCamera, &LightComponent::automaticShadowCamera)
        .addProperty("shadowBias", &LightComponent::shadowBias, &LightComponent::shadowBias)
        .addProperty("shadowNormalBias", &LightComponent::shadowNormalBias, &LightComponent::shadowNormalBias)
        .addProperty("mapResolution", &LightComponent::mapResolution, &LightComponent::mapResolution)
        .addProperty("shadowCameraNearFar", &LightComponent::shadowCameraNearFar, &LightComponent::shadowCameraNearFar)
        .addProperty("numShadowCascades", &LightComponent::numShadowCascades, &LightComponent::numShadowCascades)
        .addProperty("needUpdateShadowCamera", &LightComponent::needUpdateShadowCamera, &LightComponent::needUpdateShadowCamera)
        .addProperty("needUpdateShadowMap", &LightComponent::needUpdateShadowMap, &LightComponent::needUpdateShadowMap)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Light2DComponent>("Light2DComponent")
        // linear, unlike Light2D::setColor which converts from sRGB
        .addProperty("color", &Light2DComponent::color, &Light2DComponent::color)
        .addProperty("intensity", &Light2DComponent::intensity, &Light2DComponent::intensity)
        .addProperty("range", &Light2DComponent::range, &Light2DComponent::range)
        .addProperty("falloff", &Light2DComponent::falloff, &Light2DComponent::falloff)
        .addProperty("height", &Light2DComponent::height, &Light2DComponent::height)
        .addProperty("shadows", &Light2DComponent::shadows, &Light2DComponent::shadows)
        .addProperty("shadowBias", &Light2DComponent::shadowBias, &Light2DComponent::shadowBias)
        .addProperty("shadowSoftness", &Light2DComponent::shadowSoftness, &Light2DComponent::shadowSoftness)
        .addProperty("mapResolution", &Light2DComponent::mapResolution, &Light2DComponent::mapResolution)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Occluder2DComponent>("Occluder2DComponent")
        .addProperty("shape", &Occluder2DComponent::shape, &Occluder2DComponent::shape)
        .addProperty("points", &Occluder2DComponent::points, &Occluder2DComponent::points)
        .addProperty("closed", &Occluder2DComponent::closed, &Occluder2DComponent::closed)
        .addProperty("enabled", &Occluder2DComponent::enabled, &Occluder2DComponent::enabled)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<FogComponent>("FogComponent")
        .addProperty("type", &FogComponent::type, &FogComponent::type)
        // linear, unlike Fog::setColor which converts from sRGB
        .addProperty("color", &FogComponent::color, &FogComponent::color)
        .addProperty("density", &FogComponent::density, &FogComponent::density)
        .addProperty("linearStart", &FogComponent::linearStart, &FogComponent::linearStart)
        .addProperty("linearEnd", &FogComponent::linearEnd, &FogComponent::linearEnd)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<SkyComponent>("SkyComponent")
        .addProperty("loaded", &SkyComponent::loaded)
        .addProperty("customShader", &SkyComponent::customShader, &SkyComponent::customShader)
        .addProperty("texture", &SkyComponent::texture, &SkyComponent::texture)
        .addProperty("color", &SkyComponent::color, &SkyComponent::color)
        .addProperty("rotation", &SkyComponent::rotation, &SkyComponent::rotation)
        .addProperty("visible", &SkyComponent::visible, &SkyComponent::visible)
        .addProperty("needUpdateEnvironment", &SkyComponent::needUpdateEnvironment, &SkyComponent::needUpdateEnvironment)
        .addProperty("needUpdateTexture", &SkyComponent::needUpdateTexture, &SkyComponent::needUpdateTexture)
        .addProperty("needUpdateSky", &SkyComponent::needUpdateSky, &SkyComponent::needUpdateSky)
        .addProperty("needReload", &SkyComponent::needReload, &SkyComponent::needReload)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<MeshComponent>("MeshComponent")
        .addProperty("loaded", &MeshComponent::loaded)
        .addProperty("vertexCount", &MeshComponent::vertexCount)
        .addProperty("numSubmeshes", &MeshComponent::numSubmeshes)
        .addProperty("customShader", &MeshComponent::customShader, &MeshComponent::customShader)
        .addProperty("customDepthShader", &MeshComponent::customDepthShader, &MeshComponent::customDepthShader)
        .addProperty("aabb", &MeshComponent::aabb)
        .addProperty("worldAABB", &MeshComponent::worldAABB)
        .addProperty("receiveLights", &MeshComponent::receiveLights, &MeshComponent::receiveLights)
        .addProperty("receiveIBL", &MeshComponent::receiveIBL, &MeshComponent::receiveIBL)
        .addProperty("castShadows", &MeshComponent::castShadows, &MeshComponent::castShadows)
        .addProperty("receiveShadows", &MeshComponent::receiveShadows, &MeshComponent::receiveShadows)
        .addProperty("shadowsBillboard", &MeshComponent::shadowsBillboard, &MeshComponent::shadowsBillboard)
        .addProperty("renderInReflectionProbes", &MeshComponent::renderInReflectionProbes, &MeshComponent::renderInReflectionProbes)
        .addProperty("transparent", &MeshComponent::transparent, &MeshComponent::transparent)
        .addProperty("autoTransparency", &MeshComponent::autoTransparency, &MeshComponent::autoTransparency)
        .addProperty("lodEnabled", &MeshComponent::lodEnabled, &MeshComponent::lodEnabled)
        .addProperty("lodBias", &MeshComponent::lodBias, &MeshComponent::lodBias)
        .addProperty("cullingMode", &MeshComponent::cullingMode, &MeshComponent::cullingMode)
        .addProperty("windingOrder", &MeshComponent::windingOrder, &MeshComponent::windingOrder)
        .addProperty("needUpdateAABB", &MeshComponent::needUpdateAABB, &MeshComponent::needUpdateAABB)
        .addProperty("needUpdateBuffer", &MeshComponent::needUpdateBuffer, &MeshComponent::needUpdateBuffer)
        .addProperty("needReload", &MeshComponent::needReload, &MeshComponent::needReload)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<InstancedMeshComponent>("InstancedMeshComponent")
        .addProperty("instances", &InstancedMeshComponent::instances, &InstancedMeshComponent::instances)
        // buffer size, set with Mesh.maxInstances
        .addProperty("maxInstances", &InstancedMeshComponent::maxInstances)
        .addProperty("numVisible", &InstancedMeshComponent::numVisible)
        .addProperty("cullInstances", &InstancedMeshComponent::cullInstances, &InstancedMeshComponent::cullInstances)
        .addProperty("cullDistance", &InstancedMeshComponent::cullDistance, &InstancedMeshComponent::cullDistance)
        .addProperty("distanceFade", &InstancedMeshComponent::distanceFade, &InstancedMeshComponent::distanceFade)
        .addProperty("fadeStart", &InstancedMeshComponent::fadeStart, &InstancedMeshComponent::fadeStart)
        .addProperty("fadeEnd", &InstancedMeshComponent::fadeEnd, &InstancedMeshComponent::fadeEnd)
        .addProperty("instancedBillboard", &InstancedMeshComponent::instancedBillboard, &InstancedMeshComponent::instancedBillboard)
        .addProperty("instancedCylindricalBillboard", &InstancedMeshComponent::instancedCylindricalBillboard, &InstancedMeshComponent::instancedCylindricalBillboard)
        .addProperty("needUpdateBuffer", &InstancedMeshComponent::needUpdateBuffer, &InstancedMeshComponent::needUpdateBuffer)
        .addProperty("needUpdateInstances", &InstancedMeshComponent::needUpdateInstances, &InstancedMeshComponent::needUpdateInstances)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ModelComponent>("ModelComponent")
        .addProperty("skeleton", &ModelComponent::skeleton)
        .addProperty("bonesNameMapping", &ModelComponent::bonesNameMapping)
        .addProperty("morphNameMapping", &ModelComponent::morphNameMapping)
        .addProperty("animations", &ModelComponent::animations)
        .addProperty("filename", &ModelComponent::filename, &ModelComponent::filename)
        .addProperty("mergeStaticMeshes", &ModelComponent::mergeStaticMeshes, &ModelComponent::mergeStaticMeshes)
        .addProperty("needUpdateModel", &ModelComponent::needUpdateModel, &ModelComponent::needUpdateModel)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<BoneComponent>("BoneComponent")
        .addProperty("model", &BoneComponent::model)
        .addProperty("index", &BoneComponent::index)
        .addProperty("bindPosition", &BoneComponent::bindPosition)
        .addProperty("bindRotation", &BoneComponent::bindRotation)
        .addProperty("bindScale", &BoneComponent::bindScale)
        .addProperty("offsetMatrix", &BoneComponent::offsetMatrix)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<SpriteComponent>("SpriteComponent")
        .addProperty("width", &SpriteComponent::width, &SpriteComponent::width)
        .addProperty("height", &SpriteComponent::height, &SpriteComponent::height)
        .addProperty("automaticFlipY", &SpriteComponent::automaticFlipY, &SpriteComponent::automaticFlipY)
        .addProperty("flipY", &SpriteComponent::flipY, &SpriteComponent::flipY)
        .addProperty("textureScaleFactor", &SpriteComponent::textureScaleFactor, &SpriteComponent::textureScaleFactor)
        .addProperty("numFramesRect", &SpriteComponent::numFramesRect)
        .addProperty("pivotPreset", &SpriteComponent::pivotPreset, &SpriteComponent::pivotPreset)
        .addProperty("needUpdateSprite", &SpriteComponent::needUpdateSprite, &SpriteComponent::needUpdateSprite)
        .addProperty("needUpdateBatch", &SpriteComponent::needUpdateBatch, &SpriteComponent::needUpdateBatch)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PolygonPoint>("PolygonPoint")
        .addConstructor<void (*) (void)>()
        .addProperty("position", &PolygonPoint::position, &PolygonPoint::position)
        .addProperty("color", &PolygonPoint::color, &PolygonPoint::color)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<MeshPolygonComponent>("MeshPolygonComponent")
        .addProperty("width", &MeshPolygonComponent::width)
        .addProperty("height", &MeshPolygonComponent::height)
        .addProperty("automaticFlipY", &MeshPolygonComponent::automaticFlipY, &MeshPolygonComponent::automaticFlipY)
        .addProperty("flipY", &MeshPolygonComponent::flipY, &MeshPolygonComponent::flipY)
        .addProperty("points", &MeshPolygonComponent::points, &MeshPolygonComponent::points)
        .addProperty("needUpdatePolygon", &MeshPolygonComponent::needUpdatePolygon, &MeshPolygonComponent::needUpdatePolygon)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PolygonComponent>("PolygonComponent")
        .addProperty("points", &PolygonComponent::points, &PolygonComponent::points)
        .addProperty("needUpdatePolygon", &PolygonComponent::needUpdatePolygon, &PolygonComponent::needUpdatePolygon)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PointsComponent>("PointsComponent")
        .addProperty("loaded", &PointsComponent::loaded)
        .addProperty("points", &PointsComponent::points, &PointsComponent::points)
        .addProperty("numFramesRect", &PointsComponent::numFramesRect)
        // buffer size, set with Points.maxPoints
        .addProperty("maxPoints", &PointsComponent::maxPoints)
        .addProperty("numVisible", &PointsComponent::numVisible)
        .addProperty("aabb", &PointsComponent::aabb)
        .addProperty("customShader", &PointsComponent::customShader, &PointsComponent::customShader)
        .addProperty("texture", &PointsComponent::texture, &PointsComponent::texture)
        .addProperty("transparent", &PointsComponent::transparent, &PointsComponent::transparent)
        .addProperty("autoTransparency", &PointsComponent::autoTransparency, &PointsComponent::autoTransparency)
        .addProperty("needUpdate", &PointsComponent::needUpdate, &PointsComponent::needUpdate)
        .addProperty("needUpdateBuffer", &PointsComponent::needUpdateBuffer, &PointsComponent::needUpdateBuffer)
        .addProperty("needUpdateTexture", &PointsComponent::needUpdateTexture, &PointsComponent::needUpdateTexture)
        .addProperty("needReload", &PointsComponent::needReload, &PointsComponent::needReload)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<LinesComponent>("LinesComponent")
        .addProperty("loaded", &LinesComponent::loaded)
        .addProperty("lines", &LinesComponent::lines, &LinesComponent::lines)
        // buffer size, set with Lines.maxLines
        .addProperty("maxLines", &LinesComponent::maxLines)
        .addProperty("customShader", &LinesComponent::customShader, &LinesComponent::customShader)
        .addProperty("needUpdateBuffer", &LinesComponent::needUpdateBuffer, &LinesComponent::needUpdateBuffer)
        .addProperty("needReload", &LinesComponent::needReload, &LinesComponent::needReload)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TerrainFoliageLayer>("TerrainFoliageLayer")
        .addConstructor<void (*) (void)>()
        .addProperty("meshPath", &TerrainFoliageLayer::meshPath, &TerrainFoliageLayer::meshPath)
        .addProperty("densityMap", &TerrainFoliageLayer::densityMap, &TerrainFoliageLayer::densityMap)
        .addProperty("density", &TerrainFoliageLayer::density, &TerrainFoliageLayer::density)
        .addProperty("minScale", &TerrainFoliageLayer::minScale, &TerrainFoliageLayer::minScale)
        .addProperty("maxScale", &TerrainFoliageLayer::maxScale, &TerrainFoliageLayer::maxScale)
        .addProperty("rotationJitter", &TerrainFoliageLayer::rotationJitter, &TerrainFoliageLayer::rotationJitter)
        .addProperty("alignToNormal", &TerrainFoliageLayer::alignToNormal, &TerrainFoliageLayer::alignToNormal)
        .addProperty("minSlope", &TerrainFoliageLayer::minSlope, &TerrainFoliageLayer::minSlope)
        .addProperty("maxSlope", &TerrainFoliageLayer::maxSlope, &TerrainFoliageLayer::maxSlope)
        .addProperty("minHeight", &TerrainFoliageLayer::minHeight, &TerrainFoliageLayer::minHeight)
        .addProperty("maxHeight", &TerrainFoliageLayer::maxHeight, &TerrainFoliageLayer::maxHeight)
        .addProperty("drawDistance", &TerrainFoliageLayer::drawDistance, &TerrainFoliageLayer::drawDistance)
        .addProperty("minDrawDistance", &TerrainFoliageLayer::minDrawDistance, &TerrainFoliageLayer::minDrawDistance)
        .addProperty("seed", &TerrainFoliageLayer::seed, &TerrainFoliageLayer::seed)
        .addProperty("castShadows", &TerrainFoliageLayer::castShadows, &TerrainFoliageLayer::castShadows)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TerrainComponent>("TerrainComponent")
        .addProperty("heightMap", &TerrainComponent::heightMap, &TerrainComponent::heightMap)
        .addProperty("blendMaps", &TerrainComponent::blendMaps, &TerrainComponent::blendMaps)
        .addProperty("surfaceLayers", &TerrainComponent::surfaceLayers, &TerrainComponent::surfaceLayers)
        .addProperty("foliageLayers", &TerrainComponent::foliageLayers, &TerrainComponent::foliageLayers)
        .addProperty("foliageDistanceScale", &TerrainComponent::foliageDistanceScale, &TerrainComponent::foliageDistanceScale)
        .addProperty("heightMapLoaded", &TerrainComponent::heightMapLoaded)
        .addProperty("offset", &TerrainComponent::offset, &TerrainComponent::offset)
        .addProperty("terrainSize", &TerrainComponent::terrainSize, &TerrainComponent::terrainSize)
        .addProperty("maxHeight", &TerrainComponent::maxHeight, &TerrainComponent::maxHeight)
        .addProperty("textureBaseTiles", &TerrainComponent::textureBaseTiles, &TerrainComponent::textureBaseTiles)
        .addProperty("textureDetailTiles", &TerrainComponent::textureDetailTiles, &TerrainComponent::textureDetailTiles)
        // read each frame, only validated on a rebuild
        .addProperty("autoSetRanges", &TerrainComponent::autoSetRanges)
        .addProperty("ranges", &TerrainComponent::ranges)
        .addProperty("resolution", &TerrainComponent::resolution)
        .addProperty("rootGridSize", &TerrainComponent::rootGridSize)
        .addProperty("levels", &TerrainComponent::levels)
        .addProperty("needUpdateTerrain", &TerrainComponent::needUpdateTerrain, &TerrainComponent::needUpdateTerrain)
        .addProperty("needUpdateTexture", &TerrainComponent::needUpdateTexture, &TerrainComponent::needUpdateTexture)
        .addProperty("needUpdateFoliage", &TerrainComponent::needUpdateFoliage, &TerrainComponent::needUpdateFoliage)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<WaterComponent>("WaterComponent")
        .addProperty("loaded", &WaterComponent::loaded)
        .addProperty("aabb", &WaterComponent::aabb)
        .addProperty("worldAABB", &WaterComponent::worldAABB)
        .addProperty("customShader", &WaterComponent::customShader, &WaterComponent::customShader)
        .addProperty("customUnderwaterShader", &WaterComponent::customUnderwaterShader, &WaterComponent::customUnderwaterShader)
        .addProperty("size", &WaterComponent::size, &WaterComponent::size)
        .addProperty("subdivisions", &WaterComponent::subdivisions, &WaterComponent::subdivisions)
        // linear, unlike the Water setters which convert from sRGB
        .addProperty("shallowColor", &WaterComponent::shallowColor, &WaterComponent::shallowColor)
        .addProperty("deepColor", &WaterComponent::deepColor, &WaterComponent::deepColor)
        .addProperty("depthFade", &WaterComponent::depthFade, &WaterComponent::depthFade)
        .addProperty("waveHeight", &WaterComponent::waveHeight, &WaterComponent::waveHeight)
        .addProperty("waveLength", &WaterComponent::waveLength, &WaterComponent::waveLength)
        .addProperty("waveSpeed", &WaterComponent::waveSpeed, &WaterComponent::waveSpeed)
        .addProperty("waveDirection", &WaterComponent::waveDirection, &WaterComponent::waveDirection)
        .addProperty("waveSteepness", &WaterComponent::waveSteepness, &WaterComponent::waveSteepness)
        .addProperty("normalTexture", &WaterComponent::normalTexture, &WaterComponent::normalTexture)
        .addProperty("normalScale", &WaterComponent::normalScale, &WaterComponent::normalScale)
        .addProperty("normalStrength", &WaterComponent::normalStrength, &WaterComponent::normalStrength)
        .addProperty("rippleSpeed", &WaterComponent::rippleSpeed, &WaterComponent::rippleSpeed)
        .addProperty("reflectivity", &WaterComponent::reflectivity, &WaterComponent::reflectivity)
        .addProperty("specularIntensity", &WaterComponent::specularIntensity, &WaterComponent::specularIntensity)
        .addProperty("roughness", &WaterComponent::roughness, &WaterComponent::roughness)
        .addProperty("foamColor", &WaterComponent::foamColor, &WaterComponent::foamColor)
        .addProperty("shoreFoam", &WaterComponent::shoreFoam, &WaterComponent::shoreFoam)
        .addProperty("crestFoam", &WaterComponent::crestFoam, &WaterComponent::crestFoam)
        .addProperty("receiveShadows", &WaterComponent::receiveShadows, &WaterComponent::receiveShadows)
        .addProperty("depthEffects", &WaterComponent::depthEffects, &WaterComponent::depthEffects)
        .addProperty("planarReflection", &WaterComponent::planarReflection, &WaterComponent::planarReflection)
        .addProperty("reflectionDistortion", &WaterComponent::reflectionDistortion, &WaterComponent::reflectionDistortion)
        .addProperty("refraction", &WaterComponent::refraction, &WaterComponent::refraction)
        .addProperty("refractionDistortion", &WaterComponent::refractionDistortion, &WaterComponent::refractionDistortion)
        .addProperty("caustics", &WaterComponent::caustics, &WaterComponent::caustics)
        .addProperty("causticsScale", &WaterComponent::causticsScale, &WaterComponent::causticsScale)
        .addProperty("underwater", &WaterComponent::underwater, &WaterComponent::underwater)
        .addProperty("buoyancy", &WaterComponent::buoyancy, &WaterComponent::buoyancy)
        .addProperty("buoyancyDepth", &WaterComponent::buoyancyDepth, &WaterComponent::buoyancyDepth)
        .addProperty("needUpdateTexture", &WaterComponent::needUpdateTexture, &WaterComponent::needUpdateTexture)
        .addProperty("needReload", &WaterComponent::needReload, &WaterComponent::needReload)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<WaterExclusionComponent>("WaterExclusionComponent")
        .addProperty("shape", &WaterExclusionComponent::shape, &WaterExclusionComponent::shape)
        .addProperty("center", &WaterExclusionComponent::center, &WaterExclusionComponent::center)
        .addProperty("size", &WaterExclusionComponent::size, &WaterExclusionComponent::size)
        .addProperty("aabb", &WaterExclusionComponent::aabb)
        .addProperty("worldAABB", &WaterExclusionComponent::worldAABB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<MirrorComponent>("MirrorComponent")
        .addProperty("normal", &MirrorComponent::normal, &MirrorComponent::normal)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ReflectionProbeComponent>("ReflectionProbeComponent")
        .addProperty("mode", &ReflectionProbeComponent::mode, &ReflectionProbeComponent::mode)
        .addProperty("updateMode", &ReflectionProbeComponent::updateMode, &ReflectionProbeComponent::updateMode)
        .addProperty("boxOffset", &ReflectionProbeComponent::boxOffset, &ReflectionProbeComponent::boxOffset)
        .addProperty("boxSize", &ReflectionProbeComponent::boxSize, &ReflectionProbeComponent::boxSize)
        .addProperty("blendDistance", &ReflectionProbeComponent::blendDistance, &ReflectionProbeComponent::blendDistance)
        .addProperty("intensity", &ReflectionProbeComponent::intensity, &ReflectionProbeComponent::intensity)
        .addProperty("priority", &ReflectionProbeComponent::priority, &ReflectionProbeComponent::priority)
        .addProperty("resolution", &ReflectionProbeComponent::resolution, &ReflectionProbeComponent::resolution)
        .addProperty("nearClip", &ReflectionProbeComponent::nearClip, &ReflectionProbeComponent::nearClip)
        .addProperty("farClip", &ReflectionProbeComponent::farClip, &ReflectionProbeComponent::farClip)
        .addProperty("updateInterval", &ReflectionProbeComponent::updateInterval, &ReflectionProbeComponent::updateInterval)
        .addProperty("includeSky", &ReflectionProbeComponent::includeSky, &ReflectionProbeComponent::includeSky)
        .addProperty("texture", &ReflectionProbeComponent::texture)
        .addProperty("needUpdate", &ReflectionProbeComponent::needUpdate, &ReflectionProbeComponent::needUpdate)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ImageComponent>("ImageComponent")
        .addProperty("patchMarginLeft", &ImageComponent::patchMarginLeft, &ImageComponent::patchMarginLeft)
        .addProperty("patchMarginRight", &ImageComponent::patchMarginRight, &ImageComponent::patchMarginRight)
        .addProperty("patchMarginTop", &ImageComponent::patchMarginTop, &ImageComponent::patchMarginTop)
        .addProperty("patchMarginBottom", &ImageComponent::patchMarginBottom, &ImageComponent::patchMarginBottom)
        .addProperty("textureScaleFactor", &ImageComponent::textureScaleFactor, &ImageComponent::textureScaleFactor)
        .addProperty("needUpdatePatches", &ImageComponent::needUpdatePatches, &ImageComponent::needUpdatePatches)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TextComponent>("TextComponent")
        .addProperty("loaded", &TextComponent::loaded)
        .addProperty("font", &TextComponent::font, &TextComponent::font)
        .addProperty("text", &TextComponent::text, &TextComponent::text)
        .addProperty("fontSize", &TextComponent::fontSize, &TextComponent::fontSize)
        .addProperty("multiline", &TextComponent::multiline, &TextComponent::multiline)
        .addProperty("maxTextSize", &TextComponent::maxTextSize, &TextComponent::maxTextSize)
        .addProperty("charPositions", &TextComponent::charPositions)
        .addProperty("fixedWidth", &TextComponent::fixedWidth, &TextComponent::fixedWidth)
        .addProperty("fixedHeight", &TextComponent::fixedHeight, &TextComponent::fixedHeight)
        .addProperty("pivotBaseline", &TextComponent::pivotBaseline, &TextComponent::pivotBaseline)
        .addProperty("pivotCentered", &TextComponent::pivotCentered, &TextComponent::pivotCentered)
        .addProperty("needReloadAtlas", &TextComponent::needReloadAtlas, &TextComponent::needReloadAtlas)
        .addProperty("needUpdateText", &TextComponent::needUpdateText, &TextComponent::needUpdateText)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<UIContainerComponent>("UIContainerComponent")
        .addProperty("type", &UIContainerComponent::type, &UIContainerComponent::type)
        .addProperty("useAllWrapSpace", &UIContainerComponent::useAllWrapSpace, &UIContainerComponent::useAllWrapSpace)
        .addProperty("wrapCellWidth", &UIContainerComponent::wrapCellWidth, &UIContainerComponent::wrapCellWidth)
        .addProperty("wrapCellHeight", &UIContainerComponent::wrapCellHeight, &UIContainerComponent::wrapCellHeight)
        .addProperty("numBoxes", &UIContainerComponent::numBoxes)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<PositionActionComponent>("PositionActionComponent")
        .addProperty("endPosition", &PositionActionComponent::endPosition, &PositionActionComponent::endPosition)
        .addProperty("startPosition", &PositionActionComponent::startPosition, &PositionActionComponent::startPosition)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<RotationActionComponent>("RotationActionComponent")
        .addProperty("endRotation", &RotationActionComponent::endRotation, &RotationActionComponent::endRotation)
        .addProperty("startRotation", &RotationActionComponent::startRotation, &RotationActionComponent::startRotation)
        .addProperty("shortestPath", &RotationActionComponent::shortestPath, &RotationActionComponent::shortestPath)
        .addProperty("spin", &RotationActionComponent::spin, &RotationActionComponent::spin)
        .addProperty("axis", &RotationActionComponent::axis, &RotationActionComponent::axis)
        .addProperty("angle", &RotationActionComponent::angle, &RotationActionComponent::angle)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ScaleActionComponent>("ScaleActionComponent")
        .addProperty("endScale", &ScaleActionComponent::endScale, &ScaleActionComponent::endScale)
        .addProperty("startScale", &ScaleActionComponent::startScale, &ScaleActionComponent::startScale)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ColorActionComponent>("ColorActionComponent")
        .addProperty("endColor", &ColorActionComponent::endColor, &ColorActionComponent::endColor)
        .addProperty("startColor", &ColorActionComponent::startColor, &ColorActionComponent::startColor)
        .addProperty("useSRGB", &ColorActionComponent::useSRGB, &ColorActionComponent::useSRGB)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<AlphaActionComponent>("AlphaActionComponent")
        .addProperty("endAlpha", &AlphaActionComponent::endAlpha, &AlphaActionComponent::endAlpha)
        .addProperty("startAlpha", &AlphaActionComponent::startAlpha, &AlphaActionComponent::startAlpha)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<SpriteAnimationComponent>("SpriteAnimationComponent")
        .addProperty("loop", &SpriteAnimationComponent::loop, &SpriteAnimationComponent::loop)
        .addProperty("framesSize", &SpriteAnimationComponent::framesSize)
        .addProperty("framesTimeSize", &SpriteAnimationComponent::framesTimeSize)
        .addProperty("frameIndex", &SpriteAnimationComponent::frameIndex)
        .addProperty("frameTimeIndex", &SpriteAnimationComponent::frameTimeIndex)
        .addProperty("spriteFrameCount", &SpriteAnimationComponent::spriteFrameCount)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<KeyframeTracksComponent>("KeyframeTracksComponent")
        .addProperty("times", &KeyframeTracksComponent::times, &KeyframeTracksComponent::times)
        .addProperty("easings", &KeyframeTracksComponent::easings, &KeyframeTracksComponent::easings)
        .addProperty("loop", &KeyframeTracksComponent::loop, &KeyframeTracksComponent::loop)
        .addProperty("relative", &KeyframeTracksComponent::relative, &KeyframeTracksComponent::relative)
        .addProperty("index", &KeyframeTracksComponent::index)
        .addProperty("interpolation", &KeyframeTracksComponent::interpolation)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<TranslateTracksComponent>("TranslateTracksComponent")
        .addProperty("values", &TranslateTracksComponent::values, &TranslateTracksComponent::values)
        .addProperty("inTangents", &TranslateTracksComponent::inTangents, &TranslateTracksComponent::inTangents)
        .addProperty("outTangents", &TranslateTracksComponent::outTangents, &TranslateTracksComponent::outTangents)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<RotateTracksComponent>("RotateTracksComponent")
        .addProperty("values", &RotateTracksComponent::values, &RotateTracksComponent::values)
        .addProperty("inTangents", &RotateTracksComponent::inTangents, &RotateTracksComponent::inTangents)
        .addProperty("outTangents", &RotateTracksComponent::outTangents, &RotateTracksComponent::outTangents)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<ScaleTracksComponent>("ScaleTracksComponent")
        .addProperty("values", &ScaleTracksComponent::values, &ScaleTracksComponent::values)
        .addProperty("inTangents", &ScaleTracksComponent::inTangents, &ScaleTracksComponent::inTangents)
        .addProperty("outTangents", &ScaleTracksComponent::outTangents, &ScaleTracksComponent::outTangents)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<MorphTracksComponent>("MorphTracksComponent")
        .addProperty("values", &MorphTracksComponent::values, &MorphTracksComponent::values)
        .addProperty("inTangents", &MorphTracksComponent::inTangents, &MorphTracksComponent::inTangents)
        .addProperty("outTangents", &MorphTracksComponent::outTangents, &MorphTracksComponent::outTangents)
        .endClass();

#ifdef DORIAX_PHYSICS_2D
    luabridge::getGlobalNamespace(L)
        .beginClass<Body2DComponent>("Body2DComponent")
        .addProperty("numShapes", &Body2DComponent::numShapes)
        .addProperty("type", &Body2DComponent::type, &Body2DComponent::type)
        .addProperty("needReloadBody", &Body2DComponent::needReloadBody, &Body2DComponent::needReloadBody)
        .addProperty("needUpdateShapes", &Body2DComponent::needUpdateShapes, &Body2DComponent::needUpdateShapes)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Joint2DComponent>("Joint2DComponent")
        .addProperty("type", &Joint2DComponent::type, &Joint2DComponent::type)
        .addProperty("bodyA", &Joint2DComponent::bodyA, &Joint2DComponent::bodyA)
        .addProperty("bodyB", &Joint2DComponent::bodyB, &Joint2DComponent::bodyB)
        .addProperty("anchorA", &Joint2DComponent::anchorA, &Joint2DComponent::anchorA)
        .addProperty("anchorB", &Joint2DComponent::anchorB, &Joint2DComponent::anchorB)
        .addProperty("axis", &Joint2DComponent::axis, &Joint2DComponent::axis)
        .addProperty("target", &Joint2DComponent::target, &Joint2DComponent::target)
        .addProperty("autoAnchors", &Joint2DComponent::autoAnchors, &Joint2DComponent::autoAnchors)
        .addProperty("rope", &Joint2DComponent::rope, &Joint2DComponent::rope)
        .addProperty("needUpdateJoint", &Joint2DComponent::needUpdateJoint, &Joint2DComponent::needUpdateJoint)
        .endClass();
#endif

#ifdef DORIAX_PHYSICS_3D
    luabridge::getGlobalNamespace(L)
        .beginClass<Body3DComponent>("Body3DComponent")
        .addProperty("numShapes", &Body3DComponent::numShapes)
        .addProperty("overrideMassProperties", &Body3DComponent::overrideMassProperties, &Body3DComponent::overrideMassProperties)
        .addProperty("solidBoxSize", &Body3DComponent::solidBoxSize, &Body3DComponent::solidBoxSize)
        .addProperty("solidBoxDensity", &Body3DComponent::solidBoxDensity, &Body3DComponent::solidBoxDensity)
        .addProperty("type", &Body3DComponent::type, &Body3DComponent::type)
        .addProperty("motionQuality", &Body3DComponent::motionQuality, &Body3DComponent::motionQuality)
        .addProperty("gravityFactor", &Body3DComponent::gravityFactor, &Body3DComponent::gravityFactor)
        .addProperty("buoyancy", &Body3DComponent::buoyancy, &Body3DComponent::buoyancy)
        .addProperty("waterDrag", &Body3DComponent::waterDrag, &Body3DComponent::waterDrag)
        .addProperty("waterAngularDrag", &Body3DComponent::waterAngularDrag, &Body3DComponent::waterAngularDrag)
        .addProperty("allowedDOFs", &Body3DComponent::allowedDOFs, &Body3DComponent::allowedDOFs)
        .addProperty("sensor", &Body3DComponent::sensor, &Body3DComponent::sensor)
        .addProperty("needReloadBody", &Body3DComponent::needReloadBody, &Body3DComponent::needReloadBody)
        .addProperty("needUpdateShapes", &Body3DComponent::needUpdateShapes, &Body3DComponent::needUpdateShapes)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Joint3DComponent>("Joint3DComponent")
        .addProperty("type", &Joint3DComponent::type, &Joint3DComponent::type)
        .addProperty("bodyA", &Joint3DComponent::bodyA, &Joint3DComponent::bodyA)
        .addProperty("bodyB", &Joint3DComponent::bodyB, &Joint3DComponent::bodyB)
        .addProperty("anchorA", &Joint3DComponent::anchorA, &Joint3DComponent::anchorA)
        .addProperty("anchorB", &Joint3DComponent::anchorB, &Joint3DComponent::anchorB)
        .addProperty("anchor", &Joint3DComponent::anchor, &Joint3DComponent::anchor)
        .addProperty("axis", &Joint3DComponent::axis, &Joint3DComponent::axis)
        .addProperty("normal", &Joint3DComponent::normal, &Joint3DComponent::normal)
        .addProperty("twistAxis", &Joint3DComponent::twistAxis, &Joint3DComponent::twistAxis)
        .addProperty("planeAxis", &Joint3DComponent::planeAxis, &Joint3DComponent::planeAxis)
        .addProperty("axisX", &Joint3DComponent::axisX, &Joint3DComponent::axisX)
        .addProperty("axisY", &Joint3DComponent::axisY, &Joint3DComponent::axisY)
        .addProperty("limitsMin", &Joint3DComponent::limitsMin, &Joint3DComponent::limitsMin)
        .addProperty("limitsMax", &Joint3DComponent::limitsMax, &Joint3DComponent::limitsMax)
        .addProperty("normalHalfConeAngle", &Joint3DComponent::normalHalfConeAngle, &Joint3DComponent::normalHalfConeAngle)
        .addProperty("planeHalfConeAngle", &Joint3DComponent::planeHalfConeAngle, &Joint3DComponent::planeHalfConeAngle)
        .addProperty("twistMinAngle", &Joint3DComponent::twistMinAngle, &Joint3DComponent::twistMinAngle)
        .addProperty("twistMaxAngle", &Joint3DComponent::twistMaxAngle, &Joint3DComponent::twistMaxAngle)
        .addProperty("fixedPointA", &Joint3DComponent::fixedPointA, &Joint3DComponent::fixedPointA)
        .addProperty("fixedPointB", &Joint3DComponent::fixedPointB, &Joint3DComponent::fixedPointB)
        .addProperty("hingeA", &Joint3DComponent::hingeA, &Joint3DComponent::hingeA)
        .addProperty("hingeB", &Joint3DComponent::hingeB, &Joint3DComponent::hingeB)
        .addProperty("hinge", &Joint3DComponent::hinge, &Joint3DComponent::hinge)
        .addProperty("slider", &Joint3DComponent::slider, &Joint3DComponent::slider)
        .addProperty("numTeethGearA", &Joint3DComponent::numTeethGearA, &Joint3DComponent::numTeethGearA)
        .addProperty("numTeethGearB", &Joint3DComponent::numTeethGearB, &Joint3DComponent::numTeethGearB)
        .addProperty("numTeethRack", &Joint3DComponent::numTeethRack, &Joint3DComponent::numTeethRack)
        .addProperty("numTeethGear", &Joint3DComponent::numTeethGear, &Joint3DComponent::numTeethGear)
        .addProperty("rackLength", &Joint3DComponent::rackLength, &Joint3DComponent::rackLength)
        .addProperty("pathPoints", &Joint3DComponent::pathPoints, &Joint3DComponent::pathPoints)
        .addProperty("pathPosition", &Joint3DComponent::pathPosition, &Joint3DComponent::pathPosition)
        .addProperty("isLooping", &Joint3DComponent::isLooping, &Joint3DComponent::isLooping)
        .addProperty("autoAnchors", &Joint3DComponent::autoAnchors, &Joint3DComponent::autoAnchors)
        .addProperty("needUpdateJoint", &Joint3DComponent::needUpdateJoint, &Joint3DComponent::needUpdateJoint)
        .endClass();
#endif

#endif //DISABLE_LUA_BINDINGS
}
