#include "multi/job.h"

#include "multi/details/jobnode.h"
#include "multi/details/localjobqueue.h"

namespace multi
{
	Job::Job(iContext* context, JobNode* parent)
		: m_context(context)
		, m_parent(parent)
	{
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
		m_parent->setNext(m_context->allocJobNode(m_parent->getParent(), std::move(task)));
	}
} // namespace multi