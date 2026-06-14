/*
 *  Created by LuckyNeko on 16/03/2020.
 *  Copyright 2020 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#pragma once

namespace multi
{
	inline void start(int threadCount)
	{
		context().start(threadCount);
	}

	inline void stop()
	{
		context().stop();
	}

	inline size_t threadCount()
	{
		return context().threadCount();
	}

	template <class F>
	auto async(F&& f)
	{
		return context().async(std::forward<F>(f));
	}

	template <typename F0, typename F1, typename... Fs>
	auto async(F0&& f0, F1&& f1, Fs&&... fs)
	{
		return context().async(std::forward<F0>(f0), std::forward<F1>(f1),
							   std::forward<Fs>(fs)...);
	}

	template <typename... TASKS>
	void parallel(TASKS&&... tasks)
	{
		context().parallel(std::forward<TASKS>(tasks)...);
	}

	template <typename ITER, typename FUNC>
	void each(ITER begin, ITER end, FUNC&& func)
	{
		context().each(begin, end, std::forward<FUNC>(func));
	}
	template <typename ITER, typename FUNC>
	void each(ChunkPolicy chunkPolicy, ITER begin, ITER end, FUNC&& func)
	{
		context().each(chunkPolicy, begin, end, std::forward<FUNC>(func));
	}

	template <typename CONTAINER, typename FUNC>
	void each(CONTAINER&& c, FUNC&& func)
	{
		context().each(std::forward<CONTAINER>(c), std::forward<FUNC>(func));
	}

	template <typename CONTAINER, typename FUNC>
	void each(ChunkPolicy chunkPolicy, CONTAINER&& c, FUNC&& func)
	{
		context().each(chunkPolicy, std::forward<CONTAINER>(c), std::forward<FUNC>(func));
	}

	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, FUNC&& func)
	{
		context().range(begin, end, std::forward<FUNC>(func));
	}

	template <typename IDX, typename FUNC>
	void range(IDX begin, IDX end, IDX step, FUNC&& func)
	{
		context().range(begin, end, step, std::forward<FUNC>(func));
	}
	template <typename IDX, typename FUNC>
	void range(ChunkPolicy chunkPolicy, IDX begin, IDX end, IDX step, FUNC&& func)
	{
		context().range(chunkPolicy, begin, end, step, std::forward<FUNC>(func));
	}

	template <class... Hs>
	void waitAll(const Hs&... hs)
	{
		context().waitAll(hs...);
	}
	template <class... Ts>
	void waitAll(const std::tuple<Handle<Ts>...>& tup)
	{
		context().waitAll(tup);
	}

	template <class... Hs>
	std::size_t waitAny(const Hs&... hs)
	{
		return context().waitAny(hs...);
	}
	template <class... Ts>
	std::size_t waitAny(const std::tuple<Handle<Ts>...>& tup)
	{
		return context().waitAny(tup);
	}

	template <class Pred>
	void waitUntil(Pred&& pred)
	{
		context().waitUntil(std::forward<Pred>(pred));
	}

} // namespace multi
