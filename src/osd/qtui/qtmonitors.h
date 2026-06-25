// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtmonitors.h - Qt-native monitor snapshot (Qt-free)
//
//  QScreen geometry must be read on the Qt GUI thread, but the MAME monitor
//  module (monitor_qt) initialises on the emulation worker thread.  The GUI
//  thread captures the screen rectangles into this Qt-free store before a run
//  starts; the worker-thread module reads them back, avoiding any off-thread
//  QScreen access.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTMONITORS_H
#define MAME_OSD_QTUI_QTMONITORS_H

#pragma once

#include <vector>

namespace osd::qtui {

struct QtMonitorRect
{
	int  x = 0, y = 0, w = 0, h = 0;   // desktop geometry (logical pixels)
	bool primary = false;
};

// GUI thread → store (called before launching the emulation worker).
void qtui_set_monitors(std::vector<QtMonitorRect> mons);

// worker thread (monitor_qt) ← store.
std::vector<QtMonitorRect> qtui_get_monitors();

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTMONITORS_H
