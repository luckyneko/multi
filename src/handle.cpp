#include "multi/handle.h"

#include "multi/context.h"

namespace multi
{
	Handle::Handle(std::future<void>&& hdl, Context* ctx)
		: m_handle(std::move(hdl))
		, m_context(ctx)
	{
	}

	Handle::Handle(Handle&& a)
	{
		m_handle = std::move(a.m_handle);
		m_context = a.m_context;
		a.clear();
	}

	Handle::~Handle()
	{
		try
		{
			wait();
		}
		catch (...)
		{
			// A destructor must not throw. Explicit wait() surfaces exceptions.
		}
	}

	void Handle::wait()
	{
		if (!valid())
			return;

		if (m_context != nullptr)
		{
			while (m_handle.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
			{
				if (!m_context->tryRunSteal())
					std::this_thread::yield();
			}
		}

		// get() both waits (when m_context is null and the steal loop was
		// skipped) and rethrows any exception stored by the task's wrapper.
		// Idempotent on shared_future — repeated waits surface the same error.
		m_handle.get();
	}

	Handle& Handle::operator=(Handle&& a)
	{
		if (this == &a)
			return *this;

		// Preserve the RAII-wait invariant: the current handle's task must
		// complete before we drop it, otherwise `h = async(a); h = async(b);`
		// would silently discard a's wait. Swallow exceptions to match ~Handle;
		// callers that need to observe failures must wait() explicitly first.
		try
		{
			wait();
		}
		catch (...)
		{
		}

		m_handle = std::move(a.m_handle);
		m_context = a.m_context;
		a.clear();
		return *this;
	}
} // namespace multi
