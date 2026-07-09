/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <multi/recipe.h>
#include <type_traits>

namespace
{
	struct ReturnsInt
	{
		int operator()() const { return 1; }
	};

	template <class T, class = void>
	struct CanCallStepWithIntReturn : std::false_type
	{
	};

	template <class T>
	struct CanCallStepWithIntReturn<T, std::void_t<decltype(std::declval<T&>().step(ReturnsInt{}))>>
		: std::true_type
	{
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
	static_assert(!CanCallStepWithIntReturn<multi::Recipe>::value,
				  "Recipe::step accepts void() callables only");

	multi::Recipe recipe;
	CHECK(recipe.state() == multi::RecipeState::Idle);
	CHECK_FALSE(recipe.running());
	CHECK(recipe.stepCount() == 0);
	CHECK(recipe.finishedCount() == 0);
	CHECK(recipe.progress() == 1.0f);

	auto step = recipe.step([]() {});

	CHECK(step.valid());
	CHECK(static_cast<bool>(step));
	CHECK(recipe.stepCount() == 1);
	CHECK(recipe.finishedCount() == 0);
	CHECK(recipe.progress() == 0.0f);
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

TEST_CASE("Recipe: clear removes steps and invalidates old handles")
{
	multi::Recipe recipe;
	auto old = recipe.step([]() {});
	REQUIRE(old);

	CHECK(recipe.clear() == multi::RecipeResult::Ok);
	CHECK(recipe.stepCount() == 0);
	CHECK(recipe.finishedCount() == 0);
	CHECK(recipe.progress() == 1.0f);
	CHECK_FALSE(old.valid());
	CHECK(old.before(old) == multi::RecipeResult::InvalidStep);

	auto fresh = recipe.step([]() {});
	CHECK(fresh.valid());
	CHECK_FALSE(old.valid());
	CHECK(old.before(fresh) == multi::RecipeResult::InvalidStep);
}

TEST_CASE("Recipe: is neither copyable nor movable")
{
	static_assert(!std::is_copy_constructible_v<multi::Recipe>,
				  "Recipe owns Step handle identity and must not be copied");
	static_assert(!std::is_copy_assignable_v<multi::Recipe>,
				  "Recipe owns Step handle identity and must not be copy-assigned");
	static_assert(!std::is_move_constructible_v<multi::Recipe>,
				  "Recipe address stability keeps Step handles simple");
	static_assert(!std::is_move_assignable_v<multi::Recipe>,
				  "Recipe address stability keeps Step handles simple");
}
