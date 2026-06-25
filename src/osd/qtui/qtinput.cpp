// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtinput.cpp - Qt-native OSD input bus implementation (Qt-free)
//
//============================================================

#include "qtinput.h"

namespace osd::qtui {

QtInputBus &QtInputBus::instance()
{
	static QtInputBus bus;
	return bus;
}

void QtInputBus::pushKeyboard(const QtInputEvent &e)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_keyboard.push_back(e);
	// bound the queue so a paused/closed worker can't grow it without limit
	while (m_keyboard.size() > 256)
		m_keyboard.pop_front();
}

void QtInputBus::pushMouse(const QtInputEvent &e)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_mouse.push_back(e);
	m_lightgun.push_back(e);   // both modules consume mouse events independently
	while (m_mouse.size() > 256)
		m_mouse.pop_front();
	while (m_lightgun.size() > 256)
		m_lightgun.pop_front();
}

std::vector<QtInputEvent> QtInputBus::drain(std::deque<QtInputEvent> &q)
{
	std::vector<QtInputEvent> out(q.begin(), q.end());
	q.clear();
	return out;
}

std::vector<QtInputEvent> QtInputBus::takeKeyboard()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return drain(m_keyboard);
}

std::vector<QtInputEvent> QtInputBus::takeMouse()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return drain(m_mouse);
}

std::vector<QtInputEvent> QtInputBus::takeLightgun()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return drain(m_lightgun);
}

void QtInputBus::clear()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_keyboard.clear();
	m_mouse.clear();
	m_lightgun.clear();
}

} // namespace osd::qtui
