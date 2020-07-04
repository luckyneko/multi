#include "multi/task.h"

#include "multi/job.h"

namespace multi
{
	Task::~Task()
	{
		Job jb;
		run(jb);
	}

	void Task::run(Job& jb)
	{
		if (m_func)
			m_func(jb);
		m_func = nullptr;
	}
} // namespace multi