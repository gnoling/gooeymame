// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  threadutil.cpp - low-priority background thread helper
//
//  Deliberately includes NO MAME or Qt headers so the platform system
//  headers (notably <windows.h>) can't collide with them.
//
//============================================================

#include "threadutil.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <sys/resource.h>   // setpriority, PRIO_PROCESS
#include <sys/syscall.h>    // SYS_gettid, SYS_ioprio_set
#include <unistd.h>         // syscall
#endif

namespace osd::qtui {

void lower_current_thread_priority()
{
#if defined(_WIN32)
	// Background mode lowers this thread's scheduling priority AND its IO/memory
	// priority until it ends (Vista+); ideal for a transient worker thread.
	SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#elif defined(__linux__)
	// Linux nice values are per-thread, so this only nudges the worker down.
	pid_t const tid = static_cast<pid_t>(::syscall(SYS_gettid));
	::setpriority(PRIO_PROCESS, tid, 10);
#if defined(SYS_ioprio_set)
	// Idle IO class so the worker's disk/network IO yields to GUI-side IO.
	// IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE = 3, 0) == (3 << 13).
	enum { IOPRIO_WHO_PROCESS = 1, IOPRIO_CLASS_IDLE = 3, IOPRIO_CLASS_SHIFT = 13 };
	::syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, tid,
			(IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT));
#endif
#endif
}

} // namespace osd::qtui
