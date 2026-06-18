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
#include <vector>


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
	MountImage,       // sval = device brief name, sval2 = file path
	UnloadImage,      // sval = device brief name
	SetSlot,          // sval = slot name, sval2 = option value ("" = none); triggers hard reset
	Exit
};

struct EmbedAction
{
	EmbedCommand cmd;
	double       dval = 0.0;
	int          ival = 0;
	std::string  sval;
	std::string  sval2;
};

// A mountable image device on the live machine, published by the emulation
// thread so the GUI can build the Media menu without touching running_machine.
struct EmbedImage
{
	std::string brief;      // brief_instance_name(), e.g. "flop1" — command key
	std::string label;      // human label, e.g. "Floppy Disk 1 [flop1]"
	std::string filename;   // current image basename, "" if empty
	bool        loaded = false;
};

// A user-configurable device slot on the live machine.  Changing the selected
// option requires a reconfigure (hard reset), so this snapshot is published at
// machine init (slots don't change mid-run).
struct EmbedSlot
{
	std::string name;                  // slot_name(), the emu_options key — command key
	std::string current;               // currently selected option value ("" = none)
	std::string defaultOption;         // the slot's default option name
	std::vector<std::string> options;  // selectable option values (excluding "(none)")
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

	// Image-device snapshot published by the emulation thread (publishImages)
	// and read by the GUI (imagesSnapshot) to build the Media menu.  The
	// generation counter lets the UI tell when it changed.
	void publishImages(std::vector<EmbedImage> images)
	{
		std::lock_guard<std::mutex> lk(m_imgMutex);
		m_images = std::move(images);
		m_imagesGen.fetch_add(1, std::memory_order_relaxed);
	}

	std::vector<EmbedImage> imagesSnapshot() const
	{
		std::lock_guard<std::mutex> lk(m_imgMutex);
		return m_images;
	}

	unsigned imagesGeneration() const { return m_imagesGen.load(std::memory_order_relaxed); }

	// Slot-device snapshot (published at machine init, read by the GUI's Slots menu).
	void publishSlots(std::vector<EmbedSlot> list)
	{
		std::lock_guard<std::mutex> lk(m_slotMutex);
		m_slots = std::move(list);
	}

	std::vector<EmbedSlot> slotsSnapshot() const
	{
		std::lock_guard<std::mutex> lk(m_slotMutex);
		return m_slots;
	}

	// Status published by the emulation thread for the UI to read.
	std::atomic<bool> running{false};
	std::atomic<bool> paused{false};

private:
	std::mutex m_mutex;
	std::deque<EmbedAction> m_queue;

	mutable std::mutex m_imgMutex;
	std::vector<EmbedImage> m_images;
	std::atomic<unsigned> m_imagesGen{0};

	mutable std::mutex m_slotMutex;
	std::vector<EmbedSlot> m_slots;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_EMBEDSESSION_H
