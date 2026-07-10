/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <atomic>
#include <multi/multi.h>
#include <multi/recipe.h>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace
{
	struct ReturnsInt
	{
		int operator()() const { return 1; }
	};

	struct MoveOnlyVoid
	{
		MoveOnlyVoid() = default;
		MoveOnlyVoid(const MoveOnlyVoid&) = delete;
		MoveOnlyVoid(MoveOnlyVoid&&) = default;
		void operator()() const {}
	};

} // namespace

TEST_CASE("Recipe: default step is invalid")
{
	multi::Step step;

	CHECK_FALSE(step.valid());
	CHECK_FALSE(static_cast<bool>(step));
	CHECK(step.before(step) == multi::RecipeResult::InvalidStep);
}

TEST_CASE("Recipe: step adds a valid void callable")
{
	multi::Recipe recipe;
	CHECK(recipe.stepCount() == 0);

	auto step = recipe.step([]() {});

	CHECK(step.valid());
	CHECK(static_cast<bool>(step));
	CHECK(recipe.stepCount() == 1);

	auto moveOnly = recipe.step(MoveOnlyVoid{});
	CHECK(moveOnly.valid());
	CHECK(recipe.stepCount() == 2);

	auto returnsValue = recipe.step(ReturnsInt{});
	CHECK(returnsValue.valid());
	CHECK(recipe.stepCount() == 3);
}

TEST_CASE("Recipe: before links two steps")
{
	multi::Recipe recipe;
	auto first = recipe.step([]() {});
	auto second = recipe.step([]() {});

	CHECK(first.before(second) == multi::RecipeResult::Ok);
	CHECK(first.before(second) == multi::RecipeResult::DuplicateEdge);
}

TEST_CASE("Recipe: before rejects invalid and cross-recipe steps")
{
	multi::Recipe recipe;
	multi::Recipe other;

	auto step = recipe.step([]() {});
	auto otherStep = other.step([]() {});
	multi::Step invalid;

	CHECK(step.before(invalid) == multi::RecipeResult::InvalidStep);
	CHECK(invalid.before(step) == multi::RecipeResult::InvalidStep);
	CHECK(step.before(otherStep) == multi::RecipeResult::DifferentRecipe);
}

TEST_CASE("Recipe: before rejects cycles")
{
	multi::Recipe recipe;
	auto a = recipe.step([]() {});
	auto b = recipe.step([]() {});
	auto c = recipe.step([]() {});

	CHECK(a.before(a) == multi::RecipeResult::WouldCycle);
	CHECK(a.before(b) == multi::RecipeResult::Ok);
	CHECK(b.before(c) == multi::RecipeResult::Ok);
	CHECK(c.before(a) == multi::RecipeResult::WouldCycle);
	CHECK(c.before(b) == multi::RecipeResult::WouldCycle);
}

TEST_CASE("Recipe: is move-only")
{
	static_assert(!std::is_copy_constructible_v<multi::Recipe>,
				  "Recipe owns Step handle identity and must not be copied");
	static_assert(!std::is_copy_assignable_v<multi::Recipe>,
				  "Recipe owns Step handle identity and must not be copy-assigned");
	static_assert(std::is_move_constructible_v<multi::Recipe>,
				  "Recipe is consumed by async");
	static_assert(std::is_move_assignable_v<multi::Recipe>,
				  "Recipe is consumed by async");
}

TEST_CASE("RecipeHandle: is move-only")
{
	static_assert(!std::is_copy_constructible_v<multi::RecipeHandle>,
				  "RecipeHandle owns the run observation handle");
	static_assert(!std::is_copy_assignable_v<multi::RecipeHandle>,
				  "RecipeHandle owns the run observation handle");
	static_assert(std::is_move_constructible_v<multi::RecipeHandle>,
				  "RecipeHandle can transfer run observation");
	static_assert(!std::is_move_assignable_v<multi::RecipeHandle>,
				  "RecipeHandle follows Handle assignment rules");
}

TEST_CASE("Recipe: empty async succeeds")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2));
	multi::Context context;
	context.start(threadCount);

	multi::Recipe recipe;
	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	context.waitAll(h);

	context.stop();
}

TEST_CASE("Recipe: handle reports run progress")
{
	multi::Context context;
	context.start(1);

	std::atomic<bool> entered{false};
	std::atomic<bool> release{false};
	std::atomic<int> ran{0};

	multi::Recipe recipe;
	auto first = recipe.step([&]()
	{
		entered.store(true, std::memory_order_release);
		while (!release.load(std::memory_order_acquire))
			std::this_thread::yield();
		ran.fetch_add(1, std::memory_order_relaxed);
	});
	auto second = recipe.step([&]()
	{
		ran.fetch_add(1, std::memory_order_relaxed);
	});
	REQUIRE(first.before(second) == multi::RecipeResult::Ok);

	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	CHECK(h.stepCount() == 2);

	while (!entered.load(std::memory_order_acquire))
		std::this_thread::yield();

	CHECK(h.finishedCount() == 0);
	CHECK(h.progress() == Catch::Approx(0.0f));

	release.store(true, std::memory_order_release);
	context.waitAll(h);
	CHECK(h.finishedCount() == 2);
	CHECK(h.progress() == Catch::Approx(1.0f));
	CHECK(ran.load(std::memory_order_relaxed) == 2);
	CHECK(h.get());

	context.stop();
}

