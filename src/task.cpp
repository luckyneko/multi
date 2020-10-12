#include "multi/task.h"

#include "multi/job.h"

namespace multi
{
	void Task::run()
	{
		Job jb;
		run(jb);
	}

	void Task::run(Job jb)
	{
		if (m_func)
			m_func(std::move(jb));
		m_func = nullptr;
	}
} // namespace multi