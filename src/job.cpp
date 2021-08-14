#include "multi/job.h"

#include "multi/details/jobnode.h"
#include "multi/details/jobqueue.h"

namespace multi
{
	Job::Job(iJobQueue* queue, JobNode* parent)
		: m_queue(queue)
		, m_parent(parent)
	{
	}

	JobNode* Job::allocJobNode(Task&& task, JobNode* next)
	{
		return new JobNode(m_parent, std::move(task), next);
	}

	void Job::queueJobNode(JobNode* node)
	{
		assert(m_queue);
		m_queue->queueJobNode(node);
	}
} // namespace multi