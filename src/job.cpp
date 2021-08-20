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

	JobNode* Job::attachJobNode(JobNode* previous, JobNode* next)
	{
		if(previous)
			previous->setNext(next);
		return next;
	}

	void Job::addUntil(std::function<bool()> pred, std::function<void(Job)> func)
	{
		Task task = [pred, func](Job jb)
		{
			if(!pred())
			{
				func(jb);
				jb.addUntil(pred, func);
			}
		};
		m_parent->setNext(new JobNode(m_parent->getParent(), std::move(task)));
	}

	void Job::queueJobNode(JobNode* node)
	{
		assert(m_queue);
		m_queue->queueJobNode(node);
	}
} // namespace multi