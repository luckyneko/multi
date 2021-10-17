#include "multi/details/job.h"

#include <cassert>
#include <thread>

namespace multi
{
	Job::Job()
		: m_inner(nullptr)
	{
	}

	Job::Job(std::vector<Task>&& tasks)
		: m_inner(new Inner)
	{
		m_inner->tasks = std::move(tasks);
		m_inner->next = 0;
		m_inner->unfinished = m_inner->tasks.size();
		m_inner->ref = 1;
	}

	Job::Job(const Job& cpy)
	{
		m_inner = cpy.m_inner;
		if (valid())
			m_inner->ref++;
	}

	Job::~Job()
	{
		reset();
	}

	void Job::reset()
	{
		if (valid())
		{
			if (--m_inner->ref == 0)
				delete m_inner;
			m_inner = nullptr;
		}
	}

	bool Job::tryRun()
	{
		if (!valid() || !hasWork())
			return false;

		size_t taskIdx = m_inner->next++;
		if (taskIdx >= m_inner->tasks.size())
			return false;

		m_inner->tasks[taskIdx]();
		--m_inner->unfinished;
		return true;
	}

	void Job::waitRun()
	{
		// Run
		while (tryRun())
			continue;

		// Wait
		while (!complete())
			std::this_thread::yield();
	}

	Job& Job::operator=(const Job& cpy)
	{
		reset();
		m_inner = cpy.m_inner;
		if (valid())
			m_inner->ref++;
		return *this;
	}
} // namespace multi