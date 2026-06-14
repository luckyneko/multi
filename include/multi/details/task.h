/*
 *  Created by LuckyNeko on 02/10/2021.
 *  Copyright 2021 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace multi::details
{
	/*
	 * Task
	 * A move-only std::function<void()> — move-only because it stores move-only
	 * job state (e.g. std::promise). Small functors live in a 24-byte inline
	 * buffer (SBO, no heap); larger or throwing-move functors fall back to a
	 * single heap allocation. 24 B is sized to fit the library's own captures,
	 * the largest being [innerBegin, innerEnd, &func].
	 */
	class Task
	{
		// Hand-rolled dispatch table for the type-erased functor. Using this
		// instead of a virtual base keeps m_buf holding only the functor — no
		// embedded vptr — so the SBO budget isn't spent on type info.
		struct Vtable
		{
			void (*invoke)(void* buf);			   // call the functor
			void (*destroy)(void* buf);			   // destroy (and free, if heap) the functor
			void (*moveTo)(void* src, void* dst); // relocate functor src -> dst; src buffer left dead
		};

		// Functor stored inline in the buffer. moveTo move-constructs it into
		// dst then destroys the source object.
		template <class F>
		struct SboOps
		{
			static void invoke(void* buf) { (*static_cast<F*>(buf))(); }
			static void destroy(void* buf) { static_cast<F*>(buf)->~F(); }
			static void moveTo(void* src, void* dst)
			{
				new (dst) F(std::move(*static_cast<F*>(src)));
				static_cast<F*>(src)->~F();
			}
			static constexpr Vtable vtable = {invoke, destroy, moveTo};
		};

		// Functor stored on the heap; the buffer holds a pointer to it. moveTo
		// just copies that pointer — ownership transfers and the caller clears
		// the source vtable, so destroy never runs on the dead source.
		template <class F>
		struct HeapOps
		{
			// Read the heap pointer back out of the raw buffer. memcpy avoids
			// the alignment / strict-aliasing pitfalls of reinterpret_cast.
			static F* load(const void* buf) noexcept
			{
				F* p;
				std::memcpy(&p, buf, sizeof(F*));
				return p;
			}
			static void invoke(void* buf) { (*load(buf))(); }
			static void destroy(void* buf) { delete load(buf); }
			static void moveTo(void* src, void* dst)
			{
				std::memcpy(dst, src, sizeof(F*));
			}
			static constexpr Vtable vtable = {invoke, destroy, moveTo};
		};

		static constexpr std::size_t SBO_SIZE = 24;
		static constexpr std::size_t SBO_ALIGN = alignof(void*); // buffer must also hold a heap pointer

		// Inline in the buffer iff it fits and can't throw while we relocate it:
		// Task's move ops are noexcept, so a throwing-move functor must heap-
		// allocate (where relocation is just a noexcept pointer copy).
		template <class F>
		static constexpr bool fitsSbo = sizeof(F) <= SBO_SIZE &&
										 alignof(F) <= SBO_ALIGN &&
										 std::is_nothrow_move_constructible_v<F>;

	public:
		Task() = default;
		~Task() { reset(); }
		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;
		Task(Task&& other) noexcept { adopt(std::move(other)); }

		Task& operator=(Task&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				adopt(std::move(other));
			}
			return *this;
		}

		// Construct from any invocable that isn't a Task. The constraint stops
		// this from shadowing the copy/move constructors.
		template <class F, std::enable_if_t<
							   !std::is_same_v<std::decay_t<F>, Task> &&
								   std::is_invocable_v<std::decay_t<F>>,
							   int> = 0>
		Task(F&& f)
		{
			using FD = std::decay_t<F>;
			if constexpr (fitsSbo<FD>)
			{
				new (m_buf) FD(std::forward<F>(f)); // construct inline
				m_vtable = &SboOps<FD>::vtable;
			}
			else
			{
				FD* p = new FD(std::forward<F>(f));
				std::memcpy(m_buf, &p, sizeof(FD*)); // stash the pointer (read back by HeapOps::load)
				m_vtable = &HeapOps<FD>::vtable;
			}
		}

		// Precondition: non-empty. Invokes the stored functor.
		void operator()() { m_vtable->invoke(m_buf); }

		explicit operator bool() const noexcept { return m_vtable != nullptr; }

	private:
		// Destroy any held functor and become empty.
		void reset() noexcept
		{
			if (m_vtable)
				m_vtable->destroy(m_buf);
			m_vtable = nullptr;
		}

		// Relocate other's functor into *this, leaving other empty. Assumes
		// *this is already empty (caller resets first).
		void adopt(Task&& other) noexcept
		{
			if (other.m_vtable)
			{
				other.m_vtable->moveTo(other.m_buf, m_buf);
				m_vtable = other.m_vtable;
				other.m_vtable = nullptr;
			}
		}

	private:
		alignas(SBO_ALIGN) unsigned char m_buf[SBO_SIZE]; // functor (inline) or pointer to it (heap)
		const Vtable* m_vtable = nullptr;				  // null == empty
	};

} // namespace multi::details
