#include "multi/task.h"

#include "multi/details/localjobqueue.h"
#include "multi/job.h"

namespace multi
{
	void Task::run()
	{
		LocalJobQueue jobQueue;
		run(Job(&jobQueue));
		jobQueue.run();
	}

	void Task::run(Job jb)
	{
		if (m_func)
			m_func(std::move(jb));
		m_func = nullptr;
	}
} // namespace multi