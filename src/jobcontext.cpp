#include "multi/jobcontext.h"

#include "multi/context.h"
#include "multi/details/jobnode.h"

namespace multi
{
	JobContext::JobContext(Context* context, JobNode* parent)
		: m_context(context)
		, m_parent(parent)
	{
	}

	void JobContext::add(Job&& job)
	{
		queueJobNode(allocJobNode(job.popTask()));
	}

	JobNode* JobContext::allocJobNode(Task&& task, JobNode* next)
	{
		return new JobNode(m_parent, std::move(task), next);
	}

	void JobContext::queueJobNode(JobNode* node)
	{
		if (m_context)
			m_context->queueJobNode(node);
		else
			node->run();
	}
} // namespace multi