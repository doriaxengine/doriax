// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef AUDIOSYSTEM_H
#define AUDIOSYSTEM_H

#include "SubSystem.h"

#include "component/SoundComponent.h"

#include <vector>

namespace SoLoud{
	class Soloud;
}

namespace doriax{

	class DORIAX_API AudioSystem : public SubSystem {

		friend class Engine;

    private:
		static bool inited;

		static void deInit();

		static float globalVolume;
		static bool muted;

		// voices to resume, the game's own pauses are kept
		static std::vector<unsigned int> systemPausedVoices;

		// Called by Engine
		static void systemPause();
		static void systemResume();

		Vector3 cameraLastPosition;
		bool outputRequested = false;

        bool loadSoundSample(SoundComponent& audio);
        void preloadSoundAssets();

	public:
		AudioSystem(Scene* scene);

		static SoLoud::Soloud& getSoloud();
		static bool init();

		static void stopAll();
		static void pauseAll();
		static void resumeAll();
		static void checkActive();

		static void setGlobalVolume(float volume);
		static float getGlobalVolume();

		// Platform mute, like YouTube's, that the global volume can't override
		static void setMuted(bool muted);

        bool loadSound(SoundComponent& audio);
		void destroySound(SoundComponent& audio);
		void getLoadCount(size_t& loaded, size_t& total) const;
		void stopSceneSounds();
		bool seekSound(SoundComponent& audio, double time);

		void setPaused(bool paused) override;

		void load() override;
		void draw() override;
		void destroy() override;
		void update(double dt) override;

		void onComponentAdded(Entity entity, ComponentId componentId) override;
		void onComponentRemoved(Entity entity, ComponentId componentId) override;
	};

}

#endif //AUDIOSYSTEM_H