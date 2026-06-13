/*
 *  Created by LuckyNeko on 12/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

namespace multi
{
	template <class T>
	Handle<T>::Handle(std::future<T>&& hdl)
		: m_handle(std::move(hdl))
	{
	}

	template <class T>
	Handle<T>::Handle(Handle&& a) noexcept
		: m_handle(std::move(a.m_handle))
	{
		a.m_handle = std::shared_future<T>();
	}

	template <class T>
	bool Handle<T>::complete() const
	{
		return !valid() || m_handle.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	template <class T>
	bool Handle<T>::valid() const
	{
		return m_handle.valid();
	}

	template <class T>
	void Handle<T>::wait() const
	{
		if (m_handle.valid())
			m_handle.wait();
	}

	template <class T>
	bool Handle<T>::get(T* value)
	{
		if (!complete())
			return false;
		*value = m_handle.get();
		return true;
	}

} // namespace multi
