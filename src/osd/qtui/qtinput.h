// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtinput.h - Qt-native OSD input bus (Qt-free, MAME-free)
//
//  Phase 13b: a thread-safe hand-off of input from the Qt GUI thread (which
//  receives QKeyEvent/QMouseEvent on the render QWindow) to the Qt-native
//  input modules running on the emulation worker thread.  Deliberately free of
//  both Qt and MAME headers (events are plain ints) so it can be included by
//  the Qt front-end AND by the MAME-side input module without dragging either
//  world's headers across the boundary.
//
//  Only one game runs embedded at a time, so a process-global singleton bus is
//  sufficient.  Per-category queues (keyboard / mouse / lightgun) keep each
//  module a single consumer of its own events.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTINPUT_H
#define MAME_OSD_QTUI_QTINPUT_H

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace osd::qtui {

enum class QtInputType
{
	KeyPress,
	KeyRelease,
	Char,           // committed text codepoint in `codepoint` (natural keyboard / UI)
	FocusGained,    // render window gained keyboard focus
	FocusLost,      // render window lost keyboard focus
	MouseMove,      // x,y are absolute within the surface; also carries relative dx,dy
	MouseButton,    // button index in `button`, pressed in `value`
	MouseWheel,     // wheel delta in `value`
};

struct QtInputEvent
{
	QtInputType   type;
	int           key = 0;             // Qt::Key value (as int) for key events
	unsigned      nativeScanCode = 0;  // platform scancode (positional mapping, future)
	unsigned      modifiers = 0;       // Qt::KeyboardModifiers (as int)
	std::uint32_t codepoint = 0;       // committed text codepoint (natural keyboard)
	int           x = 0, y = 0;        // absolute pointer position within the surface
	int           dx = 0, dy = 0;      // relative pointer motion
	int           surfaceW = 0, surfaceH = 0;  // surface size (for absolute/lightgun normalization)
	int           button = 0;          // button index (mouse)
	int           value = 0;           // press/release (1/0) or wheel delta
};

class QtInputBus
{
public:
	static QtInputBus &instance();

	// GUI thread → bus
	void pushKeyboard(const QtInputEvent &e);
	void pushMouse(const QtInputEvent &e);     // duplicated into mouse + lightgun queues
	void setFocused(bool f) { m_focused.store(f, std::memory_order_relaxed); }

	// worker thread (input modules) ← bus
	std::vector<QtInputEvent> takeKeyboard();
	std::vector<QtInputEvent> takeMouse();
	std::vector<QtInputEvent> takeLightgun();
	bool focused() const { return m_focused.load(std::memory_order_relaxed); }

	// drop any queued events (call when a run starts/stops)
	void clear();

private:
	QtInputBus() = default;

	static std::vector<QtInputEvent> drain(std::deque<QtInputEvent> &q);

	std::mutex                m_mutex;
	std::deque<QtInputEvent>  m_keyboard;
	std::deque<QtInputEvent>  m_mouse;
	std::deque<QtInputEvent>  m_lightgun;
	std::atomic<bool>         m_focused{ false };
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTINPUT_H
