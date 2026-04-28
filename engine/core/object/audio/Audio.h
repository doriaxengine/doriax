//
// (c) 2026 Eduardo Doria.
//

#ifndef Audio_h
#define Audio_h

#include "EntityHandle.h"
#include "Object.h"
#include "component/AudioComponent.h"

namespace doriax {

    class DORIAX_API Audio: public EntityHandle{
    public:
        Audio(Scene* scene);
        Audio(Scene* scene, bool is3D);
        Audio(Scene* scene, Entity entity);
        virtual ~Audio();

        int loadAudio(const std::string& filename);
        void destroyAudio();

        Object getObject() const;

        void play();
        void pause();
        void stop();
        void seek(double time);

        double getLength();
        double getPlayingTime();

        bool isPlaying();
        bool isPaused();
        bool isStopped();

        void setSound3D(bool sound3D);
        bool isSound3D() const;

        void setClockedSound(bool enableClocked);
        bool isClockedSound() const;

        void setVolume(double volume);
        double getVolume() const;

        void setSpeed(float speed);
        float getSpeed() const;

        void setPan(float pan);
        float getPan() const;

        void setLopping(bool lopping);
        bool isLopping() const;

        void setLoopingPoint(double loopingPoint);
        double getLoopingPoint() const;

        void setProtectVoice(bool protectVoice);
        bool isProtectVoice() const;

        void setInaudibleBehavior(bool mustTick, bool kill);

        void setMinMaxDistance(float minDistance, float maxDistance);

        void setMinDistance(float minDistance);
        float getMinDistance() const;

        void setMaxDistance(float maxDistance);
        float getMaxDistance() const;

        void setAttenuationModel(AudioAttenuation attenuationModel);
        AudioAttenuation getAttenuationModel() const;

        void setAttenuationRolloffFactor(float attenuationRolloffFactor);
        float getAttenuationRolloffFactor() const;

        void setDopplerFactor(float dopplerFactor);
        float getDopplerFactor() const;
    };

}

#endif /* Audio_h */
