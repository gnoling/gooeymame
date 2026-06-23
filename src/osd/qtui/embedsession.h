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
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
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
	SetField,         // sval = port tag, mask = field mask, value = chosen setting value (DIP/config)
	SetView,          // ival = render view index
	SetVisibility,    // ival = visibility-toggle index, value = 1 enable / 0 disable (bezel/artwork)
	SetFilter,        // ival = 1 smooth (bilinear) / 0 sharp (nearest-neighbour) screen scaling
	SetKeepAspect,    // ival = 1 maintain aspect ratio / 0 stretch
	SetScaleMode,     // ival = SCALE_FRACTIONAL/_X/_Y/_AUTO/INTEGER (render.h)
	SetZoomToScreen,  // ival = 1 zoom screen area to fill view / 0 off
	SetSlider,        // ival = slider index (publish order), dval = new value
	Exit
};

struct EmbedAction
{
	EmbedCommand  cmd;
	double        dval = 0.0;
	int           ival = 0;
	std::string   sval;
	std::string   sval2;
	std::uint32_t mask = 0;    // SetField: ioport_field mask (identifies the field within a port)
	std::uint32_t value = 0;   // SetField: ioport_setting value to apply
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

// A live, settable per-machine setting: a DIP-switch bank or a machine
// configuration field (IPT_DIPSWITCH / IPT_CONFIG).  Published by the emulation
// thread; the GUI builds dynamic submenus from it.  These vary by machine — the
// "different games have different options" surface.
struct EmbedSetting
{
	bool        config = false;   // false = DIP switch, true = machine configuration
	std::string portTag;          // owning port tag — command key (with mask)
	std::uint32_t mask = 0;        // field mask within the port — command key
	std::string name;             // field display name
	std::string current;          // current setting's display name
	// Selectable settings: (value, display name), enabled ones only.
	std::vector<std::pair<std::uint32_t, std::string>> options;
};

// The running machine's render views and the current view's artwork-visibility
// toggles (bezel/overlay/backdrop/cpanel or named layout collections).
// Published by the emulation thread for the GUI's Video menu.
struct EmbedVideo
{
	std::vector<std::string> views;     // selectable view names
	int currentView = -1;               // index into views
	struct Toggle { std::string name; bool enabled = false; };
	std::vector<Toggle> toggles;        // current view's visibility toggles (index = command key)
	bool smooth = true;                 // screen scaling: true = bilinear (blurry), false = nearest (sharp)
	bool keepaspect = true;             // maintain the source aspect ratio
	int  scaleMode = 0;                 // SCALE_FRACTIONAL/_X/_Y/_AUTO/INTEGER (render.h)
	bool zoomToScreen = false;          // zoom so the screen area fills the view (artwork views)
	bool zoomAvailable = false;         // whether zoom-to-screen is meaningful (view has artwork)
};

// A live adjustment slider (brightness / contrast / gamma / volume / speed /
// refresh / beam / …), enumerated from the UI + OSD slider lists.  The index in
// the published vector is the command key: the emulation thread re-fetches the
// same combined list in the same order to apply a change.
struct EmbedSlider
{
	std::string description;   // e.g. "Brightness", "Master Volume"
	std::string text;          // MAME-formatted current value (e.g. "1.00", "0%")
	int minval = 0;
	int defval = 0;
	int maxval = 0;
	int incval = 1;
	int current = 0;
};

// Capability flags describing what the running machine actually supports, so the
// GUI can show ONLY the menus relevant to it (hidden, not disabled).  Recomputed
// on the emulation thread; the generation counter lets the GUI detect change
// cheaply (a cart load can add image/slot capability mid-run).  Mirrors the
// predicates MAME's own menu_main::populate() uses to show/hide Tab-menu entries.
struct EmbedCaps
{
	bool hasDips = false;            // any enabled IPT_DIPSWITCH field with settings
	bool hasConfigs = false;         // any enabled IPT_CONFIG field with settings
	bool hasBios = false;            // a selectable system/device BIOS
	bool hasSlots = false;           // any device slot with selectable options
	bool hasImages = false;          // any user-loadable image device
	bool hasTape = false;            // a cassette device
	bool hasNetwork = false;         // a network interface
	bool hasBarcode = false;         // a barcode reader
	bool hasCrosshair = false;       // crosshair in use
	bool hasSound = false;           // sound enabled
	bool hasNaturalKeyboard = false; // natural-keyboard capable (Paste / Emulated-Natural)
	bool cheatEnabled = false;       // -cheat active
	bool multiView = false;          // more than one render view
	std::string swList;              // running software-list short name ("" if none)
	std::string swShort;             // running software item short name ("" if none)

