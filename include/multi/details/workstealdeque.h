/*
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_WORKSTEALDEQUE_H_
#define _MULTI_WORKSTEALDEQUE_H_

#include "multi/task.h"

#include <deque>
#include <mutex>

namespace multi
{
	/*
	 * WorkStealDeque
	 * Per-worker deque supporting local push/pop (LIFO) and remote steal (FIFO).
	 * Mutex-based: contention is low because only the owning worker pushes/pops,
	 * and steals from other workers are infrequent.
	 */
	class WorkStealDeque
	{
	public:
		// Push a task to the bottom (called by owning worker or submitter)
		void push(Task&& task);

		// Pop a task from the bottom, LIFO (called by owning worker)
		// Returns true if a task was obtained
		bool pop(Task* task);

		// Steal a task from the top, FIFO (called by any other thread)
		// Returns true if a task was stolen
		bool steal(Task* task);

		// Approximate size (racy but useful for heuristics)
		size_t sizeHint() const;

		// Check if deque appears empty (racy)
		bool empty() const;

	private:
		std::deque<Task> m_deque;
		mutable std::mutex m_mutex;
	};
} // namespace multi

#endif // _MULTI_WORKSTEALDEQUE_H_
