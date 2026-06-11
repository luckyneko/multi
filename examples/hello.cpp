// hello.cpp — minimal start/stop + a single async task.
//
// Covers:
//   - multi::start / multi::stop pool lifecycle
//   - multi::async returning Handle<T>; .get() retrieves the value
//   - Handle's destructor auto-waits when not observed

#include <multi/multi.h>

#include <cstdio>

int main()
{
	// No argument → default pool of hardware_concurrency()-1 workers, leaving
	// a core for the calling thread (which also participates in work-stealing).
	multi::start();

	// Typed async: lambda returns int → Handle<int>. .get() blocks until
	// the task finishes and returns the value (or rethrows on exception).
	auto greeting = multi::async([]() {
		return 42;
	});
	const int answer = greeting.get();
	std::printf("got %d from worker thread\n", answer);

	// Void async: the Handle's dtor auto-waits at end of scope. Useful
	// for fire-and-don't-care patterns where you just need the task to
	// run before continuing.
	multi::async([]() {
		std::printf("side-effect task ran\n");
	});  // dtor here blocks until the task completes

	multi::stop();
	return (answer == 42) ? 0 : 1;
}
