//
// (c) 2026 Eduardo Doria.
//

#include "LuaBinding.h"

#include "lua.hpp"

#include "LuaBridge.h"
#include "LuaBridgeAddon.h"

#include "action/Action.h"
#include "action/TimedAction.h"
#include "action/AlphaAction.h"
#include "action/ColorAction.h"
#include "action/PositionAction.h"
#include "action/RotationAction.h"
#include "action/ScaleAction.h"
#include "action/Animation.h"
#include "action/Particles.h"
#include "action/SpriteAnimation.h"
#include "action/Ease.h"
#include "action/keyframe/MorphTracks.h"
#include "action/keyframe/RotateTracks.h"
#include "action/keyframe/ScaleTracks.h"
#include "action/keyframe/TranslateTracks.h"

using namespace doriax;

void LuaBinding::registerActionClasses(lua_State *L){
#ifndef DISABLE_LUA_BINDINGS

    luabridge::getGlobalNamespace(L)
        .beginNamespace("EaseType")
        .addVariable("LINEAR", EaseType::LINEAR)
        .addVariable("QUAD_IN", EaseType::QUAD_IN)
        .addVariable("QUAD_OUT", EaseType::QUAD_OUT)
        .addVariable("QUAD_IN_OUT", EaseType::QUAD_IN_OUT)
        .addVariable("CUBIC_IN", EaseType::CUBIC_IN)
        .addVariable("CUBIC_OUT", EaseType::CUBIC_OUT)
        .addVariable("CUBIC_IN_OUT", EaseType::CUBIC_IN_OUT)
        .addVariable("QUART_IN", EaseType::QUART_IN)
        .addVariable("QUART_OUT", EaseType::QUART_OUT)
        .addVariable("QUART_IN_OUT", EaseType::QUART_IN_OUT)
        .addVariable("QUINT_IN", EaseType::QUINT_IN)
        .addVariable("QUINT_OUT", EaseType::QUINT_OUT)
        .addVariable("QUINT_IN_OUT", EaseType::QUINT_IN_OUT)
        .addVariable("SINE_IN", EaseType::SINE_IN)
        .addVariable("SINE_OUT", EaseType::SINE_OUT)
        .addVariable("SINE_IN_OUT", EaseType::SINE_IN_OUT)
        .addVariable("EXPO_IN", EaseType::EXPO_IN)
        .addVariable("EXPO_OUT", EaseType::EXPO_OUT)
        .addVariable("EXPO_IN_OUT", EaseType::EXPO_IN_OUT)
        .addVariable("CIRC_IN", EaseType::CIRC_IN)
        .addVariable("CIRC_OUT", EaseType::CIRC_OUT)
        .addVariable("CIRC_IN_OUT", EaseType::CIRC_IN_OUT)
        .addVariable("ELASTIC_IN", EaseType::ELASTIC_IN)
        .addVariable("ELASTIC_OUT", EaseType::ELASTIC_OUT)
        .addVariable("ELASTIC_IN_OUT", EaseType::ELASTIC_IN_OUT)
        .addVariable("BACK_IN", EaseType::BACK_IN)
        .addVariable("BACK_OUT", EaseType::BACK_OUT)
        .addVariable("BACK_IN_OUT", EaseType::BACK_IN_OUT)
        .addVariable("BOUNCE_IN", EaseType::BOUNCE_IN)
        .addVariable("BOUNCE_OUT", EaseType::BOUNCE_OUT)
        .addVariable("BOUNCE_IN_OUT", EaseType::BOUNCE_IN_OUT)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .beginNamespace("ActionState")
        .addVariable("Running", ActionState::Running)
        .addVariable("Paused", ActionState::Paused)
        .addVariable("Stopped", ActionState::Stopped)
        .endNamespace();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Action, EntityHandle>("Action")
        .addConstructor <void(Scene*), void(Scene*, Entity)> ()
        .addFunction("start", &Action::start)
        .addFunction("pause", &Action::pause)
        .addFunction("stop", &Action::stop)
        .addProperty("ownedTarget", &Action::getOwnedTarget, &Action::setOwnedTarget)
        .addProperty("target", (Entity(Action::*)()const)&Action::getTarget, (void(Action::*)(Entity))&Action::setTarget)
        .addFunction("setTarget", 
            luabridge::overload<Object*>(&Action::setTarget),
            luabridge::overload<Entity>(&Action::setTarget))
        .addProperty("speed", &Action::getSpeed, &Action::setSpeed)
        .addFunction("isRunning", &Action::isRunning)
        .addFunction("isStopped", &Action::isStopped)
        .addFunction("isPaused", &Action::isPaused)
        .addFunction("getTimeCount", &Action::getTimeCount)
        .addFunction("getActionComponent", &Action::getComponent<ActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<TimedAction, Action>("TimedAction")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setFunctionType", &TimedAction::setFunctionType)
        .addFunction("getTimedActionComponent", &TimedAction::getComponent<TimedActionComponent>)
        .addFunction("getTime", &TimedAction::getTime)
        .addFunction("getValue", &TimedAction::getValue)
        .addProperty("duration", &TimedAction::getDuration, &TimedAction::setDuration)
        .addProperty("loop", &TimedAction::isLoop, &TimedAction::setLoop)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<AlphaAction, TimedAction>("AlphaAction")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setAction", &AlphaAction::setAction)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ColorAction, TimedAction>("ColorAction")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setAction", 
            luabridge::overload<Vector3, Vector3, float, bool>(&ColorAction::setAction),
            luabridge::overload<Vector4, Vector4, float, bool>(&ColorAction::setAction))
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<PositionAction, TimedAction>("PositionAction")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setAction", &PositionAction::setAction)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<RotationAction, TimedAction>("RotationAction")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setAction", &RotationAction::setAction)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ScaleAction, TimedAction>("ScaleAction")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setAction", &ScaleAction::setAction)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Particles, Action>("Particles")
        .addConstructor <void (*) (Scene*)> ()
        .addProperty("rate", &Particles::getRate, &Particles::setRate)
        .addProperty("maxPerUpdate", &Particles::getMaxPerUpdate, &Particles::setMaxPerUpdate)
        .addProperty("emitter", &Particles::isEmitter, &Particles::setEmitter)
        .addProperty("loop", &Particles::isLoop, &Particles::setLoop)
        .addFunction("setLifeInitializer", 
            luabridge::overload<float>(&Particles::setLifeInitializer),
            luabridge::overload<float, float>(&Particles::setLifeInitializer))
        .addFunction("setPositionInitializer", 
            luabridge::overload<Vector3>(&Particles::setPositionInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setPositionInitializer))
        .addFunction("setPositionModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setPositionModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setPositionModifier))
        .addFunction("setVelocityInitializer", 
            luabridge::overload<Vector3>(&Particles::setVelocityInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setVelocityInitializer))
        .addFunction("setVelocityModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setVelocityModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setVelocityModifier))
        .addFunction("setAccelerationInitializer", 
            luabridge::overload<Vector3>(&Particles::setAccelerationInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setAccelerationInitializer))
        .addFunction("setAccelerationModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setAccelerationModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setAccelerationModifier))
        .addFunction("setColorInitializer", 
            luabridge::overload<Vector3>(&Particles::setColorInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setColorInitializer))
        .addFunction("setColorModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setColorModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setColorModifier))
        .addFunction("setAlphaInitializer", 
            luabridge::overload<float>(&Particles::setAlphaInitializer),
            luabridge::overload<float, float>(&Particles::setAlphaInitializer))
        .addFunction("setAlphaModifier", 
            luabridge::overload<float, float, float, float>(&Particles::setAlphaModifier),
            luabridge::overload<float, float, float, float, EaseType>(&Particles::setAlphaModifier))
        .addFunction("setSizeInitializer", 
            luabridge::overload<float>(&Particles::setSizeInitializer),
            luabridge::overload<float, float>(&Particles::setSizeInitializer))
        .addFunction("setSizeModifier", 
            luabridge::overload<float, float, float, float>(&Particles::setSizeModifier),
            luabridge::overload<float, float, float, float, EaseType>(&Particles::setSizeModifier))
        .addFunction("setSpriteIntializer", 
            luabridge::overload<std::vector<int>>(&Particles::setSpriteIntializer),
            luabridge::overload<int, int>(&Particles::setSpriteIntializer))
        .addFunction("setSpriteModifier", 
            luabridge::overload<float, float, std::vector<int>>(&Particles::setSpriteModifier),
            luabridge::overload<float, float, std::vector<int>, EaseType>(&Particles::setSpriteModifier))
        .addFunction("setRotationInitializer", 
            luabridge::overload<float>(&Particles::setRotationInitializer),
            luabridge::overload<float, float>(&Particles::setRotationInitializer),
            luabridge::overload<Quaternion>(&Particles::setRotationInitializer),
            luabridge::overload<Quaternion, Quaternion>(&Particles::setRotationInitializer))
        .addFunction("setRotationModifier", 
            luabridge::overload<float, float, float, float>(&Particles::setRotationModifier),
            luabridge::overload<float, float, float, float, EaseType>(&Particles::setRotationModifier),
            luabridge::overload<float, float, Quaternion, Quaternion>(&Particles::setRotationModifier),
            luabridge::overload<float, float, Quaternion, Quaternion, EaseType>(&Particles::setRotationModifier))
        .addFunction("setScaleInitializer", 
            luabridge::overload<float>(&Particles::setScaleInitializer),
            luabridge::overload<Vector3>(&Particles::setScaleInitializer),
            luabridge::overload<float, float>(&Particles::setScaleInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setScaleInitializer))
        .addFunction("setScaleModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setScaleModifier),
            luabridge::overload<float, float, float, float>(&Particles::setScaleModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setScaleModifier))
        .addFunction("getParticlesomponent", &Particles::getComponent<ParticlesComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<SpriteAnimation, Action>("SpriteAnimation")
        .addConstructor <void (*) (Scene*)> ()
        .addFunction("setAnimation", 
            luabridge::overload<std::vector<int>, std::vector<int>, bool>(&SpriteAnimation::setAnimation),
            luabridge::overload<int, int, int, bool>(&SpriteAnimation::setAnimation))
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Animation, Action>("Animation")
        .addConstructor <void(Scene*), void(Scene*, Entity)> ()
        .addProperty("loop", &Animation::isLoop, &Animation::setLoop)
        .addProperty("ownedActions", &Animation::isOwnedActions, &Animation::setOwnedActions)
        .addProperty("name", &Animation::getName, &Animation::setName)
        .addFunction("addActionFrame", 
            luabridge::overload<float, float, Entity, Entity>(&Animation::addActionFrame),
            luabridge::overload<float, Entity, Entity>(&Animation::addActionFrame),
            luabridge::overload<float, float, Entity>(&Animation::addActionFrame),
            luabridge::overload<float, Entity>(&Animation::addActionFrame))
        .addFunction("getActionFrameSize", &Animation::getActionFrameSize)
        .addFunction("getActionFrame", &Animation::getActionFrame)
        .addFunction("setActionFrameStartTime", &Animation::setActionFrameStartTime)
        .addFunction("setActionFrameDuration", &Animation::setActionFrameDuration)
        .addFunction("setActionFrameEntity", &Animation::setActionFrameEntity)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .beginClass<Ease>("Ease")
        .addStaticFunction("linear", &Ease::linear)
        .addStaticFunction("easeInQuad", &Ease::easeInQuad)
        .addStaticFunction("easeOutQuad", &Ease::easeOutQuad)
        .addStaticFunction("easeInOutQuad", &Ease::easeInOutQuad)
        .addStaticFunction("easeInCubic", &Ease::easeInCubic)
        .addStaticFunction("easeOutCubic", &Ease::easeOutCubic)
        .addStaticFunction("easeInOutCubic", &Ease::easeInOutCubic)
        .addStaticFunction("easeInQuart", &Ease::easeInQuart)
        .addStaticFunction("easeOutQuart", &Ease::easeOutQuart)
        .addStaticFunction("easeInQuint", &Ease::easeOutQuint)
        .addStaticFunction("easeOutQuint", &Ease::easeOutQuint)
        .addStaticFunction("easeInOutQuint", &Ease::easeInOutQuint)
        .addStaticFunction("easeInSine", &Ease::easeInSine)
        .addStaticFunction("easeOutSine", &Ease::easeOutSine)
        .addStaticFunction("easeInOutSine", &Ease::easeInOutSine)
        .addStaticFunction("easeInExpo", &Ease::easeInExpo)
        .addStaticFunction("easeOutExpo", &Ease::easeOutExpo)
        .addStaticFunction("easeInOutExpo", &Ease::easeInOutExpo)
        .addStaticFunction("easeInCirc", &Ease::easeInCirc)
        .addStaticFunction("easeOutCirc", &Ease::easeOutCirc)
        .addStaticFunction("easeInOutCirc", &Ease::easeInOutCirc)
        .addStaticFunction("easeInElastic", &Ease::easeInElastic)
        .addStaticFunction("easeOutElastic", &Ease::easeOutElastic)
        .addStaticFunction("easeInOutElastic", &Ease::easeInOutElastic)
        .addStaticFunction("easeInBack", &Ease::easeInBack)
        .addStaticFunction("easeOutBack", &Ease::easeOutBack)
        .addStaticFunction("easeInOutBack", &Ease::easeInOutBack)
        .addStaticFunction("easeOutBounce", &Ease::easeOutBounce)
        .addStaticFunction("easeInBounce", &Ease::easeInBounce)
        .addStaticFunction("easeInOutBounce", &Ease::easeInOutBounce)

        .addStaticFunction("getFunction", &Ease::getFunction)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<MorphTracks, Action>("MorphTracks")
        .addConstructor <void(Scene*), void(Scene*, std::vector<float>, std::vector<std::vector<float>>)> ()
        .addFunction("setTimes", &MorphTracks::setTimes)
        .addFunction("setValues", &MorphTracks::setValues)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<RotateTracks, Action>("RotateTracks")
        .addConstructor <void(Scene*), void(Scene*, std::vector<float>, std::vector<Quaternion>)> ()
        .addFunction("setTimes", &RotateTracks::setTimes)
        .addFunction("setValues", &RotateTracks::setValues)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ScaleTracks, Action>("ScaleTracks")
        .addConstructor <void(Scene*), void(Scene*, std::vector<float>, std::vector<Vector3>)> ()
        .addFunction("setTimes", &ScaleTracks::setTimes)
        .addFunction("setValues", &ScaleTracks::setValues)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<TranslateTracks, Action>("TranslateTracks")
        .addConstructor <void(Scene*), void(Scene*, std::vector<float>, std::vector<Vector3>)> ()
        .addFunction("setTimes", &TranslateTracks::setTimes)
        .addFunction("setValues", &TranslateTracks::setValues)
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}