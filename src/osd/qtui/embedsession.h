// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  embedsession.h - bridge between the Qt UI and an in-process embedded run
//
//  The Qt menu/toolbar posts high-level commands from the GUI thread; the
//  qtui_osd_interface drains and applies them from the emulation thread (the
//  only thread that may touch running_machine).  Deliberately Qt-free so it
//  can be included by both the Qt UI (mainwindow.cpp) and the emulation
//  backend (emulator.cpp).
//
//============================================================
#ifndef MAME_OSD_QTUI_EMBEDSESSION_H
#define MAME_OSD_QTUI_EMBEDSESSION_H

#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>


namespace osd::qtui {

// In-emulation actions, mirroring NEWUI's menu (newuires.h).  Each maps to a
// running_machine / emu_options call applied on the emulation thread.
enum class EmbedCommand
{
	TogglePause,
	SoftReset,
	HardReset,
	SaveState,        // sval = filename ("" = default slot)
	LoadState,        // sval = filename ("" = default slot)
	SaveSnapshot,
	ToggleFullscreen,
	ToggleFps,
	SetThrottleRate,  // dval = speed multiplier (1.0 = 100%)
	ToggleThrottle,
	SetFrameskip,     // ival = level, or -1 for auto
	SetRotate,        // ival = 0/90/180/270
	KeyboardEmulated,
	KeyboardNatural,
	Paste,
	Exit
};

struct EmbedAction
{
	EmbedCommand cmd;
	double       dval = 0.0;
	int          ival = 0;
	std::string  sval;
};

//============================================================
//  EmbedSession - thread-safe command queue + published status.
//============================================================
class EmbedSession
{
public:
	// Posted from the GUI thread.
	void post(const EmbedAction &action)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_queue.push_back(action);
	}

	// Drained from the emulation thread; returns false when empty.
	bool take(EmbedAction &out)
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		if (m_queue.empty())
			return false;
		out = m_queue.front();
		m_queue.pop_front();
		return true;
	}

	// Status published by the emulation thread for the UI to read.
	std::atomic<bool> running{false};
	std::atomic<bool> paused{false};

private:
	std::mutex m_mutex;
	std::deque<EmbedAction> m_queue;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_EMBEDSESSION_H
