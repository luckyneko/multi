/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/recipegraph.h"
#include "multi/details/task.h"

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace multi
{
	class Recipe;
}

namespace multi::details
{
	// Steps a RecipeJob runner took back from inline submits, oldest first,
	// linked through RecipeJob::m_pending (see RecipeJob::runStep).
	struct RecipeDeferredSteps
	{
		static constexpr std::size_t None = ~std::size_t{0};
		std::size_t head = None;
		std::size_t tail = None;
	};

	class RecipeJob : public std::enable_shared_from_this<RecipeJob>
	{
	public:
		using Submit = std::function<void(Task&&)>;

		RecipeJob(Recipe&& recipe, Submit submit);

		std::future<void> getFuture();

		void start();
		void runStep(std::size_t index) noexcept;
		std::size_t stepCount() const noexcept;
		std::size_t finishedCount() const noexcept;
		float progress() const noexcept;

	private:
		using Deferred = RecipeDeferredSteps;

		void schedule(std::size_t index, Deferred& deferred);
		void defer(std::size_t index, Deferred& deferred) noexcept;
		void runChain(std::size_t index, Deferred& deferred) noexcept;
		void runDeferred(Deferred& deferred) noexcept;
		bool runStepOnce(std::size_t index, std::size_t& inlineSuccessor, Deferred& deferred) noexcept;
		bool executeStep(std::size_t index) noexcept;
		bool scheduleReadySuccessors(std::size_t index, std::size_t& inlineSuccessor, Deferred& deferred);
		bool tryMarkScheduled(std::size_t index) noexcept;
		void skipSuccessors(std::size_t index) noexcept;
		// Claims a Pending step as skipped and counts it; false if another
		// thread ran, scheduled or skipped it first.
		bool skip(std::size_t index) noexcept;
		void finishOne() noexcept;
		void complete() noexcept;

		enum : int
		{
			Pending,
			Scheduled,
			Finished
		};

		Submit m_submit;
		RecipeGraph m_graph;
		// Per step: predecessors that have not finished yet. Once a step is
		// Scheduled its count has reached zero and no predecessor touches it
		// again, so its runner reuses the slot as the link of a deferred-step
		// list (runStep): deferring a step allocates nothing. start()'s root
		// scan may still read such a link; a Scheduled step fails
		// tryMarkScheduled whatever its slot holds.
		std::vector<std::atomic<std::size_t>> m_pending;
		std::vector<std::atomic<int>> m_state;
		std::atomic<std::size_t> m_remaining;
		std::once_flag m_exceptionOnce;
		std::exception_ptr m_firstException;
		std::once_flag m_completeOnce;
		std::promise<void> m_promise;
	};
} // namespace multi::details
