// hello.cpp — minimal start/stop + a single async task.
//
// Covers:
//   - multi::start / multi::stop pool lifecycle
//   - multi::async returning Handle<T>
//   - multi::waitAll to wait (with steal participation), then Handle::get(&out)

#include <multi/multi.h>

#include <cstdio>

int main()
{
	// No argument → default pool of hardware_concurrency()-1 workers, leaving
	// a core for the calling thread (which also participates in work-stealing).
	multi::start();

	// Typed async: lambda returns int → Handle<int>. The Handle itself never
	// blocks; wait on it with multi::waitAll (which participates in stealing),
	// then get(&out) copies the value out and returns true once complete.
	auto greeting = multi::async([]() {
		return 42;
	});
	multi::waitAll(greeting);
	int answer = 0;
	greeting.get(&answer);
	std::printf("got %d from worker thread\n", answer);

	// Fire-and-forget: a Handle you don't keep does NOT block on destruction.
	// The task still runs on the pool and is guaranteed to have finished once
	// stop() drains the workers below.
	multi::async([]() {
		std::printf("side-effect task ran\n");
	});

	multi::stop();
	return (answer == 42) ? 0 : 1;
}
