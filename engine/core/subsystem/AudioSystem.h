//
// (c) 2026 Eduardo Doria.
//

#ifndef AUDIOSYSTEM_H
#define AUDIOSYSTEM_H

#include "SubSystem.h"

#include "component/AudioComponent.h"

namespace SoLoud{
	class Soloud;
}

namespace doriax{

	class DORIAX_API AudioSystem : public SubSystem {

    private:
        static SoLoud::Soloud& getSoloud();
		static bool inited;

		static void init();
		static void deInit();

		static float globalVolume;

		Vector3 cameraLastPosition;

	public:
		AudioSystem(Scene* scene);

		static void stopAll();
		static void pauseAll();
		static void resumeAll();
		static void checkActive();

		static void setGlobalVolume(float volume);
		static float getGlobalVolume();

        bool loadAudio(AudioComponent& audio, Entity entity);
		void destroyAudio(AudioComponent& audio);
		bool seekAudio(AudioComponent& audio, double time);

		void load() override;
		void draw() override;
		void destroy() override;
		void update(double dt) override;

		void onComponentAdded(Entity entity, ComponentId componentId) override;
		void onComponentRemoved(Entity entity, ComponentId componentId) override;
	};

}

#endif //AUDIOSYSTEM_H