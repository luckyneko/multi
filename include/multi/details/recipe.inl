/*
 *  Created by LuckyNeko on 09/07/2026.
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <utility>

namespace multi
{
	template <class F, class D, std::enable_if_t<std::is_invocable_v<D> &&
													 std::is_void_v<std::invoke_result_t<D>>,
												 int>>
	Step Recipe::step(F&& f)
	{
		if (running())
			return {};

		m_steps.push_back(Entry{details::Task(std::forward<F>(f)), {}, 0});
		m_finished.store(0, std::memory_order_relaxed);
		return Step(this, m_steps.size() - 1, m_generation);
	}
} // namespace multi

