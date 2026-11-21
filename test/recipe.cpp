/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <atomic>
#include <catch2/catch_all.hpp>
#include <cstddef>
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

	// spineLength spine steps, each with one leaf. A spine step's successors
	// are [its leaf, the next spine step], linked in that order, so the leaf
	// is the successor the executor continues with on the same thread and the
	// next spine step is the one it submits. Every step runs a copy of body.
	template <class F>
	multi::Recipe makeSpineWithLeaves(std::size_t spineLength, const F& body, bool& linked)
	{
		multi::Recipe recipe(2 * spineLength);
		multi::Step previousSpine;
		for (std::size_t i = 0; i < spineLength; ++i)
		{
			auto spine = recipe.step(body);
			auto leaf = recipe.step(body);
			linked = linked && recipe.order(spine >> leaf) == multi::RecipeResult::Ok;
			if (previousSpine)
				linked = linked && recipe.order(previousSpine >> spine) == multi::RecipeResult::Ok;
			previousSpine = spine;
		}
		return recipe;
	}

} // namespace

TEST_CASE("Recipe: default step is invalid")
{
	multi::Recipe recipe;
	multi::Step step;

	CHECK_FALSE(step.valid());
	CHECK_FALSE(static_cast<bool>(step));
	CHECK(recipe.order(step >> step) == multi::RecipeResult::InvalidStep);
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

TEST_CASE("Recipe: can reserve expected step count")
{
	multi::Recipe recipe(4);

	auto a = recipe.step([]() {});
	auto b = recipe.step([]() {});

	CHECK(a.valid());
	CHECK(b.valid());
	CHECK(recipe.stepCount() == 2);
	CHECK(recipe.order(a >> b) == multi::RecipeResult::Ok);
}

TEST_CASE("Recipe: order links two steps")
{
	multi::Recipe recipe;
	auto first = recipe.step([]() {});
	auto second = recipe.step([]() {});

	CHECK(recipe.order(first >> second) == multi::RecipeResult::Ok);
	CHECK(recipe.order(first >> second) == multi::RecipeResult::DuplicateEdge);
}

TEST_CASE("Recipe: order rejects duplicates after many successors")
{
	multi::Recipe recipe;
	auto source = recipe.step([]() {});
	multi::Step last;

	for (int i = 0; i < 130; ++i)
	{
		last = recipe.step([]() {});
		REQUIRE(recipe.order(source >> last) == multi::RecipeResult::Ok);
	}

	CHECK(recipe.order(source >> last) == multi::RecipeResult::DuplicateEdge);
}

TEST_CASE("Recipe: order rejects invalid and out-of-range steps")
{
	multi::Recipe recipe;
	multi::Recipe other;

	auto step = recipe.step([]() {});
	other.step([]() {});
	auto outOfRange = other.step([]() {});
	multi::Step invalid;

	CHECK(recipe.order(step >> invalid) == multi::RecipeResult::InvalidStep);
	CHECK(recipe.order(invalid >> step) == multi::RecipeResult::InvalidStep);
	CHECK(recipe.order(step >> outOfRange) == multi::RecipeResult::InvalidStep);
}

TEST_CASE("Recipe: order rejects cycles")
{
	multi::Recipe recipe;
	auto a = recipe.step([]() {});
	auto b = recipe.step([]() {});
	auto c = recipe.step([]() {});

	CHECK(recipe.order(a >> a) == multi::RecipeResult::WouldCycle);
	CHECK(recipe.order(a >> b) == multi::RecipeResult::Ok);
	CHECK(recipe.order(b >> c) == multi::RecipeResult::Ok);
	CHECK(recipe.order(c >> a) == multi::RecipeResult::WouldCycle);
	CHECK(recipe.order(c >> b) == multi::RecipeResult::WouldCycle);
}

TEST_CASE("Recipe: order checks cycles after a legal backward link")
{
	multi::Recipe recipe;
	auto a = recipe.step([]() {});
	auto b = recipe.step([]() {});
	auto c = recipe.step([]() {});

	CHECK(recipe.order(b >> a) == multi::RecipeResult::Ok);
	CHECK(recipe.order(c >> b) == multi::RecipeResult::Ok);
	CHECK(recipe.order(a >> c) == multi::RecipeResult::WouldCycle);
}

TEST_CASE("Recipe: is move-only")
{
	static_assert(!std::is_copy_constructible_v<multi::Recipe>,
				  "Recipe owns move-only tasks and must not be copied");
	static_assert(!std::is_copy_assignable_v<multi::Recipe>,
				  "Recipe owns move-only tasks and must not be copy-assigned");
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
		ran.fetch_add(1, std::memory_order_relaxed); });
	auto second = recipe.step([&]()
							  { ran.fetch_add(1, std::memory_order_relaxed); });
	REQUIRE(recipe.order(first >> second) == multi::RecipeResult::Ok);

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
							   { throw std::runtime_error("recipe step failed"); });
	auto dependent = recipe.step([&]()
								 { dependentRan.store(1, std::memory_order_release); });
	REQUIRE(recipe.order(failing >> dependent) == multi::RecipeResult::Ok);

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
			errors.fetch_add(1, std::memory_order_relaxed); });
	auto b = recipe.step([&]()
						 {
		if (phase.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		phase.store(2, std::memory_order_release); });
	auto c = recipe.step([&]()
						 {
		if (phase.load(std::memory_order_acquire) != 2)
			errors.fetch_add(1, std::memory_order_relaxed);
		phase.store(3, std::memory_order_release); });
	REQUIRE(recipe.order(a >> b) == multi::RecipeResult::Ok);
	REQUIRE(recipe.order(b >> c) == multi::RecipeResult::Ok);

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
							  { sourceDone.store(1, std::memory_order_release); });
	auto left = recipe.step([&]()
							{
		if (sourceDone.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		branchBits.fetch_or(1, std::memory_order_acq_rel); });
	auto right = recipe.step([&]()
							 {
		if (sourceDone.load(std::memory_order_acquire) != 1)
			errors.fetch_add(1, std::memory_order_relaxed);
		branchBits.fetch_or(2, std::memory_order_acq_rel); });
	auto join = recipe.step([&]()
							{
		if (branchBits.load(std::memory_order_acquire) != 3)
			errors.fetch_add(1, std::memory_order_relaxed); });
	REQUIRE(recipe.order(source >> left) == multi::RecipeResult::Ok);
	REQUIRE(recipe.order(source >> right) == multi::RecipeResult::Ok);
	REQUIRE(recipe.order(left >> join) == multi::RecipeResult::Ok);
	REQUIRE(recipe.order(right >> join) == multi::RecipeResult::Ok);

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
	auto first = recipe.step([&]()
							 { count.fetch_add(1, std::memory_order_relaxed); });
	auto second = recipe.step([&]()
							  { count.fetch_add(10, std::memory_order_relaxed); });
	REQUIRE(recipe.order(first >> second) == multi::RecipeResult::Ok);

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
			std::this_thread::yield(); });

	std::atomic<int> fastRan{0};
	multi::Recipe fast;
	fast.step([&]()
			  { fastRan.store(1, std::memory_order_release); });

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
		recipe.step([&]()
					{ ran.store(1, std::memory_order_release); });
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
	recipe.step([&]()
				{ ran.store(1, std::memory_order_release); });

	auto h = multi::async(std::move(recipe));
	REQUIRE(h.valid());
	multi::waitAll(h);
	CHECK(ran.load(std::memory_order_acquire) == 1);

	multi::stop();
}

