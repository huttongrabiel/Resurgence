#include "threading.h"

namespace util {

latch::latch(std::size_t count) : count(count) {}

void latch::wait() const {
	std::unique_lock lock(mutex);
	cv.wait(lock, [&]() { return count <= 0; });
}

void latch::count_down(std::size_t n) {
	std::unique_lock lock(mutex);
	if (n > count) {
		count = 0;
	} else {
		count -= n;
	}
	if (count == 0) {
		cv.notify_all();
	}
}

} // namespace util
