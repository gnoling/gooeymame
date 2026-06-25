// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtmonitors.cpp - Qt-native monitor snapshot store (Qt-free)
//
//============================================================

#include "qtmonitors.h"

#include <mutex>

namespace osd::qtui {

namespace {

std::mutex g_mutex;
std::vector<QtMonitorRect> g_monitors;

} // anonymous namespace

void qtui_set_monitors(std::vector<QtMonitorRect> mons)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_monitors = std::move(mons);
}

std::vector<QtMonitorRect> qtui_get_monitors()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_monitors;
}

} // namespace osd::qtui
