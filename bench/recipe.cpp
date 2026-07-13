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
// as well as execution and RecipeHandle observation.
// ---------------------------------------------------------------------------
TEST_CASE("recipe", "[bench][fast]")
{
	SECTION("linear 16 steps")
	{
		constexpr int rounds = 100;
		REQUIRE(runAsyncChain(16) == 16);
		REQUIRE(runRecipeChain(16) == 16);
		BENCHMARK("multi(async)") { return repeat(rounds, []() { return runAsyncChain(16); }); };
		BENCHMARK("multi(recipe)") { return repeat(rounds, []() { return runRecipeChain(16); }); };
	}

	SECTION("fan-in 32 branches")
	{
		constexpr int rounds = 100;
		REQUIRE(runAsyncFanIn(32) == 34);
		REQUIRE(runRecipeFanIn(32) == 34);
		BENCHMARK("multi(async)") { return repeat(rounds, []() { return runAsyncFanIn(32); }); };
		BENCHMARK("multi(recipe)") { return repeat(rounds, []() { return runRecipeFanIn(32); }); };
	}
}
