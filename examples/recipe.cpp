// recipe.cpp — execution-only DAG with Recipe / StepLink.
//
// Covers:
//   - recipe.step(fn) for void tasks
//   - recipe.order(a >> b) for execution dependencies
//   - Context/global async consuming a Recipe and returning RecipeHandle
//   - RecipeHandle progress plus user exception observation

#include <multi/multi.h>

#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <utility>

int main()
{
	// Default pool: hardware_concurrency()-1 workers.
	multi::start();

	// A small fan-out/fan-in recipe:
	//
	//   load
	//   /  \
	// left right
	//   \  /
	//   join
	std::atomic<int> loaded{0};
	std::atomic<int> branches{0};
	std::atomic<int> errors{0};

	multi::Recipe recipe;
	auto load = recipe.step([&]()
	{
		loaded.store(1, std::memory_order_release);
	});
	auto left = recipe.step([&]()
	{
		if (loaded.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		branches.fetch_or(1, std::memory_order_acq_rel);
	});
	auto right = recipe.step([&]()
	{
		if (loaded.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		branches.fetch_or(2, std::memory_order_acq_rel);
	});
	auto join = recipe.step([&]()
	{
		if (branches.load(std::memory_order_acquire) != 3)
			errors.fetch_add(1, std::memory_order_relaxed);
	});

	const bool orderOk =
		recipe.order(load >> left) == multi::RecipeResult::Ok &&
		recipe.order(load >> right) == multi::RecipeResult::Ok &&
		recipe.order(left >> join) == multi::RecipeResult::Ok &&
		recipe.order(right >> join) == multi::RecipeResult::Ok;

	auto handle = multi::async(std::move(recipe));
	multi::waitAll(handle);

	const bool recipeOk =
		orderOk &&
		errors.load(std::memory_order_relaxed) == 0 &&
		handle.stepCount() == 4 &&
		handle.finishedCount() == 4 &&
		handle.progress() == 1.0f &&
		handle.get();
	std::printf("recipe fan-out/fan-in: %zu/%zu finished -> %s\n",
	            handle.finishedCount(), handle.stepCount(), recipeOk ? "ok" : "FAIL");

	// User exceptions are observed through RecipeHandle::get(). Dependents of
	// the failed step are skipped, while the run still reaches 100% progress.
	std::atomic<int> dependentRan{0};
	multi::Recipe failing;
	auto fail = failing.step([]()
	{
		throw std::runtime_error("example failure");
	});
	auto dependent = failing.step([&]()
	{
		dependentRan.store(1, std::memory_order_release);
	});

	const bool failOrderOk = failing.order(fail >> dependent) == multi::RecipeResult::Ok;
	auto failingHandle = multi::async(std::move(failing));
	multi::waitAll(failingHandle);

	bool threw = false;
	try
	{
		failingHandle.get();
	}
	catch (const std::runtime_error&)
	{
		threw = true;
	}

	const bool failureOk =
		failOrderOk &&
		threw &&
		dependentRan.load(std::memory_order_acquire) == 0 &&
		failingHandle.finishedCount() == failingHandle.stepCount() &&
		failingHandle.progress() == 1.0f;
	std::printf("recipe failure skips dependent: %s\n",
	            failureOk ? "ok" : "FAIL");

	multi::stop();
	return (recipeOk && failureOk) ? 0 : 1;
}
