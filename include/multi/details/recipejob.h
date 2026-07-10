/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/details/task.h"
#include "multi/recipe.h"

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace multi::details
{
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
		void schedule(std::size_t index);
		void skip(std::size_t index) noexcept;
		void finishOne() noexcept;
		void complete() noexcept;

		enum : int
		{
			Pending,
			Scheduled,
			Finished
		};

		Submit m_submit;
		Recipe m_recipe;
		std::vector<std::atomic<std::size_t>> m_pending;
		std::vector<std::atomic<int>> m_state;
		std::atomic<std::size_t> m_remaining;
		std::once_flag m_exceptionOnce;
		std::exception_ptr m_firstException;
		std::once_flag m_completeOnce;
		std::promise<void> m_promise;
	};
} // namespace multi::details
