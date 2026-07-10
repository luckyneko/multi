/*
 *  Created by LuckyNeko on 10/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include "multi/handle.h"

#include <cstddef>
#include <memory>

namespace multi
{
	namespace details
	{
		class RecipeJob;
	}

	/// Handle for an asynchronously running Recipe.
	class RecipeHandle
	{
	public:
		RecipeHandle() = default;
		RecipeHandle(Handle<>&& handle, std::shared_ptr<details::RecipeJob> job) noexcept;
		RecipeHandle(const RecipeHandle&) = delete;
		RecipeHandle& operator=(const RecipeHandle&) = delete;
		RecipeHandle(RecipeHandle&&) noexcept = default;
		RecipeHandle& operator=(RecipeHandle&&) noexcept = delete;

		bool complete() const;
		bool valid() const;
		void wait() const;
		bool get();

		std::size_t stepCount() const noexcept;
		std::size_t finishedCount() const noexcept;
		float progress() const noexcept;

	private:
		Handle<> m_handle;
		std::shared_ptr<details::RecipeJob> m_job;
	};
} // namespace multi
