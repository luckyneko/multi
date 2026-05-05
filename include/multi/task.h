/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#ifndef _MULTI_TASK_H_
#define _MULTI_TASK_H_

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace multi
{
	/*
	 * Task
	 * Move-only type-erased callable for void(). Uses a 24-byte inline buffer
	 * (SBO) to avoid heap allocation for small lambdas; falls back to a single
	 * heap allocation for larger functors.
	 *
	 * All lambda captures in the library fit within the SBO:
	 *   [&job, i]                        16 B  (ref + size_t)
	 *   [&func, item]                    16 B  (ref + ptr)
	 *   [innerBegin, innerEnd, &func]    24 B  (ptr + ptr + ref)
	 *   [state = shared_ptr]             16 B  (shared_ptr)
	 *   [innerBegin, innerEnd, step, &func] with int IDX: 20 B — SBO
	 *                                    with int64_t IDX: 32 B — heap path
	 */
	class Task
	{
		struct Vtable
		{
			void (*invoke)(void* buf);
			void (*destroy)(void* buf);
			void (*move_to)(void* src, void* dst);
		};

		template <class F>
		struct SboOps
		{
			static void invoke(void* buf) { (*static_cast<F*>(buf))(); }
			static void destroy(void* buf) { static_cast<F*>(buf)->~F(); }
			static void move_to(void* src, void* dst)
			{
				new (dst) F(std::move(*static_cast<F*>(src)));
				static_cast<F*>(src)->~F();
			}
			static constexpr Vtable vtable = {invoke, destroy, move_to};
		};

		template <class F>
		struct HeapOps
		{
			static F* load(const void* buf) noexcept
			{
				F* p;
				std::memcpy(&p, buf, sizeof(F*));
				return p;
			}
			static void invoke(void* buf) { (*load(buf))(); }
			static void destroy(void* buf) { delete load(buf); }
			static void move_to(void* src, void* dst)
			{
				std::memcpy(dst, src, sizeof(F*));
			}
			static constexpr Vtable vtable = {invoke, destroy, move_to};
		};

		static constexpr std::size_t SBO_SIZE  = 24;
		static constexpr std::size_t SBO_ALIGN = alignof(void*);

		template <class F>
		static constexpr bool fits_sbo = sizeof(F) <= SBO_SIZE && alignof(F) <= SBO_ALIGN;

		alignas(SBO_ALIGN) unsigned char m_buf[SBO_SIZE];
		const Vtable* m_vtable = nullptr;

	public:
		Task() = default;

		~Task()
		{
			if (m_vtable)
				m_vtable->destroy(m_buf);
		}

		Task(const Task&)            = delete;
		Task& operator=(const Task&) = delete;

		Task(Task&& other) noexcept
		{
			if (other.m_vtable)
			{
				other.m_vtable->move_to(other.m_buf, m_buf);
				m_vtable       = other.m_vtable;
				other.m_vtable = nullptr;
			}
		}

		Task& operator=(Task&& other) noexcept
		{
			if (this != &other)
			{
				if (m_vtable)
					m_vtable->destroy(m_buf);
				m_vtable = nullptr;
				if (other.m_vtable)
				{
					other.m_vtable->move_to(other.m_buf, m_buf);
					m_vtable       = other.m_vtable;
					other.m_vtable = nullptr;
				}
			}
			return *this;
		}

		template <class F,
		          std::enable_if_t<
		              !std::is_same_v<std::decay_t<F>, Task> &&
		              std::is_invocable_v<std::decay_t<F>>,
		          int> = 0>
		Task(F&& f)
		{
			using FD = std::decay_t<F>;
			if constexpr (fits_sbo<FD>)
			{
				new (m_buf) FD(std::forward<F>(f));
				m_vtable = &SboOps<FD>::vtable;
			}
			else
			{
				FD* p = new FD(std::forward<F>(f));
				std::memcpy(m_buf, &p, sizeof(FD*));
				m_vtable = &HeapOps<FD>::vtable;
			}
		}

		void operator()() { m_vtable->invoke(m_buf); }

		explicit operator bool() const noexcept { return m_vtable != nullptr; }
	};

} // namespace multi

#endif // _MULTI_TASK_H_
