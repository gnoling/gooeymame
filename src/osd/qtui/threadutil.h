// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  threadutil.h - small thread helpers for qtui background work
//
//============================================================
#ifndef MAME_OSD_QTUI_THREADUTIL_H
#define MAME_OSD_QTUI_THREADUTIL_H

#pragma once

namespace osd::qtui {

// Drop the CALLING thread to low CPU *and* IO priority, so long background
// sweeps (ROM/software audits, the screenless scan) yield to the GUI thread and
// its art/icon/thumbnail loaders — important on slow network mounts (Samba/NFS).
// Call once at the top of the worker.  Implemented per-platform in threadutil.cpp
// with no MAME/Qt headers (Windows background mode / Linux nice + ioprio idle).
void lower_current_thread_priority();

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_THREADUTIL_H
