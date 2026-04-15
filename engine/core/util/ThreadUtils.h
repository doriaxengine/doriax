// ---------------------------------------
// Based on: https://github.com/oviano/sokol-multithread
// ---------------------------------------

//
// (c) 2026 Eduardo Doria.
//

// not using 'semaphore.h' file name to avoid errors when building Android with Mac
#ifndef ThreadUtils_H
#define ThreadUtils_H

// ----------------------------------------------------------------------------------------------------

#include <mutex>
#include <condition_variable>

// ----------------------------------------------------------------------------------------------------

namespace doriax {

	class Semaphore {
	public:
		Semaphore() {}

		~Semaphore() {}

		void release() {
			std::scoped_lock<std::mutex> lock(m_mutex);
			m_count++;
			m_cv.notify_one();
		}

		void acquire() {
			std::unique_lock<std::mutex> lock(m_mutex);
			while (!m_count) {
				m_cv.wait(lock);
			}
			m_count--;
		}

	private:
		uint32_t m_count = 0;
		std::mutex m_mutex;
		std::condition_variable m_cv;
	};

}

#endif // ThreadUtils_H