	bool operator==(const EmbedCaps &o) const
	{
		return hasDips == o.hasDips && hasConfigs == o.hasConfigs && hasBios == o.hasBios
			&& hasSlots == o.hasSlots && hasImages == o.hasImages && hasTape == o.hasTape
			&& hasNetwork == o.hasNetwork && hasBarcode == o.hasBarcode
			&& hasCrosshair == o.hasCrosshair && hasSound == o.hasSound
			&& hasNaturalKeyboard == o.hasNaturalKeyboard && cheatEnabled == o.cheatEnabled
			&& multiView == o.multiView && swList == o.swList && swShort == o.swShort;
	}
	bool operator!=(const EmbedCaps &o) const { return !(*this == o); }
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

	// Settings snapshot (DIP/config), republished as values change; the
	// generation counter lets the UI tell when it changed.
	void publishSettings(std::vector<EmbedSetting> list)
	{
		std::lock_guard<std::mutex> lk(m_settingMutex);
		m_settings = std::move(list);
		m_settingsGen.fetch_add(1, std::memory_order_relaxed);
	}

	std::vector<EmbedSetting> settingsSnapshot() const
	{
		std::lock_guard<std::mutex> lk(m_settingMutex);
		return m_settings;
	}

	unsigned settingsGeneration() const { return m_settingsGen.load(std::memory_order_relaxed); }

	// Render view / artwork-visibility snapshot for the Video menu.
	void publishVideo(EmbedVideo v)
	{
		std::lock_guard<std::mutex> lk(m_videoMutex);
		m_video = std::move(v);
	}

	EmbedVideo videoSnapshot() const
	{
		std::lock_guard<std::mutex> lk(m_videoMutex);
		return m_video;
	}

	// Capability snapshot for menu relevance (which menus to show at all).
	void publishCaps(EmbedCaps c)
	{
		std::lock_guard<std::mutex> lk(m_capsMutex);
		// Only advance the generation when capabilities actually change, so the
		// GUI re-applies menu relevance (which toggles menu visibility and can
		// disturb the embedded window's keyboard focus) rarely, not every frame.
		if (c != m_caps)
		{
			m_caps = std::move(c);
			m_capsGen.fetch_add(1, std::memory_order_relaxed);
		}
	}

	EmbedCaps capsSnapshot() const
	{
		std::lock_guard<std::mutex> lk(m_capsMutex);
		return m_caps;
	}

	unsigned capsGeneration() const { return m_capsGen.load(std::memory_order_relaxed); }

	// Slider snapshot (brightness/volume/…), re-published as values change.
	void publishSliders(std::vector<EmbedSlider> list)
	{
		std::lock_guard<std::mutex> lk(m_sliderMutex);
		m_sliders = std::move(list);
	}

	std::vector<EmbedSlider> slidersSnapshot() const
	{
		std::lock_guard<std::mutex> lk(m_sliderMutex);
		return m_sliders;
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

	mutable std::mutex m_settingMutex;
	std::vector<EmbedSetting> m_settings;
	std::atomic<unsigned> m_settingsGen{0};

	mutable std::mutex m_videoMutex;
	EmbedVideo m_video;

	mutable std::mutex m_capsMutex;
	EmbedCaps m_caps;
	std::atomic<unsigned> m_capsGen{0};

	mutable std::mutex m_sliderMutex;
	std::vector<EmbedSlider> m_sliders;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_EMBEDSESSION_H