TEST_CASE("Recipe: inline run of a spine with leaves keeps the stack flat")
{
	// Never started, so every submit runs its task inline on this thread.
	// Each spine step continues with its leaf and submits the next spine
	// step; run inline, that submit must not nest one call per spine step.
	// Sized so that nesting overflows a 1 MB stack in Debug and Release, and
	// an 8 MB one under ASan.
	constexpr std::size_t SpineLength = 100000;
	multi::Context context;

	std::atomic<std::size_t> ran{0};
	bool linked = true;
	auto recipe = makeSpineWithLeaves(
		SpineLength, [&ran]()
		{ ran.fetch_add(1, std::memory_order_relaxed); },
		linked);
	REQUIRE(linked);

	auto h = context.async(std::move(recipe));
	REQUIRE(h.valid());
	CHECK(h.complete());
	CHECK(h.stepCount() == 2 * SpineLength);
	CHECK(h.finishedCount() == 2 * SpineLength);
	CHECK(ran.load(std::memory_order_relaxed) == 2 * SpineLength);
	CHECK(h.get());
}

TEST_CASE("Recipe: a recipe launched from a step runs to completion inside it")
{
	// 0 threads: both recipes run inline on this thread, the inner one from
	// inside a step of the outer one. 1 thread: the outer recipe runs on the
	// worker, which drains the inner recipe through waitAll. Either way the
	// inner recipe's steps belong to the inner run and must finish there,
	// not be queued behind the step that launched it.
	auto threadCount = GENERATE(std::size_t(0), std::size_t(1));
	multi::Context context;
	context.start(static_cast<int>(threadCount));

	constexpr std::size_t OuterSpine = 32;
	constexpr std::size_t InnerSpine = 32;
	std::atomic<std::size_t> outerRan{0};
	std::atomic<std::size_t> innerRan{0};
	std::atomic<int> errors{0};

	auto innerBody = [&innerRan]()
	{ innerRan.fetch_add(1, std::memory_order_relaxed); };
	auto outerBody = [&]()
	{
		outerRan.fetch_add(1, std::memory_order_relaxed);
		bool innerLinked = true;
		auto inner = context.async(makeSpineWithLeaves(InnerSpine, innerBody, innerLinked));
		context.waitAll(inner);
		if (!innerLinked || !inner.complete() || inner.finishedCount() != inner.stepCount())
			errors.fetch_add(1, std::memory_order_relaxed);
	};

	bool linked = true;
	auto h = context.async(makeSpineWithLeaves(OuterSpine, outerBody, linked));
	REQUIRE(linked);
	REQUIRE(h.valid());
	context.waitAll(h);
	CHECK(h.finishedCount() == 2 * OuterSpine);
	CHECK(outerRan.load(std::memory_order_relaxed) == 2 * OuterSpine);
	CHECK(innerRan.load(std::memory_order_relaxed) == 2 * OuterSpine * 2 * InnerSpine);
	CHECK(errors.load(std::memory_order_relaxed) == 0);
	CHECK(h.get());

	context.stop();
}
