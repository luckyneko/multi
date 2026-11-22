/*
 *  Created by LuckyNeko on 22/11/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "multi/details/waitbackoff.h"

#include "multi/details/platform.h"

#include <algorithm>
#include <chrono>

namespace multi::details
{
	void WaitBackoff::checkSpinTime()
	{
		// The streak's clock starts at its first check, WAIT_CLOCK_INTERVAL
		// attempts in, so a short wait never reads the clock at all.
		const auto now = std::chrono::steady_clock::now();
		if (m_idlePolls == WAIT_CLOCK_INTERVAL)
			m_idleSince = now;
		else if (now - m_idleSince >= WAIT_SPIN_TIME)
			m_sleep = WAIT_SLEEP_MIN;
	}

	void WaitBackoff::sleep()
	{
		preciseSleep(m_sleep);
		m_sleep = std::min(m_sleep * 2, std::chrono::microseconds(WAIT_SLEEP_MAX));
	}
} // namespace multi::details
