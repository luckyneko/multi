#include "multi/details/jobnode.h"

namespace multi
{
	JobNode::JobNode(multi::JobNode* parent, multi::Function&& task, multi::JobNode* next)
		: m_parent(parent)
		, m_func(std::move(task))
		, m_numChildren(0)
		, m_state(State::NONE)
		, m_next(next)
	{
		if(m_parent)
			m_parent->m_numChildren++;
	}

	multi::JobNode* JobNode::run()
	{
		multi::JobNode* next = nullptr;
		assert(m_numChildren >= 0);

		// Run Node
		State noState = State::NONE;
		if (m_state.compare_exchange_strong(noState, State::RUNNING))
		{
			if (m_func)
				m_func();
			m_func = nullptr;
			m_state =State::WAITING;
		}

		// Complete Node when all children complete
		State waitState = State::WAITING;
		if (m_numChildren == 0 && m_state.compare_exchange_strong(waitState, State::COMPLETE))
		{
			if (m_parent)
				m_parent->m_numChildren--;
			next = (m_next != nullptr) ? m_next : m_parent;
			delete this;
		}

		return next;
	}
} // namespace multi