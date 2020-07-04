#include "multi/task.h"

#include "multi/jobcontext.h"

namespace multi
{
	Task::~Task()
	{
		JobContext jc;
		run(jc);
	}

	void Task::run(JobContext& jc)
	{
		if (m_func)
			m_func(jc);
		m_func = nullptr;
	}
} // namespace multi