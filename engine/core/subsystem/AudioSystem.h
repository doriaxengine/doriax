//
// (c) 2026 Eduardo Doria.
//

#ifndef AUDIOSYSTEM_H
#define AUDIOSYSTEM_H

#include "SubSystem.h"

#include "component/SoundComponent.h"

namespace SoLoud{
	class Soloud;
}

namespace doriax{

	class DORIAX_API AudioSystem : public SubSystem {

    private:
		static bool inited;

		static void deInit();

		static float globalVolume;

		Vector3 cameraLastPosition;

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

        bool loadSound(SoundComponent& audio, Entity entity);
		void destroySound(SoundComponent& audio);
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