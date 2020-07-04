#include "multi/job.h"

#include "multi/context.h"
#include "multi/details/jobnode.h"

namespace multi
{
	Job::Job(Context* context, JobNode* parent)
		: m_context(context)
		, m_parent(parent)
	{
	}

	JobNode* Job::allocJobNode(Task&& task, JobNode* next)
	{
		return new JobNode(m_parent, std::move(task), next);
	}

	void Job::queueJobNode(JobNode* node)
	{
		if (m_context)
			m_context->queueJobNode(node);
		else
			node->run();
	}
} // namespace multi