TEST_CASE("Recipe: handle rethrows failed step and counts skipped successors")
{
	multi::Context context;
	context.start(0);

	std::atomic<int> dependentRan{0};
	multi::Recipe recipe;
	auto failing = recipe.step([]()
	{
		throw std::runtime_error("recipe step failed");
	});
	auto dependent = recipe.step([&]()
	{
		dependentRan.store(1, std::memory_order_release);
	});
	REQUIRE(failing.before(dependent) == multi::RecipeResult::Ok);

	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	CHECK(h.complete());
	CHECK(h.stepCount() == 2);
	CHECK(h.finishedCount() == 2);
	CHECK(h.progress() == Catch::Approx(1.0f));
	CHECK(dependentRan.load(std::memory_order_acquire) == 0);
	CHECK_THROWS_AS(h.get(), std::runtime_error);

	context.stop();
}

TEST_CASE("Recipe: async follows a linear chain")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(2), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::atomic<int> phase{0};
	std::atomic<int> errors{0};
	multi::Recipe recipe;
	auto a = recipe.step([&]()
	{
		if (phase.exchange(1, std::memory_order_acq_rel) != 0)
			errors.fetch_add(1, std::memory_order_relaxed);
	});
	auto b = recipe.step([&]()
	{
		if (phase.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		phase.store(2, std::memory_order_release);
	});
	auto c = recipe.step([&]()
	{
		if (phase.load(std::memory_order_acquire) != 2)
			errors.fetch_add(1, std::memory_order_relaxed);
		phase.store(3, std::memory_order_release);
	});
	REQUIRE(a.before(b) == multi::RecipeResult::Ok);
	REQUIRE(b.before(c) == multi::RecipeResult::Ok);

	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	context.waitAll(h);
	CHECK(errors.load(std::memory_order_relaxed) == 0);
	CHECK(phase.load(std::memory_order_acquire) == 3);

	context.stop();
}

TEST_CASE("Recipe: async handles fan-out and fan-in")
{
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1), std::size_t(4));
	multi::Context context;
	context.start(threadCount);

	std::atomic<int> sourceDone{0};
	std::atomic<int> branchBits{0};
	std::atomic<int> errors{0};
	multi::Recipe recipe;
	auto source = recipe.step([&]()
	{
		sourceDone.store(1, std::memory_order_release);
	});
	auto left = recipe.step([&]()
	{
		if (sourceDone.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		branchBits.fetch_or(1, std::memory_order_acq_rel);
	});
	auto right = recipe.step([&]()
	{
		if (sourceDone.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		branchBits.fetch_or(2, std::memory_order_acq_rel);
	});
	auto join = recipe.step([&]()
	{
		if (branchBits.load(std::memory_order_acquire) != 3)
			errors.fetch_add(1, std::memory_order_relaxed);
	});
	REQUIRE(source.before(left) == multi::RecipeResult::Ok);
	REQUIRE(source.before(right) == multi::RecipeResult::Ok);
	REQUIRE(left.before(join) == multi::RecipeResult::Ok);
	REQUIRE(right.before(join) == multi::RecipeResult::Ok);

	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	context.waitAll(h);
	CHECK(errors.load(std::memory_order_relaxed) == 0);

	context.stop();
}

TEST_CASE("Recipe: async consumes the recipe")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> count{0};
	multi::Recipe recipe;
	auto first = recipe.step([&]() { count.fetch_add(1, std::memory_order_relaxed); });
	auto second = recipe.step([&]() { count.fetch_add(10, std::memory_order_relaxed); });
	REQUIRE(first.before(second) == multi::RecipeResult::Ok);

	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	context.waitAll(h);
	CHECK(count.load(std::memory_order_relaxed) == 11);

	context.stop();
}

TEST_CASE("Recipe: async launches independent recipes concurrently")
{
	multi::Context context;
	context.start(1);

	std::atomic<bool> entered{false};
	std::atomic<bool> release{false};
	multi::Recipe slow;
	slow.step([&]()
	{
		entered.store(true, std::memory_order_release);
		while (!release.load(std::memory_order_acquire))
			std::this_thread::yield();
	});

	std::atomic<int> fastRan{0};
	multi::Recipe fast;
	fast.step([&]() { fastRan.store(1, std::memory_order_release); });

	auto slowHandle = context.async(std::move(slow));
	REQUIRE(slowHandle.valid());
	while (!entered.load(std::memory_order_acquire))
		std::this_thread::yield();

	auto fastHandle = context.async(std::move(fast));
	REQUIRE(fastHandle.valid());
	context.waitAll(fastHandle);
	CHECK(fastRan.load(std::memory_order_acquire) == 1);

	release.store(true, std::memory_order_release);
	context.waitAll(slowHandle);

	context.stop();
}

TEST_CASE("Recipe: async job survives recipe destruction")
{
	multi::Context context;
	context.start(2);

	std::atomic<int> ran{0};
	auto h = [&]()
	{
		multi::Recipe recipe;
		recipe.step([&]() { ran.store(1, std::memory_order_release); });
		return context.async(std::move(recipe));
	}();

	REQUIRE(h.valid());
	context.waitAll(h);
	CHECK(ran.load(std::memory_order_acquire) == 1);

	context.stop();
}

TEST_CASE("Recipe: global async uses the global context")
{
	multi::start(2);

	std::atomic<int> ran{0};
	multi::Recipe recipe;
	recipe.step([&]() { ran.store(1, std::memory_order_release); });

	auto h = multi::async(std::move(recipe));
	REQUIRE(h.valid());
	multi::waitAll(h);
	CHECK(ran.load(std::memory_order_acquire) == 1);

	multi::stop();
}
