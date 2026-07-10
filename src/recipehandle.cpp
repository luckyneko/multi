/*
 *  Created by LuckyNeko on 10/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/recipehandle.h"
#include "multi/details/recipejob.h"

namespace multi
{
	RecipeHandle::RecipeHandle(Handle<>&& handle, std::shared_ptr<details::RecipeJob> job) noexcept
		: m_handle(std::move(handle))
		, m_job(std::move(job))
	{
	}

	bool RecipeHandle::complete() const
	{
		return m_handle.complete();
	}

	bool RecipeHandle::valid() const
	{
		return m_handle.valid();
	}

	void RecipeHandle::wait() const
	{
		m_handle.wait();
	}

	bool RecipeHandle::get()
	{
		return m_handle.get();
	}

	std::size_t RecipeHandle::stepCount() const noexcept
	{
		return m_job ? m_job->stepCount() : 0;
	}

	std::size_t RecipeHandle::finishedCount() const noexcept
	{
		return m_job ? m_job->finishedCount() : 0;
	}

	float RecipeHandle::progress() const noexcept
	{
		return m_job ? m_job->progress() : 1.0f;
	}
} // namespace multi
