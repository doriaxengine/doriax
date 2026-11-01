// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

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
#include "action/keyframe/KeyframeTracks.h"
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
        .addVariable("STEP", EaseType::STEP)
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
        .addVariable("CUSTOM", EaseType::CUSTOM)
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
        .addFunction("restart", &Action::restart)
        .addProperty("ownedTarget", &Action::getOwnedTarget, &Action::setOwnedTarget)
        .addProperty("target", (Entity(Action::*)()const)&Action::getTarget, (void(Action::*)(Entity))&Action::setTarget)
        .addFunction("setTarget", 
            luabridge::overload<Object*>(&Action::setTarget),
            luabridge::overload<Entity>(&Action::setTarget))
        .addProperty("speed", &Action::getSpeed, &Action::setSpeed)
        .addProperty("weight", &Action::getWeight, &Action::setWeight)
        .addProperty("startOffset", &Action::getStartOffset, &Action::setStartOffset)
        .addProperty("randomStart", &Action::isRandomStart, &Action::setRandomStart)
        .addFunction("isRunning", &Action::isRunning)
        .addFunction("isStopped", &Action::isStopped)
        .addFunction("isPaused", &Action::isPaused)
        .addFunction("getTimeCount", &Action::getTimeCount)
        .addFunction("getActionComponent", &Action::getComponent<ActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<TimedAction, Action>("TimedAction")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setFunctionType", &TimedAction::setFunctionType)
        .addFunction("getTimedActionComponent", &TimedAction::getComponent<TimedActionComponent>)
        .addFunction("getTime", &TimedAction::getTime)
        .addFunction("getValue", &TimedAction::getValue)
        .addProperty("duration", &TimedAction::getDuration, &TimedAction::setDuration)
        .addProperty("loop", &TimedAction::isLoop, &TimedAction::setLoop)
        .addProperty("pingPong", &TimedAction::isPingPong, &TimedAction::setPingPong)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<AlphaAction, TimedAction>("AlphaAction")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setAction", &AlphaAction::setAction)
        .addFunction("getAlphaActionComponent", &AlphaAction::getComponent<AlphaActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ColorAction, TimedAction>("ColorAction")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setAction", 
            luabridge::overload<Vector3, Vector3, float, bool>(&ColorAction::setAction),
            luabridge::overload<Vector4, Vector4, float, bool>(&ColorAction::setAction))
        .addFunction("getColorActionComponent", &ColorAction::getComponent<ColorActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<PositionAction, TimedAction>("PositionAction")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setAction", &PositionAction::setAction)
        .addFunction("getPositionActionComponent", &PositionAction::getComponent<PositionActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<RotationAction, TimedAction>("RotationAction")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setAction", &RotationAction::setAction)
        .addFunction("setSpinAction", &RotationAction::setSpinAction)
        .addFunction("getRotationActionComponent", &RotationAction::getComponent<RotationActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ScaleAction, TimedAction>("ScaleAction")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setAction", &ScaleAction::setAction)
        .addFunction("getScaleActionComponent", &ScaleAction::getComponent<ScaleActionComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Particles, Action>("Particles")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("reset", &Particles::reset)
        .addProperty("rate", &Particles::getRate, &Particles::setRate)
        .addProperty("maxPerUpdate", &Particles::getMaxPerUpdate, &Particles::setMaxPerUpdate)
        .addProperty("emitter", &Particles::isEmitter, &Particles::setEmitter)
        .addProperty("loop", &Particles::isLoop, &Particles::setLoop)
        .addProperty("localSpace", &Particles::isLocalSpace, &Particles::setLocalSpace)
        .addProperty("emitterShape", &Particles::getPositionInitializerShape, &Particles::setPositionInitializerShape)
        .addFunction("setLifeInitializer", 
            luabridge::overload<float>(&Particles::setLifeInitializer),
            luabridge::overload<float, float>(&Particles::setLifeInitializer))
        .addFunction("setPositionInitializer", 
            luabridge::overload<Vector3>(&Particles::setPositionInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setPositionInitializer))
        .addFunction("setSpherePositionInitializer",
            luabridge::overload<float>(&Particles::setSpherePositionInitializer),
            luabridge::overload<float, float>(&Particles::setSpherePositionInitializer))
        .addFunction("setHemispherePositionInitializer",
            luabridge::overload<float>(&Particles::setHemispherePositionInitializer),
            luabridge::overload<float, float>(&Particles::setHemispherePositionInitializer))
        .addFunction("setCirclePositionInitializer",
            luabridge::overload<float>(&Particles::setCirclePositionInitializer),
            luabridge::overload<float, float>(&Particles::setCirclePositionInitializer))
        .addFunction("setConePositionInitializer", &Particles::setConePositionInitializer)
        .addFunction("addBurst",
            luabridge::overload<float, int>(&Particles::addBurst),
            luabridge::overload<float, int, int>(&Particles::addBurst))
        .addFunction("setBursts", &Particles::setBursts)
        .addFunction("clearBursts", &Particles::clearBursts)
        .addFunction("setPositionModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setPositionModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setPositionModifier),
            luabridge::overload<float, float, Vector3, Vector3, Ease>(&Particles::setPositionModifier))
        .addFunction("setVelocityInitializer", 
            luabridge::overload<Vector3>(&Particles::setVelocityInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setVelocityInitializer))
        .addFunction("setVelocityModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setVelocityModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setVelocityModifier),
            luabridge::overload<float, float, Vector3, Vector3, Ease>(&Particles::setVelocityModifier))
        .addFunction("setAccelerationInitializer", 
            luabridge::overload<Vector3>(&Particles::setAccelerationInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setAccelerationInitializer))
        .addFunction("setAccelerationModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setAccelerationModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setAccelerationModifier),
            luabridge::overload<float, float, Vector3, Vector3, Ease>(&Particles::setAccelerationModifier))
        .addFunction("setColorInitializer", 
            luabridge::overload<Vector3>(&Particles::setColorInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setColorInitializer))
        .addFunction("setColorModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setColorModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setColorModifier),
            luabridge::overload<float, float, Vector3, Vector3, Ease>(&Particles::setColorModifier))
        .addFunction("addColorGradientStop", &Particles::addColorGradientStop)
        .addFunction("setColorGradient", &Particles::setColorGradient)
        .addFunction("clearColorGradient", &Particles::clearColorGradient)
        .addFunction("setColorGradientUseSRGB", &Particles::setColorGradientUseSRGB)
        .addFunction("setAlphaInitializer", 
            luabridge::overload<float>(&Particles::setAlphaInitializer),
            luabridge::overload<float, float>(&Particles::setAlphaInitializer))
        .addFunction("setAlphaModifier", 
            luabridge::overload<float, float, float, float>(&Particles::setAlphaModifier),
            luabridge::overload<float, float, float, float, EaseType>(&Particles::setAlphaModifier),
            luabridge::overload<float, float, float, float, Ease>(&Particles::setAlphaModifier))
        .addFunction("setSizeInitializer", 
            luabridge::overload<float>(&Particles::setSizeInitializer),
            luabridge::overload<float, float>(&Particles::setSizeInitializer))
        .addFunction("setSizeModifier", 
            luabridge::overload<float, float, float, float>(&Particles::setSizeModifier),
            luabridge::overload<float, float, float, float, EaseType>(&Particles::setSizeModifier),
            luabridge::overload<float, float, float, float, Ease>(&Particles::setSizeModifier))
        .addFunction("setSpriteIntializer", 
            luabridge::overload<std::vector<int>>(&Particles::setSpriteIntializer),
            luabridge::overload<int, int>(&Particles::setSpriteIntializer))
        .addFunction("setSpriteModifier", 
            luabridge::overload<float, float, std::vector<int>>(&Particles::setSpriteModifier),
            luabridge::overload<float, float, std::vector<int>, EaseType>(&Particles::setSpriteModifier),
            luabridge::overload<float, float, std::vector<int>, Ease>(&Particles::setSpriteModifier))
        .addFunction("setRotationInitializer", 
            luabridge::overload<float>(&Particles::setRotationInitializer),
            luabridge::overload<float, float>(&Particles::setRotationInitializer),
            luabridge::overload<Quaternion>(&Particles::setRotationInitializer),
            luabridge::overload<Quaternion, Quaternion>(&Particles::setRotationInitializer))
        .addFunction("setRotationModifier", 
            luabridge::overload<float, float, float, float>(&Particles::setRotationModifier),
            luabridge::overload<float, float, float, float, EaseType>(&Particles::setRotationModifier),
            luabridge::overload<float, float, float, float, Ease>(&Particles::setRotationModifier),
            luabridge::overload<float, float, Quaternion, Quaternion>(&Particles::setRotationModifier),
            luabridge::overload<float, float, Quaternion, Quaternion, EaseType>(&Particles::setRotationModifier),
            luabridge::overload<float, float, Quaternion, Quaternion, Ease>(&Particles::setRotationModifier))
        .addFunction("setScaleInitializer", 
            luabridge::overload<float>(&Particles::setScaleInitializer),
            luabridge::overload<Vector3>(&Particles::setScaleInitializer),
            luabridge::overload<float, float>(&Particles::setScaleInitializer),
            luabridge::overload<Vector3, Vector3>(&Particles::setScaleInitializer))
        .addFunction("setScaleModifier", 
            luabridge::overload<float, float, Vector3, Vector3>(&Particles::setScaleModifier),
            luabridge::overload<float, float, float, float>(&Particles::setScaleModifier),
            luabridge::overload<float, float, Vector3, Vector3, EaseType>(&Particles::setScaleModifier),
            luabridge::overload<float, float, Vector3, Vector3, Ease>(&Particles::setScaleModifier))
        .addFunction("getParticlesComponent", &Particles::getComponent<ParticlesComponent>)
        // old misspelled name
        .addFunction("getParticlesomponent", &Particles::getComponent<ParticlesComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<SpriteAnimation, Action>("SpriteAnimation")
        .addConstructor <void (*) (Scene*), void (*) (Scene*, Entity)> ()
        .addFunction("setAnimation", 
            luabridge::overload<std::vector<int>, std::vector<int>, bool>(&SpriteAnimation::setAnimation),
            luabridge::overload<int, int, int, bool>(&SpriteAnimation::setAnimation))
        .addFunction("getSpriteAnimationComponent", &SpriteAnimation::getComponent<SpriteAnimationComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Animation, Action>("Animation")
        .addConstructor <void(Scene*), void(Scene*, Entity)> ()
        .addProperty("loop", &Animation::isLoop, &Animation::setLoop)
        .addProperty("ownedActions", &Animation::isOwnedActions, &Animation::setOwnedActions)
        .addProperty("blendWeight", &Animation::getBlendWeight, &Animation::setBlendWeight)
        .addProperty("defaultFadeTime", &Animation::getDefaultFadeTime, &Animation::setDefaultFadeTime)
        .addFunction("fadeIn", &Animation::fadeIn)
        .addFunction("fadeOut", &Animation::fadeOut)
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
        .addFunction("getAnimationComponent", &Animation::getComponent<AnimationComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<Ease, FunctionSubscribe<float(float)>>("Ease")
        .addConstructor <void (*) (void), void (*) (EaseType)> ()
        .addFunction("__call", &Ease::call)
        .addFunction("call", &Ease::call)
        .addFunction("add", (bool (Ease::*)(const std::string&, lua_State*))&Ease::add)
        .addFunction("setFunction", (void (Ease::*)(lua_State*))&Ease::setFunction)
        .addProperty("type", &Ease::getType, &Ease::setType)
        .addFunction("getType", &Ease::getType)
        .addFunction("setType", &Ease::setType)
        .addFunction("getFunctionType", &Ease::getFunctionType)
        .addFunction("setFunctionType", &Ease::setFunctionType)
        .addStaticFunction("linear", &Ease::linear)
        .addStaticFunction("easeInQuad", &Ease::easeInQuad)
        .addStaticFunction("easeOutQuad", &Ease::easeOutQuad)
        .addStaticFunction("easeInOutQuad", &Ease::easeInOutQuad)
        .addStaticFunction("easeInCubic", &Ease::easeInCubic)
        .addStaticFunction("easeOutCubic", &Ease::easeOutCubic)
        .addStaticFunction("easeInOutCubic", &Ease::easeInOutCubic)
        .addStaticFunction("easeInQuart", &Ease::easeInQuart)
        .addStaticFunction("easeOutQuart", &Ease::easeOutQuart)
        .addStaticFunction("easeInQuint", &Ease::easeInQuint)
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
        .deriveClass<KeyframeTracks, Action>("KeyframeTracks")
        .addConstructor <void(Scene*), void(Scene*, Entity)> ()
        .addFunction("setTimes", &KeyframeTracks::setTimes)
        .addFunction("setEasings", &KeyframeTracks::setEasings)
        .addFunction("setEasing", &KeyframeTracks::setEasing)
        .addProperty("loop", &KeyframeTracks::isLoop, &KeyframeTracks::setLoop)
        .addProperty("relative", &KeyframeTracks::isRelative, &KeyframeTracks::setRelative)
        .addFunction("getKeyframeTracksComponent", &KeyframeTracks::getComponent<KeyframeTracksComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<MorphTracks, KeyframeTracks>("MorphTracks")
        .addConstructor <void(Scene*), void(Scene*, Entity), void(Scene*, std::vector<float>, std::vector<std::vector<float>>)> ()
        .addFunction("setValues", &MorphTracks::setValues)
        .addFunction("getMorphTracksComponent", &MorphTracks::getComponent<MorphTracksComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<RotateTracks, KeyframeTracks>("RotateTracks")
        .addConstructor <void(Scene*), void(Scene*, Entity), void(Scene*, std::vector<float>, std::vector<Quaternion>)> ()
        .addFunction("setValues", &RotateTracks::setValues)
        .addFunction("getRotateTracksComponent", &RotateTracks::getComponent<RotateTracksComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<ScaleTracks, KeyframeTracks>("ScaleTracks")
        .addConstructor <void(Scene*), void(Scene*, Entity), void(Scene*, std::vector<float>, std::vector<Vector3>)> ()
        .addFunction("setValues", &ScaleTracks::setValues)
        .addFunction("getScaleTracksComponent", &ScaleTracks::getComponent<ScaleTracksComponent>)
        .endClass();

    luabridge::getGlobalNamespace(L)
        .deriveClass<TranslateTracks, KeyframeTracks>("TranslateTracks")
        .addConstructor <void(Scene*), void(Scene*, Entity), void(Scene*, std::vector<float>, std::vector<Vector3>)> ()
        .addFunction("setValues", &TranslateTracks::setValues)
        .addFunction("getTranslateTracksComponent", &TranslateTracks::getComponent<TranslateTracksComponent>)
        .endClass();

#endif //DISABLE_LUA_BINDINGS
}
