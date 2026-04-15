//
// (c) 2026 Eduardo Doria.
//

#ifndef AUDIO_COMPONENT_H
#define AUDIO_COMPONENT_H

#include "Engine.h"

namespace SoLoud{
    class Wav;
}

namespace doriax{

    enum class AudioState{
        Playing,
        Paused,
        Stopped
    };

    enum class AudioAttenuation{
        NO_ATTENUATION,
        INVERSE_DISTANCE,
        LINEAR_DISTANCE,
        EXPONENTIAL_DISTANCE
    };

    struct DORIAX_API AudioComponent{
        SoLoud::Wav* sample = nullptr;
        unsigned int handle; //Soloud handle

        AudioState state = AudioState::Stopped;

        std::string filename;
        bool loaded = false;

        bool enableClocked = false;
        bool enable3D = false;
        Vector3 lastPosition = Vector3(0, 0, 0);

        bool startTrigger = false;
        bool stopTrigger = false;
        bool pauseTrigger = false;

        FunctionSubscribe<void()> onStart;
        FunctionSubscribe<void()> onPause;
        FunctionSubscribe<void()> onStop;

        double volume = 1;
        float speed = 1;
        float pan = 0; // -1 is left, 0 is middle and 1 is right
        bool looping = false;
        double loopingPoint = 0;
        bool protectVoice = false;
        bool inaudibleBehaviorMustTick = false;
        bool inaudibleBehaviorKill = false;

        // for 3D sound
        float minDistance = 1.0;
        float maxDistance = 1000.0;
        AudioAttenuation attenuationModel = AudioAttenuation::NO_ATTENUATION;
        float attenuationRolloffFactor = 1.0; // means that end of max distance sound will be zero
        float dopplerFactor = 1.0;

        // setted by system
        double length;
        double playingTime;

        bool needUpdate = true;
    };

}

#endif //AUDIO_COMPONENT_H
