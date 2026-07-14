/*
 *  Created by LuckyNeko on 13/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>

#include "workloads.h"

#include <atomic>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
	std::uint64_t buildRecipeChain(int steps)
	{
		multi::Recipe recipe;
		std::vector<multi::Step> nodes;
		nodes.reserve(static_cast<std::size_t>(steps));

		for (int i = 0; i < steps; ++i)
			nodes.push_back(recipe.step([]() {}));

		for (int i = 1; i < steps; ++i)
			recipe.order(nodes[static_cast<std::size_t>(i - 1)] >> nodes[static_cast<std::size_t>(i)]);

		return static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t buildRecipeReverseChain(int steps)
	{
		multi::Recipe recipe;
		std::vector<multi::Step> nodes;
		nodes.reserve(static_cast<std::size_t>(steps));

		for (int i = 0; i < steps; ++i)
			nodes.push_back(recipe.step([]() {}));

		for (int i = steps - 1; i > 0; --i)
			recipe.order(nodes[static_cast<std::size_t>(i - 1)] >> nodes[static_cast<std::size_t>(i)]);

		return static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t buildRecipeDenseForward(int steps, int width)
	{
		multi::Recipe recipe;
		std::vector<multi::Step> nodes;
		nodes.reserve(static_cast<std::size_t>(steps));

		for (int i = 0; i < steps; ++i)
			nodes.push_back(recipe.step([]() {}));

		for (int i = steps - 1; i >= 0; --i)
		{
			for (int offset = 1; offset <= width && i + offset < steps; ++offset)
				recipe.order(nodes[static_cast<std::size_t>(i)] >> nodes[static_cast<std::size_t>(i + offset)]);
		}

		return static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t runRecipeChain(int steps)
	{
		std::atomic<std::uint64_t> count{0};
		multi::Recipe recipe;
		std::vector<multi::Step> nodes;
		nodes.reserve(static_cast<std::size_t>(steps));

		for (int i = 0; i < steps; ++i)
		{
			nodes.push_back(recipe.step([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			}));
		}

		for (int i = 1; i < steps; ++i)
			recipe.order(nodes[static_cast<std::size_t>(i - 1)] >> nodes[static_cast<std::size_t>(i)]);

		auto handle = multi::async(std::move(recipe));
		multi::waitAll(handle);
		handle.get();
		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t runAsyncChain(int steps)
	{
		std::atomic<std::uint64_t> count{0};
		for (int i = 0; i < steps; ++i)
		{
			auto handle = multi::async([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			});
			multi::waitAll(handle);
			handle.get();
		}
		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t runRecipeIndependent(int steps)
	{
		std::atomic<std::uint64_t> count{0};
		multi::Recipe recipe;

		for (int i = 0; i < steps; ++i)
		{
			recipe.step([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			});
		}

		auto handle = multi::async(std::move(recipe));
		multi::waitAll(handle);
		handle.get();
		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t buildRecipeIndependent(int steps)
	{
		multi::Recipe recipe;

		for (int i = 0; i < steps; ++i)
			recipe.step([]() {});

		return static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t runAsyncIndependent(int steps)
	{
		std::atomic<std::uint64_t> count{0};
		std::vector<multi::Handle<>> handles;
		handles.reserve(static_cast<std::size_t>(steps));

		for (int i = 0; i < steps; ++i)
		{
			handles.push_back(multi::async([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			}));
		}
		for (auto& handle : handles)
		{
			multi::waitAll(handle);
			handle.get();
		}

		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t buildRecipeFanOut(int branches)
	{
		multi::Recipe recipe;

		auto source = recipe.step([]() {});
		for (int i = 0; i < branches; ++i)
		{
			auto branch = recipe.step([]() {});
			recipe.order(source >> branch);
		}

		return static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t buildRecipeDuplicateFanOut(int branches, int duplicates)
	{
		multi::Recipe recipe;
		auto source = recipe.step([]() {});
		std::vector<multi::Step> branchSteps;
		branchSteps.reserve(static_cast<std::size_t>(branches));

		for (int i = 0; i < branches; ++i)
		{
			branchSteps.push_back(recipe.step([]() {}));
			recipe.order(source >> branchSteps.back());
		}

		std::uint64_t duplicateCount = 0;
		for (int i = 0; i < duplicates; ++i)
		{
			if (recipe.order(source >> branchSteps.back()) == multi::RecipeResult::DuplicateEdge)
				++duplicateCount;
		}

		return duplicateCount + static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t runRecipeFanOut(int branches)
	{
		std::atomic<std::uint64_t> count{0};
		multi::Recipe recipe;

		auto source = recipe.step([&count]()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		});

		for (int i = 0; i < branches; ++i)
		{
			auto branch = recipe.step([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			});
			recipe.order(source >> branch);
		}

		auto handle = multi::async(std::move(recipe));
		multi::waitAll(handle);
		handle.get();
		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t runAsyncFanOut(int branches)
	{
		std::atomic<std::uint64_t> count{0};

		auto source = multi::async([&count]()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		});
		multi::waitAll(source);
		source.get();

		std::vector<multi::Handle<>> handles;
		handles.reserve(static_cast<std::size_t>(branches));
		for (int i = 0; i < branches; ++i)
		{
			handles.push_back(multi::async([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			}));
		}
		for (auto& handle : handles)
		{
			multi::waitAll(handle);
			handle.get();
		}

		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t buildRecipeFanIn(int branches)
	{
		multi::Recipe recipe;

		auto source = recipe.step([]() {});

		std::vector<multi::Step> branchSteps;
		branchSteps.reserve(static_cast<std::size_t>(branches));
		for (int i = 0; i < branches; ++i)
		{
			branchSteps.push_back(recipe.step([]() {}));
			recipe.order(source >> branchSteps.back());
		}

		auto join = recipe.step([]() {});
		for (auto branch : branchSteps)
			recipe.order(branch >> join);

		return static_cast<std::uint64_t>(recipe.stepCount());
	}

	std::uint64_t runRecipeFanIn(int branches)
	{
		std::atomic<std::uint64_t> count{0};
		multi::Recipe recipe;

		auto source = recipe.step([&count]()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		});

		std::vector<multi::Step> branchSteps;
		branchSteps.reserve(static_cast<std::size_t>(branches));
		for (int i = 0; i < branches; ++i)
		{
			branchSteps.push_back(recipe.step([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			}));
			recipe.order(source >> branchSteps.back());
		}

		auto join = recipe.step([&count]()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		});
		for (auto branch : branchSteps)
			recipe.order(branch >> join);

		auto handle = multi::async(std::move(recipe));
		multi::waitAll(handle);
		handle.get();
		return count.load(std::memory_order_relaxed);
	}

	std::uint64_t runAsyncFanIn(int branches)
	{
		std::atomic<std::uint64_t> count{0};

		auto source = multi::async([&count]()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		});
		multi::waitAll(source);
		source.get();

		std::vector<multi::Handle<>> handles;
		handles.reserve(static_cast<std::size_t>(branches));
		for (int i = 0; i < branches; ++i)
		{
			handles.push_back(multi::async([&count]()
			{
				count.fetch_add(1, std::memory_order_relaxed);
			}));
		}
		for (auto& handle : handles)
		{
			multi::waitAll(handle);
			handle.get();
		}

		auto join = multi::async([&count]()
		{
			count.fetch_add(1, std::memory_order_relaxed);
		});
		multi::waitAll(join);
		join.get();

		return count.load(std::memory_order_relaxed);
	}

	template <class F>
	std::uint64_t repeat(int rounds, F&& f)
	{
		std::uint64_t total = 0;
		for (int i = 0; i < rounds; ++i)
			total += f();
		return total;
	}
} // namespace

// ---------------------------------------------------------------------------
// recipe — dependency-aware async scheduling.
// Compares a single Recipe run against equivalent explicit async orchestration.
// Recipe is single-use, so these timings include lightweight graph construction
// as well as execution and RecipeHandle observation. Recipe construction is
// also measured in separate build-only sections, outside the async baseline
// speedup groups.
// ---------------------------------------------------------------------------
TEST_CASE("recipe", "[bench][fast]")
{
	SECTION("run / linear 16 steps")
	{
		constexpr int rounds = 100;
		REQUIRE(runAsyncChain(16) == 16);
		REQUIRE(runRecipeChain(16) == 16);
		BENCHMARK("async(baseline)") { return repeat(rounds, []() { return runAsyncChain(16); }); };
		BENCHMARK("multi(recipe)") { return repeat(rounds, []() { return runRecipeChain(16); }); };
	}

	SECTION("build / linear 1000 steps")
	{
		REQUIRE(buildRecipeChain(1000) == 1000);
		BENCHMARK("recipe(build)") { return buildRecipeChain(1000); };
	}

	SECTION("build / reverse linear 1000 steps")
	{
		REQUIRE(buildRecipeReverseChain(1000) == 1000);
		BENCHMARK("recipe(build)") { return buildRecipeReverseChain(1000); };
	}

	SECTION("build / dense width 4 1000 steps")
	{
		REQUIRE(buildRecipeDenseForward(1000, 4) == 1000);
		BENCHMARK("recipe(build)") { return buildRecipeDenseForward(1000, 4); };
	}

	SECTION("run / independent 32 steps")
	{
		constexpr int rounds = 100;
		REQUIRE(runAsyncIndependent(32) == 32);
		REQUIRE(runRecipeIndependent(32) == 32);
		BENCHMARK("async(baseline)") { return repeat(rounds, []() { return runAsyncIndependent(32); }); };
		BENCHMARK("multi(recipe)") { return repeat(rounds, []() { return runRecipeIndependent(32); }); };
	}

	SECTION("build / independent 1000 steps")
	{
		REQUIRE(buildRecipeIndependent(1000) == 1000);
		BENCHMARK("recipe(build)") { return buildRecipeIndependent(1000); };
	}

	SECTION("run / fan-out 64 branches")
	{
		constexpr int rounds = 100;
		REQUIRE(runAsyncFanOut(64) == 65);
		REQUIRE(runRecipeFanOut(64) == 65);
		BENCHMARK("async(baseline)") { return repeat(rounds, []() { return runAsyncFanOut(64); }); };
		BENCHMARK("multi(recipe)") { return repeat(rounds, []() { return runRecipeFanOut(64); }); };
	}

	SECTION("build / fan-out 1000 branches")
	{
		REQUIRE(buildRecipeFanOut(1000) == 1001);
		BENCHMARK("recipe(build)") { return buildRecipeFanOut(1000); };
	}

	SECTION("build / fan-out dup 1000")
	{
		REQUIRE(buildRecipeDuplicateFanOut(1000, 1000) == 2001);
		BENCHMARK("recipe(build)") { return buildRecipeDuplicateFanOut(1000, 1000); };
	}

	SECTION("run / fan-in 32 branches")
	{
		constexpr int rounds = 100;
		REQUIRE(runAsyncFanIn(32) == 34);
		REQUIRE(runRecipeFanIn(32) == 34);
		BENCHMARK("async(baseline)") { return repeat(rounds, []() { return runAsyncFanIn(32); }); };
		BENCHMARK("multi(recipe)") { return repeat(rounds, []() { return runRecipeFanIn(32); }); };
	}

	SECTION("build / fan-in 1000 branches")
	{
		REQUIRE(buildRecipeFanIn(1000) == 1002);
		BENCHMARK("recipe(build)") { return buildRecipeFanIn(1000); };
	}
}
