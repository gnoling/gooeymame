// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  emulator.cpp - qtui emulation entry points (no Qt dependencies)
//
//  Drives the Qt-native OSD (qt_osd_interface : osd_common_t) — renders into a
//  QWindow, takes input from the Qt bus, uses non-SDL sound/font/monitor — so
//  the qtui build needs nothing from src/osd/sdl.  Factored into functions so it
//  can be driven either by command-line passthrough or by the Qt GUI.
//
//============================================================

#include "emulator.h"

// OSD headers
#include "modules/lib/osdobj_common.h"   // osd_common_t / osd_options
#include "modules/osdwindow.h"           // osd_window / video_config
#include "modules/lib/osdlib.h"
#include "modules/monitor/monitor_module.h"   // pick_monitor() for the Qt-native window
#include "modules/diagnostics/diagnostics_module.h"

// qtui Qt-native OSD (Phase 13): create render windows backed by a QWindow via
// these Qt-free shims, so this translation unit stays clear of Qt headers
#include "qtnativewindow.h"
#include "qtembedtarget.h"
#include "qtinput.h"   // QtInputBus focus state for has_focus()
#include "qtbgfxchains.h"   // BGFX shader-effect enumeration/selection

// MAME headers
#include "emu.h"
#include "crsshair.h"   // crosshair_manager::get_usage() for menu relevance
#include "emuopts.h"
#include "main.h"
#include "mame.h"
#include "mameopts.h"
#include "audit.h"
#include "diimage.h"
#include "dinetwork.h"     // device_network_interface (Network Devices menu)
#include "dislot.h"
#include "natkeyboard.h"
#include "uiinput.h"       // ui_input_manager::reset() during input capture
#include "romload.h"       // system-BIOS rom entries (BIOS Selection menu)
#include "imagedev/cassette.h"   // cassette_image_device (Tape Control menu)
#include "machine/bcreader.h"    // barcode_reader_device (Barcode Reader menu)
#include "cheat.h"         // cheat_manager / cheat_entry (Cheat menu)
#include "luaengine.h"     // lua_engine get_menu/menu_populate/menu_callback (Plugin Options)
#include "ui/ui.h"
#include "ui/info.h"       // machine_info::game_info_string()/warnings_string()
#include "ui/menuitem.h"   // ui::menu_item (slider list entries)
#include "ui/slider.h"     // slider_state + SLIDER_NOCHANGE (live adjustments)
#include "iptseqpoll.h"    // input_sequence_poller (interactive input remapping)
#include "bookkeeping.h"   // bookkeeping_manager (coin counters, tickets)
#include "speaker.h"       // speaker_device (effect chain tags)
#include "audio_effects/aeffect.h"        // audio_effect base + effect_names
#include "audio_effects/filter.h"         // audio_effect_filter
#include "audio_effects/compressor.h"     // audio_effect_compressor
#include "audio_effects/eq.h"             // audio_effect_eq
#include "audio_effects/reverb.h"         // audio_effect_reverb

#include "drivenum.h"
#include "rendlay.h"   // layout_view visibility toggles (bezel/artwork)
#include "softlist_dev.h"
#include "softlist.h"

#include "corestr.h"
#include "unzip.h"

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cstdio>     // SEEK_SET / SEEK_CUR (cassette seek)
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "osdepend.h"
#include "strconv.h"

#include <clocale>
#include <locale.h>
#include <string>

#include "embedsession.h"

// Desktop-unix fontconfig init (Linux/BSD): the bitmap UI font provider doesn't
// need it, but MAME's font handling expects fontconfig to be available there.
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(__HAIKU__) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#define QTUI_USE_FONTCONFIG 1
#include <fontconfig/fontconfig.h>
#endif

#if !defined(_WIN32)
#include <unistd.h>
#endif


//============================================================
//  Global variables
//
//  sdl_entered_debugger is normally defined in sdlmain.cpp, which we do
//  not compile (qtmain.cpp supplies the program entry point instead).  The
//  Qt debugger module references it, so define it here.
//
//  video_config is the global the shared renderers/window code read (declared
//  extern in modules/osdwindow.h); each OSD defines it in its own video.cpp.
//  The qtui OSD doesn't compile src/osd/sdl/video.cpp, so define it here.
//============================================================

int sdl_entered_debugger;

osd_video_config video_config;

// OSD-provided free functions normally defined in the SDL OSD (sdlopts.cpp /
// window.cpp), which the qtui build no longer compiles.  The frontend
// (ui.cpp / miscmenu.cpp / luaengine.cpp) references them, so supply them here.
void osd_setup_osd_specific_emu_options(emu_options &opts)
{
	opts.add_entries(osd_options::s_option_entries);
}

void osd_set_aggressive_input_focus(bool aggressive_focus)
{
	// no-op: the Qt-native OSD manages input focus through the render window
}


//============================================================
//  Clipboard
//
//  On unix the SDL OSD provided osd_get/set_clipboard_text (osdlib_unix.cpp via
//  SDL); the qtui build has no SDL, so define them here backed by a Qt-free hook
//  the front-end (mainwindow.cpp) fills with QClipboard access (marshalled to
//  the GUI thread).  On Windows osdlib_win32.cpp already provides a Win32 (SDL-
//  free) clipboard, so we must NOT redefine the osd_* functions there — only the
//  hook setter is compiled (harmlessly unused).
//============================================================

namespace {
std::mutex g_clipboard_mutex;
std::function<std::string ()> g_clipboard_get;
std::function<bool (const std::string &)> g_clipboard_set;
}

void qtui_set_clipboard_hooks(
		std::function<std::string ()> get_text,
		std::function<bool (const std::string &)> set_text)
{
	std::lock_guard<std::mutex> lock(g_clipboard_mutex);
	g_clipboard_get = std::move(get_text);
	g_clipboard_set = std::move(set_text);
}

#ifndef _WIN32
std::string osd_get_clipboard_text() noexcept
{
	try
	{
		std::function<std::string ()> fn;
		{
			std::lock_guard<std::mutex> lock(g_clipboard_mutex);
			fn = g_clipboard_get;
		}
		if (fn)
			return fn();
	}
	catch (...)
	{
	}
	return std::string();
}

std::error_condition osd_set_clipboard_text(std::string_view text) noexcept
{
	try
	{
		std::function<bool (const std::string &)> fn;
		{
			std::lock_guard<std::mutex> lock(g_clipboard_mutex);
			fn = g_clipboard_set;
		}
		if (fn && fn(std::string(text)))
			return std::error_condition();
	}
	catch (...)
	{
	}
	return std::errc::io_error;
}
#endif // !_WIN32


//============================================================
//  Core serialisation
//
//  Building machine configurations (software enumeration, ROM auditing) is
//  not safe to do from two threads at once, and it mutates the process-wide
//  locale.  This mutex guards every such operation so the background auditor
//  and the on-selection software enumeration never overlap.
//============================================================

namespace {

std::mutex g_core_mutex;

// Per-platform default ini search path, mirroring the SDL OSD's (sdlopts.cpp)
// so existing user config (e.g. under $HOME/.<appname>) is still found — the
// bare emu_options default ("ini") would only look in ./ini.  APP_NAME is
// substituted with the lowercase application name at construction.
#if defined(_WIN32)
	#define QTUI_INI_PATH ".;ini;ini/presets"
#elif defined(__APPLE__)
	#define QTUI_INI_PATH "$HOME/Library/Application Support/APP_NAME;$HOME/.APP_NAME;.;ini"
#else
	#define QTUI_INI_PATH "$HOME/.APP_NAME;.;ini"
#endif

// Options class for the Qt-native OSD: osd_options (all the OSDOPTION_* video/
// render options the renderers read) plus the per-platform inipath default.
// Replaces qt_options so the qtui build needs nothing from src/osd/sdl.
class qt_options : public osd_options
{
public:
	qt_options()
	{
		std::string inipath(QTUI_INI_PATH);
		strreplace(inipath, "APP_NAME", emulator_info::get_appname_lower());
		set_default_value(OPTION_INIPATH, std::move(inipath));
	}
};

// RAII helper: hold the core mutex and force the "C" locale (which the MAME
// parsers require) for the duration, restoring it on scope exit.
class core_guard
{
public:
	core_guard() : m_lock(g_core_mutex), m_saved(std::setlocale(LC_ALL, nullptr))
	{
		std::setlocale(LC_ALL, "C");
	}
	~core_guard()
	{
		std::setlocale(LC_ALL, m_saved.c_str());
	}

private:
	std::lock_guard<std::mutex> m_lock;
	std::string m_saved;
};

} // anonymous namespace


void qtui_init_process()
{
	// disable I/O buffering
	setvbuf(stdout, (char *) nullptr, _IONBF, 0);
	setvbuf(stderr, (char *) nullptr, _IONBF, 0);

	// Initialize crash diagnostics
	diagnostics_module::get_instance()->init_crash_diagnostics();
}


std::vector<std::string> qtui_command_line(int argc, char **argv)
{
	// Wrap osd_get_command_line so qtmain.cpp (Qt-only, no MAME core headers) can
	// obtain a properly-decoded argument vector (UTF-8/wide-aware on Windows).
	return osd_get_command_line(argc, argv);
}


//============================================================
//  Embedded in-process emulation
//
//  The Qt-native OSD captures the running_machine and, each frame (in update(),
//  which runs on the emulation thread), drains the UI's command queue via the
//  EmbedController and applies it directly to the machine.  This is the analog
//  of NEWUI, whose native menu is serviced by the Windows OSD's message pump
//  (src/osd/winui/newui.cpp invoke_command()).
//============================================================

namespace {

// The in-game command queue + capability/snapshot publishing, factored out of
// the OSD so it can be driven by either the SDL-backed OSD or the Qt-native
// (osd_common_t) OSD.  Holds the running machine + EmbedSession; uses only
// running_machine APIs and osd_common_t::window_list(), so it's OSD-agnostic.
class EmbedController
{
public:
	EmbedController(osd::qtui::EmbedSession &session) :
		m_session(session)
	{
	}

	// True while an interactive input-remap capture is running.  The OSD reports
	// focus during capture so device polling continues even though the remap
	// dialog (not the game surface) holds the keyboard focus — otherwise
	// should_poll_devices() (background_input || has_focus) is false and neither
	// the keyboard nor the controller is ever polled, so the poller sees nothing.
	bool capture_active() const { return m_seqPoll != nullptr; }

	// True while an open plugin menu is polling for raw input (a "nokeys" overlay,
	// e.g. autofire's Hotkey capture).  Like capture_active(), the OSD reports
	// focus so device polling continues while the plugin dialog holds focus.
	bool plugin_polling_active() const { return !m_pluginMenu.empty() && m_pluginState.nokeys; }

	void init(running_machine &machine)
	{
		m_machine = &machine;
		m_session.running.store(true);
		// A hard reset rebuilds the machine and re-runs init() on this same OSD
		// object, so re-arm the one-shot publish — otherwise caps/sliders/info/
		// BIOS/etc. stay stale after a reset (e.g. the BIOS menu would keep
		// showing the pre-reset selection).
		m_capsInit = false;
		refresh_images();
		refresh_slots();
		refresh_settings();
		refresh_video();
		// NOTE: refresh_caps() is deferred to update() — at osd init() time (early
		// in running_machine::start()) the sound/natkeyboard/crosshair managers it
		// queries don't exist yet, so calling it here crashes.
	}

	// Called once per frame by the OSD (which then does its own redraw).
	void update()
	{
		drain_commands();
		// advance any in-progress input-remap capture (publishes the partial
		// sequence each frame; applies + refreshes when it finishes)
		poll_capture();
		if (m_machine)
		{
			m_session.paused.store(m_machine->paused());
			// Publish capabilities once the machine has actually started.  The
			// first update()s fire during the "Initializing" phase (forced video
			// pumps from set_startup_text), before the sound/natkeyboard/crosshair
			// managers refresh_caps() queries are constructed — so wait for RESET.
			if (!m_capsInit && m_machine->phase() >= machine_phase::RESET)
			{
				m_capsInit = true;
				refresh_caps();
				refresh_sliders();
				refresh_shader_chains();
				refresh_info();
				refresh_bios();
				refresh_tape();
				refresh_network();
				refresh_barcode();
				refresh_crosshairs();
				refresh_cheats();
				refresh_inputmap();
				refresh_audio_effects();
				refresh_plugin_menus();
			}
			// Re-publish the image snapshot a couple of times a second so the
			// Media menu reflects reality: software/carts mount AFTER osd init(),
			// a cart load can trigger a reset, and MAME's own Tab UI can change
			// media too.  Iterating the few image devices is cheap.
			if (++m_imageRefreshTick >= 30)
			{
				m_imageRefreshTick = 0;
				refresh_images();
				// DIP/config + render view can also change via MAME's own Tab UI.
				refresh_settings();
				refresh_video();
				// A cart/disk load (or a slot hard-reset) can change which device
				// menus are relevant, so re-publish capability flags too — but only
				// once the managers it queries exist (see the one-shot above).
				if (m_machine->phase() >= machine_phase::RESET)
				{
					refresh_caps();
					refresh_sliders();
					refresh_info();
					refresh_tape();   // position advances while playing
					refresh_cheats(); // reflects changes via MAME's own cheat menu
				}
			}
			// A plugin menu that requested "idle" (e.g. cheatfind during a scan)
			// expects a callback with no key each frame so it can advance and
			// refresh its own contents; re-populate when it asks us to.
			poll_plugin_idle();
		}
	}

private:
	void apply(const osd::qtui::EmbedAction &a)
	{
		using osd::qtui::EmbedCommand;
		running_machine &m = *m_machine;
		switch (a.cmd)
		{
		case EmbedCommand::TogglePause:
			if (m.paused()) m.resume(); else m.pause();
			break;
		case EmbedCommand::SoftReset:
			m.schedule_soft_reset();
			break;
		case EmbedCommand::HardReset:
			m.schedule_hard_reset();
			break;
		case EmbedCommand::SaveState:
			m.schedule_save(a.sval.empty() ? std::string("1") : std::string(a.sval));
			break;
		case EmbedCommand::LoadState:
			m.schedule_load(a.sval.empty() ? std::string("1") : std::string(a.sval));
			break;
		case EmbedCommand::SaveSnapshot:
			m.video().save_active_screen_snapshots();
			break;
		case EmbedCommand::ToggleFullscreen:
			// No-op for the Qt-native OSD: fullscreen of the render surface is a
			// GUI-window concern, handled by MainWindow::setEmbedFullscreen (the
			// OSD window has no SDL-style destroy+recreate fullscreen toggle).
			break;
		case EmbedCommand::ToggleFps:
			{
				auto &ui = mame_machine_manager::instance()->ui();
				ui.set_show_fps(!ui.show_fps());
			}
			break;
		case EmbedCommand::SetThrottleRate:
			m.video().set_throttle_rate(float(a.dval));
			m.video().set_throttled(true);
			break;
		case EmbedCommand::ToggleThrottle:
			m.video().set_throttled(!m.video().throttled());
			break;
		case EmbedCommand::SetFrameskip:
			m.video().set_frameskip(a.ival);   // -1 = auto
			break;
		case EmbedCommand::SetRotate:
			{
				int orient = ROT0;
				switch (a.ival)
				{
				case 90:  orient = ROT90;  break;
				case 180: orient = ROT180; break;
				case 270: orient = ROT270; break;
				default:  orient = ROT0;   break;
				}
				// The game window's target is what the renderer draws (and is also
				// the UI target in a single-window run), so set it directly — do NOT
				// skip is_ui_target() or we'd rotate nothing.  Mirror NEWUI: when it
				// is the UI target, rotate the UI container too.
				render_manager &rm = m.render();
				if (render_target *const t = rm.first_target())
				{
					t->set_orientation(orient);
					if (t->is_ui_target())
					{
						render_container::user_settings s = rm.ui_container().get_user_settings();
						s.m_orientation = orient;
						rm.ui_container().set_user_settings(s);
					}
				}
			}
			break;
		case EmbedCommand::KeyboardEmulated:
			m.natkeyboard().set_in_use(false);
			break;
		case EmbedCommand::KeyboardNatural:
			m.natkeyboard().set_in_use(true);
			break;
		case EmbedCommand::Paste:
			m.natkeyboard().paste();
			break;
		case EmbedCommand::MountImage:
			if (device_image_interface *const img = find_image(a.sval))
			{
				img->load(a.sval2);   // ignore the (error, message) result; UI re-reads state
				refresh_images();
			}
			break;
		case EmbedCommand::UnloadImage:
			if (device_image_interface *const img = find_image(a.sval))
			{
				img->unload();
				refresh_images();
			}
			break;
		case EmbedCommand::SetSlot:
			// Slot devices are fixed at machine creation, so apply the new option
			// and hard-reset; the rebuilt machine re-publishes the slot snapshot.
			if (::slot_option *const opt = m.options().find_slot_option(a.sval))
			{
				try { opt->specify(a.sval2); }
				catch (...) { break; }
				m.schedule_hard_reset();
			}
			break;
		case EmbedCommand::SetField:
			// Live DIP-switch / machine-configuration change: locate the field by
			// (port tag, mask) and write the chosen setting value.  No reset needed.
			if (ioport_field *const f = find_field(a.sval, a.mask))
			{
				ioport_field::user_settings us;
				f->get_user_settings(us);
				us.value = a.value;
				f->set_user_settings(us);
				refresh_settings();
			}
			break;
		case EmbedCommand::SetView:
			if (render_target *const t = m.render().first_target())
			{
				t->set_view(unsigned(a.ival));
				refresh_video();
			}
			break;
		case EmbedCommand::SetVisibility:
			// Toggle an artwork-visibility layer (bezel/overlay/backdrop or a named
			// layout collection) on the current view — live, no reset.
			if (render_target *const t = m.render().first_target())
			{
				t->set_visibility_toggle(unsigned(a.ival), a.value != 0);
				refresh_video();
			}
			break;
		case EmbedCommand::SetFilter:
			// Screen scaling: bilinear (smooth/blurry) vs nearest (sharp pixels).
			// The renderers read the global video_config.filter at texture-create
			// time, so flip it and force every window to rebuild its textures —
			// live on the next frame, no reset.
			video_config.filter = a.ival ? 1 : 0;
			// force every window's renderer to rebuild textures (drawogl/drawbgfx)
			for (const auto &win : osd_common_t::window_list())
				if (osd_window *const w = win.get())
					if (w->has_renderer())
						w->renderer().notify_changed();
			refresh_video();
			break;
		case EmbedCommand::SetKeepAspect:
			if (render_target *const t = m.render().first_target())
			{
				t->set_keepaspect(a.ival != 0);
				refresh_video();
			}
			break;
		case EmbedCommand::SetScaleMode:
			if (render_target *const t = m.render().first_target())
			{
				t->set_scale_mode(a.ival);
				refresh_video();
			}
			break;
		case EmbedCommand::SetZoomToScreen:
			if (render_target *const t = m.render().first_target())
			{
				t->set_zoom_to_screen(a.ival != 0);
				refresh_video();
			}
			break;
		case EmbedCommand::SetSlider:
			{
				// Re-fetch the combined slider list (same order as the snapshot)
				// and drive the addressed slider's update callback live.
				std::vector<slider_state *> const sl = collect_sliders();
				if (a.ival >= 0 && a.ival < int(sl.size()) && sl[a.ival])
				{
					sl[a.ival]->update(nullptr, std::int32_t(a.dval));
					refresh_sliders();
				}
			}
			break;
		case EmbedCommand::SetShaderChain:
			// Switch the BGFX shader effect for screen 0 (no-op if not BGFX).
			{
				auto const &wins = osd_common_t::window_list();
				if (!wins.empty() && wins.front())
					osd::qtui::bgfx_select_chain(*wins.front(), 0, a.ival);
				refresh_shader_chains();
			}
			break;
		case EmbedCommand::SetBios:
			// Mirror menu_bios_selection: set the device's system BIOS + the
			// matching option, then hard reset to reconfigure.
			if (device_t *const dev = find_device(a.sval))
			{
				dev->set_system_bios(u8(a.ival));
				if (!std::strcmp(dev->tag(), ":"))
					m.options().set_value(OPTION_BIOS, a.ival - 1, OPTION_PRIORITY_CMDLINE);
				else if (dev->owner())
					m.options().slot_option(dev->owner()->tag() + 1).set_bios(string_format("%d", a.ival - 1));
				m.schedule_hard_reset();
			}
			break;
		case EmbedCommand::TapeControl:
			if (cassette_image_device *const c = find_cassette(a.sval))
			{
				switch (a.ival)
				{
				case 0: c->change_state(CASSETTE_STOPPED, CASSETTE_MASK_UISTATE); break;
				case 1: c->change_state(CASSETTE_PLAY, CASSETTE_MASK_UISTATE); break;
				case 2: c->change_state(CASSETTE_RECORD, CASSETTE_MASK_UISTATE); break;
				case 3: c->seek(-30, SEEK_CUR); break;
				case 4: c->seek(+30, SEEK_CUR); break;
				case 5: c->seek(0, SEEK_SET); break;
				default: break;
				}
				refresh_tape();
			}
			break;
		case EmbedCommand::SetNetwork:
			if (device_network_interface *const net = find_network(a.sval))
			{
				net->set_interface(a.ival);
				refresh_network();
			}
			break;
		case EmbedCommand::BarcodeDecode:
			if (barcode_reader_device *const bc = find_barcode(a.sval))
				bc->write_code(a.sval2.c_str(), int(a.sval2.size()));
			break;
		case EmbedCommand::SetCrosshairMode:
			if (a.ival >= 0 && a.ival < MAX_PLAYERS && m.crosshair().get_usage())
			{
				render_crosshair &cross = m.crosshair().get_crosshair(a.ival);
				cross.set_mode(u8(a.value));
				cross.set_visible(a.value != 0 /* CROSSHAIR_VISIBILITY_OFF */);
				refresh_crosshairs();
			}
			break;
		case EmbedCommand::CheatToggleGlobal:
			if (m.options().cheat())
			{
				cheat_manager &cheat = mame_machine_manager::instance()->cheat();
				cheat.set_enable(!cheat.enabled(), true);
				refresh_cheats();
			}
			break;
		case EmbedCommand::CheatSelect:
			if (m.options().cheat())
			{
				cheat_manager &cheat = mame_machine_manager::instance()->cheat();
				auto const &list = cheat.entries();
				if (a.ival >= 0 && a.ival < int(list.size()))
				{
					cheat_entry &e = *list[a.ival];
					if (a.value == 1)       e.select_next_state();
					else if (a.value == 2)  e.select_previous_state();
					else                    e.select_default_state();
					refresh_cheats();
				}
			}
			break;
		case EmbedCommand::CheatReload:
			if (m.options().cheat())
			{
				mame_machine_manager::instance()->cheat().reload();
				refresh_cheats();
			}
			break;
		case EmbedCommand::PluginMenuOpen:
			open_plugin_menu(a.sval);
			break;
		case EmbedCommand::PluginMenuEvent:
			plugin_menu_event(a.ival, a.sval2);
			break;
		case EmbedCommand::PluginMenuClose:
			close_plugin_menu();
			break;
		case EmbedCommand::InputCaptureStart:
			start_capture(a.ival, a.value != 0);
			break;
		case EmbedCommand::InputCaptureCancel:
			cancel_capture();
			break;
		case EmbedCommand::InputSetDefault:
			set_input_default_or_none(a.ival, true);
			break;
		case EmbedCommand::InputSetNone:
			set_input_default_or_none(a.ival, false);
			break;
		case EmbedCommand::SetEffectParam:
			apply_effect_param(int(a.mask), int(a.value), a.ival, a.dval);
			break;
		case EmbedCommand::ResetEffectParam:
			reset_effect_param(int(a.mask), int(a.value), a.ival);
			break;
		case EmbedCommand::ResetEffect:
			reset_effect(int(a.mask), int(a.value));
			break;
		case EmbedCommand::RefocusInput:
			// No-op: this was the SDL foreign-window (-attach_window) focus/grab
			// dance for the SDL embed path.  The Qt-native window gets focus from
			// Qt directly (the input bus), so nothing is needed here.  (The SDL
			// embed path that relied on this is being retired.)
			break;
		case EmbedCommand::Exit:
			m.schedule_exit();
			break;
		}
	}

	void drain_commands()
	{
		if (!m_machine)
			return;
		osd::qtui::EmbedAction a;
		while (m_session.take(a))
			apply(a);
	}

	// Locate a settings field (DIP/config) by its owning port tag and mask.
	ioport_field *find_field(const std::string &tag, std::uint32_t mask)
	{
		if (!m_machine)
			return nullptr;
		for (auto &port : m_machine->ioport().ports())
		{
			if (port.second->tag() != tag)
				continue;
			for (ioport_field &field : port.second->fields())
				if (field.mask() == mask)
					return &field;
		}
		return nullptr;
	}

	// Publish the live DIP switches and machine-configuration fields for the GUI.
	void refresh_settings()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedSetting> out;
		for (auto &port : m_machine->ioport().ports())
		{
			for (ioport_field &field : port.second->fields())
			{
				if (field.type() != IPT_DIPSWITCH && field.type() != IPT_CONFIG)
					continue;
				if (!field.enabled() || field.settings().empty())
					continue;
				osd::qtui::EmbedSetting s;
				s.config = (field.type() == IPT_CONFIG);
				s.portTag = port.second->tag();
				s.mask = field.mask();
				s.name = field.name();
				s.current = field.setting_name() ? field.setting_name() : "";
				for (const ioport_setting &setting : field.settings())
					if (setting.enabled())
						s.options.emplace_back(setting.value(), setting.name());
				out.push_back(std::move(s));
			}
		}
		m_session.publishSettings(std::move(out));
	}

	// Publish the render views + the current view's artwork-visibility toggles
	// (bezel/overlay/...) for the GUI's Video menu.
	void refresh_video()
	{
		if (!m_machine)
			return;
		osd::qtui::EmbedVideo v;
		if (render_target *const t = m_machine->render().first_target())
		{
			for (unsigned i = 0; const char *const name = t->view_name(i); ++i)
				v.views.emplace_back(name);
			v.currentView = int(t->view());

			layout_view const &view = t->current_view();
			u32 const mask = t->visibility_mask();
			unsigned idx = 0;
			for (const auto &toggle : view.visibility_toggles())
			{
				v.toggles.push_back({ toggle.name(), BIT(mask, idx) != 0 });
				++idx;
			}
			v.keepaspect = t->keepaspect();
			v.scaleMode = t->scale_mode();
			v.zoomToScreen = t->zoom_to_screen();
			v.zoomAvailable = view.has_art();   // zoom-to-screen only matters with artwork
		}
		v.smooth = (video_config.filter != 0);
		m_session.publishVideo(std::move(v));
	}

	// Locate a user-loadable image device by its brief instance name ("flop1").
	device_image_interface *find_image(const std::string &brief)
	{
		if (!m_machine)
			return nullptr;
		for (device_image_interface &img : image_interface_enumerator(m_machine->root_device()))
			if (img.brief_instance_name() == brief)
				return &img;
		return nullptr;
	}

	// Publish the current mountable image devices for the GUI's Media menu.
	void refresh_images()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedImage> out;
		for (device_image_interface &img : image_interface_enumerator(m_machine->root_device()))
		{
			if (!img.user_loadable())
				continue;
			osd::qtui::EmbedImage e;
			e.brief = img.brief_instance_name();
			e.label = img.instance_name() + " [" + img.brief_instance_name() + "]";
			e.loaded = img.exists();
			if (img.exists())
				e.filename = (img.basename() && img.basename()[0]) ? img.basename() : "(loaded)";
			out.push_back(std::move(e));
		}
		m_session.publishImages(std::move(out));
	}

	// Publish the user-configurable device slots for the GUI's Slots menu.
	void refresh_slots()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedSlot> out;
		for (device_slot_interface &slot : slot_interface_enumerator(m_machine->root_device()))
		{
			if (!slot.has_selectable_options())
				continue;
			osd::qtui::EmbedSlot s;
			s.name = std::string(slot.slot_name());
			s.current = m_machine->options().slot_option(s.name).value();
			s.defaultOption = slot.default_option() ? slot.default_option() : "";
			for (const auto &ent : slot.option_list())
				if (ent.second->selectable())
					s.options.emplace_back(ent.second->name());   // string_view -> std::string
			std::sort(s.options.begin(), s.options.end());
			out.push_back(std::move(s));
		}
		m_session.publishSlots(std::move(out));
	}

	// Publish capability flags so the GUI shows only the menus relevant to the
	// running machine (mirrors the predicates MAME's menu_main::populate() uses).
	void refresh_caps()
	{
		if (!m_machine)
			return;
		osd::qtui::EmbedCaps c;

		// DIP switches / machine configuration (same predicate as refresh_settings).
		for (auto &port : m_machine->ioport().ports())
		{
			for (ioport_field &field : port.second->fields())
			{
				if (!field.enabled() || field.settings().empty())
					continue;
				if (field.type() == IPT_DIPSWITCH)
					c.hasDips = true;
				else if (field.type() == IPT_CONFIG)
					c.hasConfigs = true;
			}
		}

		// User-loadable image devices + the running software item (if any).
		for (device_image_interface &img : image_interface_enumerator(m_machine->root_device()))
		{
			if (img.user_loadable())
				c.hasImages = true;
			if (c.swShort.empty())
			{
				if (const software_info *const sw = img.software_entry())
				{
					c.swShort = sw->shortname();
					c.swList = img.software_list_name();
				}
			}
		}

		// Configurable device slots.
		for (device_slot_interface &slot : slot_interface_enumerator(m_machine->root_device()))
		{
			if (slot.has_selectable_options())
			{
				c.hasSlots = true;
				break;
			}
		}

		c.hasSound = !m_machine->sound().no_sound();
		c.hasNaturalKeyboard = (m_machine->natkeyboard().keyboard_count() != 0);
		c.hasCrosshair = m_machine->crosshair().get_usage();
		c.cheatEnabled = m_machine->options().cheat();
		if (lua_engine *const lua = mame_machine_manager::instance()->lua())
			c.hasPlugins = !lua->get_menu().empty();

		// Device-presence flags (mirror menu_main::populate's conditions).
		c.hasTape = (cassette_device_enumerator(m_machine->root_device()).first() != nullptr);
		c.hasNetwork = (network_interface_enumerator(m_machine->root_device()).first() != nullptr);
		c.hasBarcode = (device_type_enumerator<barcode_reader_device>(m_machine->root_device()).first() != nullptr);
		for (device_t &dev : device_enumerator(m_machine->root_device()))
		{
			bool found = false;
			for (tiny_rom_entry const *rom = dev.rom_region(); rom && !ROMENTRY_ISEND(rom); ++rom)
				if (ROMENTRY_ISSYSTEM_BIOS(rom)) { found = true; break; }
			if (found) { c.hasBios = true; break; }
		}

		if (render_target *const t = m_machine->render().first_target())
		{
			unsigned n = 0;
			while (t->view_name(n))
				++n;
			c.multiView = (n > 1);
		}

		m_session.publishCaps(std::move(c));
	}

	// The combined UI + OSD slider list, in a stable order — the index into this
	// vector is the command key the GUI uses to address a slider.
	std::vector<slider_state *> collect_sliders()
	{
		std::vector<slider_state *> out;
		if (!m_machine)
			return out;
		auto &ui = mame_machine_manager::instance()->ui();
		for (ui::menu_item &mi : ui.get_slider_list())
			if (mi.type() == ui::menu_item_type::SLIDER)
				out.push_back(reinterpret_cast<slider_state *>(mi.ref()));
		for (ui::menu_item &mi : m_machine->osd().get_slider_list())
			if (mi.type() == ui::menu_item_type::SLIDER)
				out.push_back(reinterpret_cast<slider_state *>(mi.ref()));
		return out;
	}

	// Publish the live sliders (brightness/volume/speed/…) for the Video/Audio
	// menus, reading each current value via the slider's own update callback.
	void refresh_sliders()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedSlider> out;
		for (slider_state *const s : collect_sliders())
		{
			if (!s)
				continue;
			osd::qtui::EmbedSlider e;
			e.description = s->description;
			e.minval = s->minval;
			e.defval = s->defval;
			e.maxval = s->maxval;
			e.incval = s->incval ? s->incval : 1;
			std::string buf;
			e.current = s->update(&buf, SLIDER_NOCHANGE);
			e.text = buf;
			out.push_back(std::move(e));
		}
		m_session.publishSliders(std::move(out));
	}

	void refresh_shader_chains()
	{
		osd::qtui::EmbedShaderChains out;
		auto const &wins = osd_common_t::window_list();
		if (!wins.empty() && wins.front())
		{
			std::vector<std::string> names;
			int current = 0, screens = 0;
			if (osd::qtui::bgfx_chain_info(*wins.front(), names, current, screens))
			{
				out.available = true;
				out.names = std::move(names);
				out.current = current;
			}
		}
		m_session.publishShaderChains(std::move(out));
	}

	// Publish read-only info text (system info / warnings / bookkeeping) for the
	// Info menu's dialogs.  bookkeeping changes over time, hence re-published.
	void refresh_info()
	{
		if (!m_machine)
			return;
		osd::qtui::EmbedInfo info;
		auto &ui = mame_machine_manager::instance()->ui();
		info.sysInfo = ui.machine_info().game_info_string();
		info.warnings = ui.machine_info().warnings_string();

		// Bookkeeping (mirrors ui/miscmenu.cpp menu_bookkeeping).
		std::ostringstream out;
		int const secs = m_machine->time().seconds();
		char buf[64];
		std::snprintf(buf, sizeof(buf), "Uptime: %d:%02d:%02d\n\n",
				secs / 3600, (secs / 60) % 60, secs % 60);
		out << buf;
		bookkeeping_manager &bk = m_machine->bookkeeping();
		int const tickets = bk.get_dispensed_tickets();
		if (tickets)
			out << "Tickets dispensed: " << tickets << "\n\n";
		for (int i = 0; i < int(bookkeeping_manager::COIN_COUNTERS); ++i)
		{
			int const count = bk.coin_counter_get_count(i);
			if (!count && !bk.coin_lockout_get_state(i))
				continue;
			out << "Coin " << char('A' + i) << ": " << count;
			if (bk.coin_lockout_get_state(i))
				out << "  (locked out)";
			out << "\n";
		}
		std::string book = out.str();
		// Trim a trailing newline for tidiness.
		while (!book.empty() && book.back() == '\n')
			book.pop_back();
		info.bookkeeping = std::move(book);

		m_session.publishInfo(std::move(info));
	}

	device_t *find_device(const std::string &tag)
	{
		if (!m_machine)
			return nullptr;
		for (device_t &dev : device_enumerator(m_machine->root_device()))
			if (tag == dev.tag())
				return &dev;
		return nullptr;
	}

	cassette_image_device *find_cassette(const std::string &tag)
	{
		if (!m_machine)
			return nullptr;
		for (cassette_image_device &c : cassette_device_enumerator(m_machine->root_device()))
			if (tag == c.tag())
				return &c;
		return nullptr;
	}

	device_network_interface *find_network(const std::string &tag)
	{
		if (!m_machine)
			return nullptr;
		for (device_network_interface &n : network_interface_enumerator(m_machine->root_device()))
			if (tag == n.device().tag())
				return &n;
		return nullptr;
	}

	barcode_reader_device *find_barcode(const std::string &tag)
	{
		if (!m_machine)
			return nullptr;
		for (barcode_reader_device &b : device_type_enumerator<barcode_reader_device>(m_machine->root_device()))
			if (tag == b.tag())
				return &b;
		return nullptr;
	}

	// Publish the BIOS-selectable devices (system + slot cards), mirroring
	// ui/miscmenu.cpp menu_bios_selection.  Static, so published once.
	void refresh_bios()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedBios> out;
		for (device_t &device : device_enumerator(m_machine->root_device()))
		{
			device_t const *const parent = device.owner();
			device_slot_interface const *const slot = dynamic_cast<device_slot_interface const *>(parent);
			if (parent && !(slot && slot->get_card_device() == &device))
				continue;
			tiny_rom_entry const *rom = device.rom_region();
			if (!rom || ROMENTRY_ISEND(rom))
				continue;
			osd::qtui::EmbedBios b;
			for ( ; rom && !ROMENTRY_ISEND(rom); ++rom)
			{
				if (!ROMENTRY_ISSYSTEM_BIOS(rom))
					continue;
				std::string label = (rom->name && rom->name[0]) ? rom->name : "";
				if (rom->hashdata && rom->hashdata[0])
					label += std::string("  —  ") + rom->hashdata;
				b.options.emplace_back(int(ROM_GETBIOSFLAGS(rom)), std::move(label));
			}
			if (b.options.empty())
				continue;
			b.tag = device.tag();
			b.label = !parent ? "System" : (device.tag() + 1);
			b.current = device.system_bios();
			out.push_back(std::move(b));
		}
		m_session.publishBios(std::move(out));
	}

	// Publish cassette devices (transport state + position).  Re-published
	// periodically since position advances while playing.
	void refresh_tape()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedTape> out;
		for (cassette_image_device &cass : cassette_device_enumerator(m_machine->root_device()))
		{
			osd::qtui::EmbedTape t;
			t.tag = cass.tag();
			t.label = cass.brief_instance_name();
			t.loaded = cass.exists();
			cassette_state const st = cass.get_state();
			int const ui = st & CASSETTE_MASK_UISTATE;
			t.status = !t.loaded ? "no tape"
					: (ui == CASSETTE_PLAY) ? "playing"
					: (ui == CASSETTE_RECORD) ? "recording" : "stopped";
			if (t.loaded)
			{
				t.position = int(cass.get_position());
				t.length = int(cass.get_length());
			}
			out.push_back(std::move(t));
		}
		m_session.publishTapes(std::move(out));
	}

	// Publish network devices + the host interfaces they can bind to.
	void refresh_network()
	{
		if (!m_machine)
			return;
		std::vector<osd::network_device_info> const interfaces = m_machine->osd().list_network_devices();
		std::vector<osd::qtui::EmbedNetwork> out;
		for (device_network_interface &net : network_interface_enumerator(m_machine->root_device()))
		{
			osd::qtui::EmbedNetwork n;
			n.tag = net.device().tag();
			n.label = net.device().tag() + 1;
			n.current = net.get_interface();
			for (const osd::network_device_info &info : interfaces)
				n.interfaces.emplace_back(info.id, std::string(info.description));
			out.push_back(std::move(n));
		}
		m_session.publishNetwork(std::move(out));
	}

	// Publish barcode-reader devices (tag + label).
	void refresh_barcode()
	{
		if (!m_machine)
			return;
		std::vector<std::pair<std::string, std::string>> out;
		for (barcode_reader_device &bc : device_type_enumerator<barcode_reader_device>(m_machine->root_device()))
			out.emplace_back(bc.tag(), std::string(bc.tag() + 1));
		m_session.publishBarcodes(std::move(out));
	}

	// Publish the in-use per-player crosshairs and their visibility mode.
	void refresh_crosshairs()
	{
		if (!m_machine)
			return;
		std::vector<osd::qtui::EmbedCrosshair> out;
		if (m_machine->crosshair().get_usage())
		{
			for (int p = 0; p < MAX_PLAYERS; ++p)
			{
				render_crosshair &cross = m_machine->crosshair().get_crosshair(p);
				if (!cross.is_used())
					continue;
				osd::qtui::EmbedCrosshair c;
				c.player = p;
				c.mode = cross.mode();
				out.push_back(c);
			}
		}
		m_session.publishCrosshairs(std::move(out));
	}

	// Publish the cheat list (global enable + per-entry description/state).
	void refresh_cheats()
	{
		if (!m_machine || !m_machine->options().cheat())
			return;
		cheat_manager &cheat = mame_machine_manager::instance()->cheat();
		osd::qtui::EmbedCheat out;
		out.enabled = cheat.enabled();
		for (const std::unique_ptr<cheat_entry> &entry : cheat.entries())
		{
			std::string desc, state;
			uint32_t flags = 0;
			entry->menu_text(desc, state, flags);
			osd::qtui::EmbedCheat::Entry e;
			e.description = desc;
			e.state = state;
			e.textOnly = entry->is_text_only();
			out.entries.push_back(std::move(e));
		}
		m_session.publishCheats(std::move(out));
	}

	//------------------------------------------------------------------
	//  Plugin Options (Lua-driven menus; mirrors ui/pluginopt.cpp)
	//------------------------------------------------------------------

	// Publish the static list of plugin menu names (once, at init).  Empty when
	// no plugin registered a menu — the GUI hides the action in that case.
	void refresh_plugin_menus()
	{
		lua_engine *const lua = mame_machine_manager::instance()->lua();
		if (!lua)
			return;
		m_pluginState = osd::qtui::EmbedPluginState();
		m_pluginState.menus = lua->get_menu();
		m_pluginMenu.clear();
		m_pluginNeedIdle = false;
		m_session.publishPluginMenu(m_pluginState);
	}

	// Re-run the active menu's populate() callback and publish the item list.
	void populate_plugin_menu()
	{
		lua_engine *const lua = mame_machine_manager::instance()->lua();
		if (!lua || m_pluginMenu.empty())
			return;

		std::vector<std::tuple<std::string, std::string, std::string>> list;
		std::string flags;
		std::optional<long> const sel = lua->menu_populate(m_pluginMenu, list, flags);

		m_pluginState.active = true;
		m_pluginState.activeName = m_pluginMenu;
		m_pluginState.items.clear();
		for (auto &entry : list)
		{
			osd::qtui::EmbedPluginItem it;
			it.text = std::get<0>(entry);
			it.subtext = std::get<1>(entry);
			if (it.text == "---")
			{
				it.separator = true;
			}
			else
			{
				// space-separated per-item flags (see menu_plugin_opt::populate)
				std::string_view tf = std::get<2>(entry);
				for (std::size_t s = tf.find_first_not_of(' '); s != std::string_view::npos; )
				{
					tf.remove_prefix(s);
					auto const e = tf.find(' ');
					std::string_view const flag = tf.substr(0, e);
					tf.remove_prefix(flag.length());
					s = tf.find_first_not_of(' ');
					if (flag == "off")            it.disabled = true;
					else if (flag == "l")         it.leftArrow = true;
					else if (flag == "r")         it.rightArrow = true;
					else if (flag == "lr")        it.leftArrow = it.rightArrow = true;
					else if (flag == "invert")    it.invert = true;
					else if (flag == "heading")   { it.heading = true; it.disabled = true; }
				}
			}
			m_pluginState.items.push_back(std::move(it));
		}
		m_pluginState.selection = sel ? int(*sel) : 0;

		// menu-wide flags: we only care about "nokeys" (suppress char entry) and
		// "idle" (poll a no-key callback each frame).
		m_pluginNeedIdle = false;
		m_pluginState.nokeys = false;
		std::string_view mf = flags;
		for (std::size_t s = mf.find_first_not_of(' '); s != std::string_view::npos; )
		{
			mf.remove_prefix(s);
			auto const e = mf.find(' ');
			std::string_view const flag = mf.substr(0, e);
			mf.remove_prefix(flag.length());
			s = mf.find_first_not_of(' ');
			if (flag == "nokeys")    { m_pluginState.nokeys = true; m_pluginNeedIdle = true; }
			else if (flag == "idle") m_pluginNeedIdle = true;
		}

		m_session.publishPluginMenu(m_pluginState);
	}

	void open_plugin_menu(const std::string &name)
	{
		m_pluginMenu = name;
		m_pluginLastIndex = 0;
		populate_plugin_menu();
	}

	void close_plugin_menu()
	{
		m_pluginMenu.clear();
		m_pluginNeedIdle = false;
		m_pluginState.active = false;
		m_pluginState.activeName.clear();
		m_pluginState.items.clear();
		m_pluginState.selection = 0;
		m_pluginState.nokeys = false;
		m_session.publishPluginMenu(m_pluginState);
	}

	// Forward a navigation/edit event to the active menu's callback.  "back" at
	// the menu's own root (callback declines to repopulate) closes the menu.
	void plugin_menu_event(int index, const std::string &key)
	{
		lua_engine *const lua = mame_machine_manager::instance()->lua();
		if (!lua || m_pluginMenu.empty())
			return;

		// Remember the item the user acted on: an "idle" poller (e.g. autofire's
		// Hotkey capture) needs the callback driven against the SAME item index,
		// because the plugin routes the idle event by index and only the original
		// content row reaches the polling branch.  populate() often returns a nil
		// selection while polling, so we can't rely on m_pluginState.selection.
		m_pluginLastIndex = index;

		auto const result = lua->menu_callback(m_pluginMenu, index, key);
		if (result.second)
			m_pluginState.selection = m_pluginLastIndex = int(uintptr_t(*result.second));

		if (key == "back" && !result.first)
		{
			close_plugin_menu();
			return;
		}
		if (result.first)
			populate_plugin_menu();           // contents changed — re-read them
		else
			m_session.publishPluginMenu(m_pluginState); // just the selection moved
	}

	// Drive an "idle" plugin menu (cheatfind scans, autofire hotkey capture, …)
	// one step per frame against the last item the user acted on.
	void poll_plugin_idle()
	{
		if (m_pluginMenu.empty() || !m_pluginNeedIdle)
			return;
		lua_engine *const lua = mame_machine_manager::instance()->lua();
		if (!lua)
			return;
		auto const result = lua->menu_callback(m_pluginMenu, m_pluginLastIndex, "");
		if (result.second)
			m_pluginState.selection = m_pluginLastIndex = int(uintptr_t(*result.second));
		if (result.first)
			populate_plugin_menu();
	}

	//------------------------------------------------------------------
	//  Input remapping
	//------------------------------------------------------------------

	// A remappable input target the GUI refers to by index (parallel to the
	// published EmbedInputMap).  Either a machine field or a general input type.
	struct InputTarget
	{
		ioport_field   *field = nullptr;            // machine field (else general)
		ioport_type     type = IPT_INVALID;         // general input type (field==nullptr)
		int             player = 0;
		input_seq_type  seqtype = SEQ_TYPE_STANDARD;
		bool            analog = false;
	};

	static std::string seqtype_suffix(bool analog, input_seq_type st)
	{
		if (!analog)
			return std::string();
		switch (st)
		{
		case SEQ_TYPE_INCREMENT: return " Inc";
		case SEQ_TYPE_DECREMENT: return " Dec";
		default:                 return " Analog";
		}
	}

	input_seq current_seq(const InputTarget &t) const
	{
		if (t.field)
			return t.field->seq(t.seqtype);
		return m_machine->ioport().type_seq(t.type, t.player, t.seqtype);
	}

	void refresh_inputmap()
	{
		if (!m_machine)
			return;

		m_inputTargets.clear();
		std::vector<osd::qtui::EmbedInputEntry> out;
		ioport_manager &iom = m_machine->ioport();

		auto add_entry = [&] (const InputTarget &t, const std::string &group, const std::string &name)
		{
			input_seq const seq = current_seq(t);
			osd::qtui::EmbedInputEntry e;
			e.group = group;
			e.name = name + seqtype_suffix(t.analog, t.seqtype);
			e.seqText = m_machine->input().seq_name(seq);
			e.analog = t.analog;
			e.isNone = !seq.length();
			e.isDefault = (t.field) ? (seq == t.field->defseq(t.seqtype)) : false;
			m_inputTargets.push_back(t);
			out.push_back(std::move(e));
		};

		// this machine's controls
		for (auto const &port : iom.ports())
		{
			for (ioport_field &field : port.second->fields())
			{
				if (!field.enabled())
					continue;
				auto const cls = field.type_class();
				if (cls != INPUT_CLASS_CONTROLLER && cls != INPUT_CLASS_MISC && cls != INPUT_CLASS_KEYBOARD)
					continue;
				bool const analog = field.is_analog();
				int const last = analog ? SEQ_TYPE_DECREMENT : SEQ_TYPE_STANDARD;
				std::string const group = field.player() >= 0 && field.type_class() == INPUT_CLASS_CONTROLLER
						? ("Player " + std::to_string(field.player() + 1))
						: std::string("This Machine");
				for (int st = SEQ_TYPE_STANDARD; st <= last; st++)
				{
					InputTarget t;
					t.field = &field;
					t.player = field.player();
					t.seqtype = input_seq_type(st);
					t.analog = analog;
					add_entry(t, group, field.name());
				}
			}
		}

		// general inputs (UI navigation + standard player controls)
		for (input_type_entry const &entry : iom.types())
		{
			if (entry.name().empty())
				continue;
			bool const analog = ioport_manager::type_is_analog(entry.type());
			int const last = analog ? SEQ_TYPE_DECREMENT : SEQ_TYPE_STANDARD;
			std::string const group = (entry.group() == IPG_UI)
					? std::string("User Interface")
					: std::string("General Input");
			for (int st = SEQ_TYPE_STANDARD; st <= last; st++)
			{
				InputTarget t;
				t.type = entry.type();
				t.player = entry.player();
				t.seqtype = input_seq_type(st);
				t.analog = analog;
				add_entry(t, group, entry.name());
			}
		}

		m_session.publishInputMap(std::move(out));
	}

	// Apply a captured/explicit sequence to the input at `index`.
	void apply_seq(int index, const input_seq &seq)
	{
		if (index < 0 || index >= int(m_inputTargets.size()))
			return;
		InputTarget const &t = m_inputTargets[index];
		if (t.field)
		{
			ioport_field::user_settings settings;
			t.field->get_user_settings(settings);
			settings.seq[t.seqtype] = seq;
			if (seq.is_default())
				settings.cfg[t.seqtype].clear();
			else if (!seq.length())
				settings.cfg[t.seqtype] = "NONE";
			else
				settings.cfg[t.seqtype] = m_machine->input().seq_to_tokens(seq);
			t.field->set_user_settings(settings);
		}
		else
		{
			m_machine->ioport().set_type_seq(t.type, t.player, t.seqtype, seq);
		}
	}

	void publish_capture(bool finished, bool cancelled)
	{
		osd::qtui::EmbedCapture c;
		c.active = (m_seqPoll != nullptr);
		c.index = m_captureIndex;
		c.finished = finished;
		c.cancelled = cancelled;
		if (m_seqPoll)
			c.prompt = m_machine->input().seq_name(m_seqPoll->sequence());
		m_session.publishCapture(std::move(c));
	}

	void start_capture(int index, bool recordNext)
	{
		if (!m_machine || index < 0 || index >= int(m_inputTargets.size()))
			return;
		InputTarget const &t = m_inputTargets[index];
		m_captureIndex = index;
		if (t.analog)
			m_seqPoll = std::make_unique<axis_sequence_poller>(m_machine->input());
		else
			m_seqPoll = std::make_unique<switch_sequence_poller>(m_machine->input());
		if (recordNext)
			m_seqPoll->start(current_seq(t));
		else
			m_seqPoll->start();
		publish_capture(false, false);
	}

	void cancel_capture()
	{
		if (!m_seqPoll)
			return;
		m_seqPoll.reset();
		m_machine->ui_input().reset();
		m_captureIndex = -1;
		publish_capture(false, true);
	}

	// Called every frame while a capture is active (returns when finished).
	void poll_capture()
	{
		if (!m_seqPoll)
			return;
		if (m_seqPoll->poll())   // finished (1s after the last input change)
		{
			if (m_seqPoll->valid())
				apply_seq(m_captureIndex, m_seqPoll->sequence());
			m_seqPoll.reset();
			m_machine->ui_input().reset();
			m_captureIndex = -1;
			refresh_inputmap();
			publish_capture(true, false);
		}
		else
		{
			publish_capture(false, false);   // live partial-sequence display
		}
	}

	void set_input_default_or_none(int index, bool toDefault)
	{
		input_seq seq;
		if (toDefault)
			seq.set_default();
		else
			seq.reset();   // none
		apply_seq(index, seq);
		refresh_inputmap();
	}

	//---- Audio effect chains (per-speaker + default DSP) -------------------
	// Per-effect-type parameter ids (the SetEffectParam command key).  Values
	// are interpreted only after the effect type is known, so the small ranges
	// may overlap between types.  EQ ids are computed: 0 = mode, otherwise
	// id = 1 + band*4 + {0 shelf, 1 freq, 2 Q, 3 gain}.
	enum { FP_HP_ACTIVE, FP_HP_F, FP_HP_Q, FP_LP_ACTIVE, FP_LP_F, FP_LP_Q };
	enum { CP_MODE, CP_THRESHOLD, CP_RATIO, CP_ATTACK, CP_RELEASE, CP_IN_GAIN, CP_OUT_GAIN,
		CP_CONVEXITY, CP_LINK, CP_FEEDBACK, CP_INERTIA, CP_INERTIA_DECAY, CP_CEILING };
	enum { RP_MODE, RP_PRESET, RP_DRY, RP_WIDTH, RP_ERS, RP_ETAP, RP_EDAMP, RP_EL, RP_E2L,
		RP_LRS, RP_LDAMP, RP_LPDELAY, RP_LDIFF, RP_LWANDER, RP_LDECAY, RP_LSPIN, RP_LL };

	// formatters mirroring the MAME ui/audio_effect_* menus
	static std::string fmt_db(double v)     { return util::string_format("%1$+g dB", v); }
	static std::string fmt_hz(double v)     { return util::string_format("%1$d Hz", u32(v + 0.5)); }
	static std::string fmt_2dec(double v)   { return util::string_format("%1$.2f", v); }
	static std::string fmt_ms0(double v)    { return util::string_format("%1$.0f ms", v); }
	static std::string fmt_ms1(double v)    { return util::string_format("%1$.1f ms", v); }
	static std::string fmt_pct(double v)    { return util::string_format("%1$d%%", u32(v)); }
	static std::string fmt_ratio(double v)  { return (v > 0) ? util::string_format("%1$g:1", v) : std::string("Infinity:1"); }
	static std::string fmt_release(double v){ return (v < 0) ? std::string("Infinite") : fmt_ms0(v); }
	static std::string fmt_decay(double v)  { return util::string_format("%1$.2f s", v); }
	static std::string fmt_spin(double v)   { return util::string_format("%1$.2f Hz", v); }
	static std::string fmt_filt_f(double v) { return (v <= 20) ? std::string("DC removal") : fmt_hz(v); }

	static void addToggle(std::vector<osd::qtui::EmbedEffectParam> &v, int id, std::string group,
			std::string label, bool on, bool isset)
	{
		osd::qtui::EmbedEffectParam p;
		p.id = id; p.kind = osd::qtui::EmbedEffectParam::Toggle;
		p.group = std::move(group); p.label = std::move(label);
		p.value = on ? 1 : 0; p.isDefault = !isset;
		p.choices = { "Bypass", "Active" };
		p.text = on ? "Active" : "Bypass";
		v.push_back(std::move(p));
	}
	static void addChoice(std::vector<osd::qtui::EmbedEffectParam> &v, int id, std::string group,
			std::string label, int idx, bool isset, std::vector<std::string> choices)
	{
		osd::qtui::EmbedEffectParam p;
		p.id = id; p.kind = osd::qtui::EmbedEffectParam::Choice;
		p.group = std::move(group); p.label = std::move(label);
		p.value = idx; p.isDefault = !isset; p.choices = std::move(choices);
		p.text = (idx >= 0 && idx < int(p.choices.size())) ? p.choices[idx] : std::string();
		v.push_back(std::move(p));
	}
	static void addNum(std::vector<osd::qtui::EmbedEffectParam> &v, int id, std::string group,
			std::string label, double val, double mn, double mx, double step, bool isset,
			std::string text)
	{
		osd::qtui::EmbedEffectParam p;
		p.id = id; p.kind = osd::qtui::EmbedEffectParam::Numeric;
		p.group = std::move(group); p.label = std::move(label);
		p.value = val; p.minv = mn; p.maxv = mx; p.step = step;
		p.isDefault = !isset; p.text = std::move(text);
		v.push_back(std::move(p));
	}

	void build_effect_params(audio_effect *eff, std::vector<osd::qtui::EmbedEffectParam> &out)
	{
		switch (eff->type())
		{
		case audio_effect::FILTER:
		{
			auto *f = static_cast<audio_effect_filter *>(eff);
			addToggle(out, FP_HP_ACTIVE, "High-pass", "Mode", f->highpass_active(), f->isset_highpass_active());
			addNum(out, FP_HP_F, "High-pass", "Cutoff frequency", f->fh(), 20, 5000, 1, f->isset_fh(), fmt_filt_f(f->fh()));
			addNum(out, FP_HP_Q, "High-pass", "Q factor", f->qh(), 0.1, 10.0, 0.01, f->isset_qh(), fmt_2dec(f->qh()));
			addToggle(out, FP_LP_ACTIVE, "Low-pass", "Mode", f->lowpass_active(), f->isset_lowpass_active());
			addNum(out, FP_LP_F, "Low-pass", "Cutoff frequency", f->fl(), 100, 20000, 1, f->isset_fl(), fmt_hz(f->fl()));
			addNum(out, FP_LP_Q, "Low-pass", "Q factor", f->ql(), 0.1, 10.0, 0.01, f->isset_ql(), fmt_2dec(f->ql()));
			break;
		}
		case audio_effect::COMPRESSOR:
		{
			auto *c = static_cast<audio_effect_compressor *>(eff);
			addToggle(out, CP_MODE, "", "Mode", c->mode() != 0, c->isset_mode());
			addNum(out, CP_THRESHOLD, "", "Threshold", c->threshold(), -60, 6, 0.5, c->isset_threshold(), fmt_db(c->threshold()));
			addNum(out, CP_RATIO, "", "Ratio", c->ratio(), 1, 30, 0.1, c->isset_ratio(), fmt_ratio(c->ratio()));
			addNum(out, CP_ATTACK, "", "Attack", c->attack(), 0, 300, 1, c->isset_attack(), fmt_ms0(c->attack()));
			addNum(out, CP_RELEASE, "", "Release", c->release(), 0, 1000, 1, c->isset_release(), fmt_release(c->release()));
			addNum(out, CP_IN_GAIN, "", "Input gain", c->input_gain(), -12, 24, 0.5, c->isset_input_gain(), fmt_db(c->input_gain()));
			addNum(out, CP_OUT_GAIN, "", "Output gain", c->output_gain(), -12, 24, 0.5, c->isset_output_gain(), fmt_db(c->output_gain()));
			addNum(out, CP_CONVEXITY, "Advanced", "Convexity", c->convexity(), -2, 2, 0.01, c->isset_convexity(), fmt_2dec(c->convexity()));
			addNum(out, CP_LINK, "Advanced", "Channel link", c->channel_link(), 0, 1, 0.01, c->isset_channel_link(), fmt_2dec(c->channel_link()));
			addNum(out, CP_FEEDBACK, "Advanced", "Feedback", c->feedback(), 0, 1, 0.01, c->isset_feedback(), fmt_2dec(c->feedback()));
			addNum(out, CP_INERTIA, "Advanced", "Inertia", c->inertia(), -1, 0.3, 0.01, c->isset_inertia(), fmt_2dec(c->inertia()));
			addNum(out, CP_INERTIA_DECAY, "Advanced", "Inertia decay", c->inertia_decay(), 0.8, 0.96, 0.01, c->isset_inertia_decay(), fmt_2dec(c->inertia_decay()));
			addNum(out, CP_CEILING, "Advanced", "Ceiling", c->ceiling(), 0.3, 3, 0.01, c->isset_ceiling(), fmt_2dec(c->ceiling()));
			break;
		}
		case audio_effect::EQ:
		{
			auto *q = static_cast<audio_effect_eq *>(eff);
			static const u32 fmin[5] = { 20, 100, 100, 100, 500 };
			static const u32 fmax[5] = { 2000, 10000, 10000, 10000, 16000 };
			static const char *const bandName[5] =
				{ "Low Band", "Low Mid Band", "Mid Band", "High Mid Band", "High Band" };
			addToggle(out, 0, "", "Mode", q->mode() != 0, q->isset_mode());
			for (u32 b = 0; b < audio_effect_eq::BANDS; ++b)
			{
				int const base = 1 + b * 4;
				bool const shelfBand = (b == 0 || b == 4);
				bool const shelf = (b == 0) ? q->low_shelf() : (b == 4) ? q->high_shelf() : false;
				if (shelfBand)
				{
					osd::qtui::EmbedEffectParam p;
					p.id = base + 0; p.kind = osd::qtui::EmbedEffectParam::Toggle;
					p.group = bandName[b]; p.label = "Mode";
					p.value = shelf ? 1 : 0;
					p.isDefault = !(b == 0 ? q->isset_low_shelf() : q->isset_high_shelf());
					p.choices = { "Peak", "Shelf" };
					p.text = shelf ? "Shelf" : "Peak";
					out.push_back(std::move(p));
				}
				addNum(out, base + 1, bandName[b], "Frequency", q->f(b), fmin[b], fmax[b], 1, q->isset_f(b), fmt_hz(q->f(b)));
				if (!(shelfBand && shelf))   // shelf bands hide Q
					addNum(out, base + 2, bandName[b], "Q factor", q->q(b), 0.1, 10.0, 0.01, q->isset_q(b), fmt_2dec(q->q(b)));
				addNum(out, base + 3, bandName[b], "Gain", q->db(b), -12, 12, 0.1, q->isset_db(b), fmt_db(q->db(b)));
			}
			break;
		}
		case audio_effect::REVERB:
		{
			auto *r = static_cast<audio_effect_reverb *>(eff);
			std::vector<std::string> presets;
			for (u32 i = 0; i < audio_effect_reverb::preset_count(); ++i)
				presets.emplace_back(audio_effect_reverb::preset_name(i));
			std::vector<std::string> taps;
			for (u32 i = 0; i < audio_effect_reverb::early_tap_setup_count(); ++i)
				taps.emplace_back(audio_effect_reverb::early_tap_setup_name(i));
			u32 const curPreset = r->find_current_preset();
			addToggle(out, RP_MODE, "", "Mode", r->mode() != 0, r->isset_mode());
			addChoice(out, RP_PRESET, "", "Preset", int(curPreset), curPreset == r->default_preset(), presets);
			addNum(out, RP_DRY, "", "Dry level", r->dry_level(), 0, 100, 1, r->isset_dry_level(), fmt_pct(r->dry_level()));
			addNum(out, RP_WIDTH, "", "Stereo width", r->stereo_width(), 0, 100, 1, r->isset_stereo_width(), fmt_pct(r->stereo_width()));
			addNum(out, RP_ERS, "Early Reflections", "Room size", r->early_room_size(), 0, 100, 1, r->isset_early_room_size(), fmt_pct(r->early_room_size()));
			addChoice(out, RP_ETAP, "Early Reflections", "Tap setup", int(r->early_tap_setup()), r->isset_early_tap_setup(), taps);
			addNum(out, RP_EDAMP, "Early Reflections", "Damping", r->early_damping(), 100, 16000, 1, r->isset_early_damping(), fmt_hz(r->early_damping()));
			addNum(out, RP_EL, "Early Reflections", "Level", r->early_level(), 0, 100, 1, r->isset_early_level(), fmt_pct(r->early_level()));
			addNum(out, RP_E2L, "Early Reflections", "Send to Late", r->early_to_late_level(), 0, 100, 1, r->isset_early_to_late_level(), fmt_pct(r->early_to_late_level()));
			addNum(out, RP_LRS, "Late Reflections", "Room size", r->late_room_size(), 0, 100, 1, r->isset_late_room_size(), fmt_pct(r->late_room_size()));
			addNum(out, RP_LDAMP, "Late Reflections", "Damping", r->late_damping(), 100, 16000, 1, r->isset_late_damping(), fmt_hz(r->late_damping()));
			addNum(out, RP_LPDELAY, "Late Reflections", "Pre-delay", r->late_predelay(), 0, 200, 0.1, r->isset_late_predelay(), fmt_ms1(r->late_predelay()));
			addNum(out, RP_LDIFF, "Late Reflections", "Diffusion", r->late_diffusion(), 0, 100, 1, r->isset_late_diffusion(), fmt_pct(r->late_diffusion()));
			addNum(out, RP_LWANDER, "Late Reflections", "Wander", r->late_wander(), 0, 100, 1, r->isset_late_wander(), fmt_pct(r->late_wander()));
			addNum(out, RP_LDECAY, "Late Reflections", "Decay", r->late_global_decay(), 0.1, 30, 0.01, r->isset_late_global_decay(), fmt_decay(r->late_global_decay()));
			addNum(out, RP_LSPIN, "Late Reflections", "Spin", r->late_spin(), 0, 5, 0.01, r->isset_late_spin(), fmt_spin(r->late_spin()));
			addNum(out, RP_LL, "Late Reflections", "Level", r->late_level(), 0, 100, 1, r->isset_late_level(), fmt_pct(r->late_level()));
			break;
		}
		}
	}

	audio_effect *effect_at(int chain, int entry) const
	{
		if (!m_machine)
			return nullptr;
		auto &snd = m_machine->sound();
		std::vector<audio_effect *> chainv;
		if (chain == 0xffff)
			chainv = snd.default_effect_chain();
		else if (chain >= 0 && chain < int(snd.effect_chains()))
			chainv = snd.effect_chain(chain);
		if (entry >= 0 && entry < int(chainv.size()))
			return chainv[entry];
		return nullptr;
	}

	void refresh_audio_effects()
	{
		std::vector<osd::qtui::EmbedEffect> out;
		if (m_machine && !m_machine->sound().no_sound())
		{
			auto &snd = m_machine->sound();
			auto build_chain = [&] (int chainId, const std::string &tag, bool isDefault)
			{
				std::vector<audio_effect *> chainv =
						isDefault ? snd.default_effect_chain() : snd.effect_chain(chainId);
				for (int e = 0; e < int(chainv.size()); ++e)
				{
					osd::qtui::EmbedEffect ef;
					ef.chain = isDefault ? 0xffff : chainId;
					ef.chainTag = tag;
					ef.chainDefault = isDefault;
					ef.index = e;
					ef.type = chainv[e]->type();
					ef.typeName = audio_effect::effect_names[ef.type];
					build_effect_params(chainv[e], ef.params);
					out.push_back(std::move(ef));
				}
			};
			for (int c = 0; c < int(snd.effect_chains()); ++c)
				build_chain(c, snd.effect_chain_tag(c), false);
			build_chain(0, std::string(), true);   // the default chain
		}
		m_session.publishAudioEffects(std::move(out));
	}

	void apply_effect_param(int chain, int entry, int paramId, double value)
	{
		audio_effect *eff = effect_at(chain, entry);
		if (!eff)
			return;
		switch (eff->type())
		{
		case audio_effect::FILTER:
		{
			auto *f = static_cast<audio_effect_filter *>(eff);
			switch (paramId)
			{
			case FP_HP_ACTIVE: f->set_highpass_active(value != 0); break;
			case FP_HP_F:      f->set_fh(u32(value + 0.5)); break;
			case FP_HP_Q:      f->set_qh(float(value)); break;
			case FP_LP_ACTIVE: f->set_lowpass_active(value != 0); break;
			case FP_LP_F:      f->set_fl(u32(value + 0.5)); break;
			case FP_LP_Q:      f->set_ql(float(value)); break;
			}
			break;
		}
		case audio_effect::COMPRESSOR:
		{
			auto *c = static_cast<audio_effect_compressor *>(eff);
			switch (paramId)
			{
			case CP_MODE:          c->set_mode(u32(value)); break;
			case CP_THRESHOLD:     c->set_threshold(float(value)); break;
			case CP_RATIO:         c->set_ratio(float(value)); break;
			case CP_ATTACK:        c->set_attack(float(value)); break;
			case CP_RELEASE:       c->set_release(float(value)); break;
			case CP_IN_GAIN:       c->set_input_gain(float(value)); break;
			case CP_OUT_GAIN:      c->set_output_gain(float(value)); break;
			case CP_CONVEXITY:     c->set_convexity(float(value)); break;
			case CP_LINK:          c->set_channel_link(float(value)); break;
			case CP_FEEDBACK:      c->set_feedback(float(value)); break;
			case CP_INERTIA:       c->set_inertia(float(value)); break;
			case CP_INERTIA_DECAY: c->set_inertia_decay(float(value)); break;
			case CP_CEILING:       c->set_ceiling(float(value)); break;
			}
			break;
		}
		case audio_effect::EQ:
		{
			auto *q = static_cast<audio_effect_eq *>(eff);
			if (paramId == 0)
			{
				q->set_mode(u32(value));
			}
			else
			{
				int const b = (paramId - 1) / 4;
				int const sub = (paramId - 1) % 4;
				switch (sub)
				{
				case 0: (b == 0) ? q->set_low_shelf(value != 0) : q->set_high_shelf(value != 0); break;
				case 1: q->set_f(b, u32(value + 0.5)); break;
				case 2: q->set_q(b, float(value)); break;
				case 3: q->set_db(b, float(value)); break;
				}
			}
			break;
		}
		case audio_effect::REVERB:
		{
			auto *r = static_cast<audio_effect_reverb *>(eff);
			switch (paramId)
			{
			case RP_MODE:    r->set_mode(u32(value)); break;
			case RP_PRESET:  r->load_preset(u32(value)); break;
			case RP_DRY:     r->set_dry_level(value); break;
			case RP_WIDTH:   r->set_stereo_width(value); break;
			case RP_ERS:     r->set_early_room_size(value); break;
			case RP_ETAP:    r->set_early_tap_setup(u32(value)); break;
			case RP_EDAMP:   r->set_early_damping(value); break;
			case RP_EL:      r->set_early_level(value); break;
			case RP_E2L:     r->set_early_to_late_level(value); break;
			case RP_LRS:     r->set_late_room_size(value); break;
			case RP_LDAMP:   r->set_late_damping(value); break;
			case RP_LPDELAY: r->set_late_predelay(value); break;
			case RP_LDIFF:   r->set_late_diffusion(value); break;
			case RP_LWANDER: r->set_late_wander(value); break;
			case RP_LDECAY:  r->set_late_global_decay(float(value)); break;
			case RP_LSPIN:   r->set_late_spin(value); break;
			case RP_LL:      r->set_late_level(value); break;
			}
			break;
		}
		}
		if (chain == 0xffff)
			m_machine->sound().default_effect_changed(entry);
		refresh_audio_effects();
	}

	void reset_effect_param(int chain, int entry, int paramId)
	{
		audio_effect *eff = effect_at(chain, entry);
		if (!eff)
			return;
		switch (eff->type())
		{
		case audio_effect::FILTER:
		{
			auto *f = static_cast<audio_effect_filter *>(eff);
			switch (paramId)
			{
			case FP_HP_ACTIVE: f->reset_highpass_active(); break;
			case FP_HP_F:      f->reset_fh(); break;
			case FP_HP_Q:      f->reset_qh(); break;
			case FP_LP_ACTIVE: f->reset_lowpass_active(); break;
			case FP_LP_F:      f->reset_fl(); break;
			case FP_LP_Q:      f->reset_ql(); break;
			}
			break;
		}
		case audio_effect::COMPRESSOR:
		{
			auto *c = static_cast<audio_effect_compressor *>(eff);
			switch (paramId)
			{
			case CP_MODE:          c->reset_mode(); break;
			case CP_THRESHOLD:     c->reset_threshold(); break;
			case CP_RATIO:         c->reset_ratio(); break;
			case CP_ATTACK:        c->reset_attack(); break;
			case CP_RELEASE:       c->reset_release(); break;
			case CP_IN_GAIN:       c->reset_input_gain(); break;
			case CP_OUT_GAIN:      c->reset_output_gain(); break;
			case CP_CONVEXITY:     c->reset_convexity(); break;
			case CP_LINK:          c->reset_channel_link(); break;
			case CP_FEEDBACK:      c->reset_feedback(); break;
			case CP_INERTIA:       c->reset_inertia(); break;
			case CP_INERTIA_DECAY: c->reset_inertia_decay(); break;
			case CP_CEILING:       c->reset_ceiling(); break;
			}
			break;
		}
		case audio_effect::EQ:
		{
			auto *q = static_cast<audio_effect_eq *>(eff);
			if (paramId == 0)
			{
				q->reset_mode();
			}
			else
			{
				int const b = (paramId - 1) / 4;
				int const sub = (paramId - 1) % 4;
				switch (sub)
				{
				case 0: (b == 0) ? q->reset_low_shelf() : q->reset_high_shelf(); break;
				case 1: q->reset_f(b); break;
				case 2: q->reset_q(b); break;
				case 3: q->reset_db(b); break;
				}
			}
			break;
		}
		case audio_effect::REVERB:
		{
			auto *r = static_cast<audio_effect_reverb *>(eff);
			switch (paramId)
			{
			case RP_MODE:    r->reset_mode(); break;
			case RP_PRESET:  r->load_preset(r->default_preset()); break;
			case RP_DRY:     r->reset_dry_level(); break;
			case RP_WIDTH:   r->reset_stereo_width(); break;
			case RP_ERS:     r->reset_early_room_size(); break;
			case RP_ETAP:    r->reset_early_tap_setup(); break;
			case RP_EDAMP:   r->reset_early_damping(); break;
			case RP_EL:      r->reset_early_level(); break;
			case RP_E2L:     r->reset_early_to_late_level(); break;
			case RP_LRS:     r->reset_late_room_size(); break;
			case RP_LDAMP:   r->reset_late_damping(); break;
			case RP_LPDELAY: r->reset_late_predelay(); break;
			case RP_LDIFF:   r->reset_late_diffusion(); break;
			case RP_LWANDER: r->reset_late_wander(); break;
			case RP_LDECAY:  r->reset_late_global_decay(); break;
			case RP_LSPIN:   r->reset_late_spin(); break;
			case RP_LL:      r->reset_late_level(); break;
			}
			break;
		}
		}
		if (chain == 0xffff)
			m_machine->sound().default_effect_changed(entry);
		refresh_audio_effects();
	}

	void reset_effect(int chain, int entry)
	{
		audio_effect *eff = effect_at(chain, entry);
		if (!eff)
			return;
		switch (eff->type())
		{
		case audio_effect::FILTER:     static_cast<audio_effect_filter *>(eff)->reset_all(); break;
		case audio_effect::COMPRESSOR: static_cast<audio_effect_compressor *>(eff)->reset_all(); break;
		case audio_effect::EQ:         static_cast<audio_effect_eq *>(eff)->reset_all(); break;
		case audio_effect::REVERB:     static_cast<audio_effect_reverb *>(eff)->reset_all(); break;
		}
		if (chain == 0xffff)
			m_machine->sound().default_effect_changed(entry);
		refresh_audio_effects();
	}

	osd::qtui::EmbedSession &m_session;
	running_machine *m_machine = nullptr;
	unsigned m_imageRefreshTick = 0;
	bool m_capsInit = false;   // capabilities published on the first update() frame

	// input remapping
	std::vector<InputTarget> m_inputTargets;
	std::unique_ptr<input_sequence_poller> m_seqPoll;
	int m_captureIndex = -1;

	// plugin options
	std::string m_pluginMenu;       // active plugin menu name ("" = none open)
	bool m_pluginNeedIdle = false;  // active menu wants a no-key callback each frame
	int m_pluginLastIndex = 0;      // last item index acted on (drives idle pollers)
	osd::qtui::EmbedPluginState m_pluginState;
};


//============================================================
//  Qt-native OSD (Phase 13f) — derives directly from osd_common_t, NO SDL.
//
//  Renders into a QWindow (Qt GL/BGFX), takes input from the Qt bus, enumerates
//  monitors from QScreen, and selects non-SDL sound/font.  Initializes zero SDL
//  at runtime.  The in-game command queue is the shared EmbedController.
//============================================================

class qt_osd_interface : public osd_common_t
{
public:
	qt_osd_interface(qt_options &options, osd::qtui::QtEmbedTarget &target, osd::qtui::EmbedSession &session) :
		osd_common_t(options),
		m_options(options),
		m_target(target),
		m_ctrl(session)
	{
	}

	virtual void init(running_machine &machine) override
	{
		osd_common_t::init(machine);
		osd_common_t::init_subsystems();   // module-based; calls our video_init() + input_init()
		m_ctrl.init(machine);
	}

	virtual void update(bool skip_redraw) override
	{
		m_ctrl.update();
		osd_common_t::update(skip_redraw);   // resets the watchdog
		if (!skip_redraw)
			for (auto const &win : window_list())
				win->update();
	}

	// Qt input arrives via the bus; there is no SDL event pump to drain.
	virtual void process_events() override { }
	virtual void input_update(bool relative_reset) override { poll_input_modules(relative_reset); }
	virtual void check_osd_inputs() override { }

	// Focus comes from the Qt render window (the bus); gates input polling.
	// Force focus while an input-remap capture OR a plugin-menu input poller (e.g.
	// the autofire Hotkey assignment) is active, so device polling keeps running
	// even though the remap/plugin dialog holds the keyboard focus.
	virtual bool has_focus() const override
	{
		return osd::qtui::QtInputBus::instance().focused()
				|| m_ctrl.capture_active() || m_ctrl.plugin_polling_active();
	}

	virtual bool video_init() override
	{
		// CLI passthrough: the GUI thread hasn't pre-created a render surface, so
		// ask it to now (creates + shows the QWindow on the GUI thread).  Only
		// reached for invocations that actually run video — headless commands
		// (-listxml, -validate, …) return before video_init(), so they create no
		// window.  create_window() returns once the window is shown; we then wait
		// (off the GUI thread, so its event loop runs freely and delivers the
		// expose) for the native surface to exist before binding a GL/BGFX context.
		if (!m_target.window && m_target.create_window)
		{
			if (!m_target.create_window())
				return false;
			for (int i = 0; i < 500 && !m_target.exposed.load(std::memory_order_acquire); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (!m_target.exposed.load(std::memory_order_acquire))
			{
				osd_printf_error("qtui: render window was never exposed\n");
				return false;
			}
		}

		// Populate the bits of video_config the renderer/window read (replacing
		// the SDL OSD's private extract_video_config()).
		video_config.windowed = 1;
		video_config.numscreens = 1;
		video_config.prescale = m_options.prescale();
		if (video_config.prescale < 1 || video_config.prescale > 20)
			video_config.prescale = 1;
		video_config.filter = m_options.filter();
		video_config.waitvsync = m_options.wait_vsync();
		video_config.syncrefresh = m_options.sync_refresh();
		video_config.beamwidth = m_options.beam_width_min();

		osd_window_config conf{};
		auto win = osd::qtui::make_native_window(
				machine(),
				*m_render,
				0,
				m_monitor_module->pick_monitor(reinterpret_cast<osd_options &>(options()), 0),
				conf,
				m_target);
		if (!win)
			return false;

		osd_common_t::s_window_list.emplace_back(std::move(win));
		return true;
	}

	virtual void video_exit() override { window_exit(); }

	virtual void window_exit() override
	{
		while (!osd_common_t::s_window_list.empty())
		{
			auto win = std::move(osd_common_t::s_window_list.back());
			osd_common_t::s_window_list.pop_back();
			win->destroy();
		}
	}

	virtual qt_options &options() override { return m_options; }

private:
	qt_options &m_options;
	osd::qtui::QtEmbedTarget &m_target;
	EmbedController m_ctrl;
};

} // anonymous namespace


int qtui_run_embedded_native(
		const std::string &system,
		const std::string &software,
		osd::qtui::QtEmbedTarget *target,
		osd::qtui::EmbedSession &session,
		bool useBgfx,
		const std::string &bgfxBackend,
		const std::string &soundProvider)
{
	int res = 0;

	// Force the "C" locale for this thread only (see qtui_run_args): the MAME
	// ini/number parsers require it, while the Qt GUI thread keeps the user's
	// locale.  Unix uses POSIX uselocale; Windows uses per-thread CRT locale.
#if !defined(_WIN32)
	locale_t const cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t const prev = cloc ? uselocale(cloc) : (locale_t)0;
#elif defined(_WIN32)
	_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	std::string const win_prev_locale = std::setlocale(LC_ALL, nullptr);
	std::setlocale(LC_ALL, "C");
#endif

#ifdef QTUI_USE_FONTCONFIG
	FcInit();
#endif

	std::vector<std::string> args{ "mame", system };
	if (!software.empty())
		args.push_back(software);
	args.push_back("-window");
	// Force the renderer that the Qt-native window supports: OpenGL (drawogl via
	// qt_gl_context) or BGFX (drawbgfx via the native-handle provider).  BGFX
	// uses its OpenGL backend for now — most compatible with the GLX-capable
	// QWindow surface embedded via createWindowContainer.
	args.push_back("-video");
	if (useBgfx)
	{
		args.push_back("bgfx");
		// Let BGFX auto-pick by default (best shader support); honour an explicit
		// backend when the user selects one (e.g. vulkan renders more chains
		// correctly than the GL backend).
		if (!bgfxBackend.empty() && bgfxBackend != "auto")
		{
			args.push_back("-bgfx_backend");
			args.push_back(bgfxBackend);
		}
	}
	else
	{
		args.push_back("opengl");
	}
	// Use the Qt-native input providers (fed from the render window via the
	// QtInputBus) instead of SDL, which never sees our foreign window's events.
	args.push_back("-keyboardprovider");
	args.push_back("qt");
	args.push_back("-mouseprovider");
	args.push_back("qt");
	args.push_back("-lightgunprovider");
	args.push_back("qt");
	// Qt-native monitor enumeration (QScreen geometry captured on the GUI thread)
	// instead of the SDL monitor module.
	args.push_back("-monitorprovider");
	args.push_back("qt");
	// Non-SDL sound/font (the Qt-native OSD never initialises SDL video/window).
	// (-sound auto would pick SDL since it registers first.)  Joystick is the one
	// exception — the SDL game-controller subsystem on Linux (set below).
	args.push_back("-sound");
	args.push_back(soundProvider.empty() ? std::string("pulse") : soundProvider);
	args.push_back("-uifontprovider");
	args.push_back("none");
	// Gamepads (hybrid, like upstream MAME): native winhybrid on Windows, the
	// SDL game-controller module on Linux (input_sdlgame.cpp).
	args.push_back("-joystickprovider");
#if defined(_WIN32)
	args.push_back("winhybrid");
#else
	args.push_back("sdlgame");
#endif

	{
		qt_options options;
		qt_osd_interface osd(options, *target, session);
		osd.register_options();
		res = emulator_info::start_frontend(options, osd, args);
	}

	session.running.store(false);

#ifdef QTUI_USE_FONTCONFIG
	FcFini();
#endif

#if !defined(_WIN32)
	if (cloc)
	{
		uselocale(prev);
		freelocale(cloc);
	}
#elif defined(_WIN32)
	std::setlocale(LC_ALL, win_prev_locale.c_str());
#endif

	return res;
}


int qtui_run_args_native(
		std::vector<std::string> &args,
		osd::qtui::QtEmbedTarget *target,
		osd::qtui::EmbedSession &session,
		const std::string &soundProvider)
{
	int res = 0;

	// CLI passthrough through the Qt-native OSD.  Runs on a dedicated worker
	// thread (the Qt event loop owns the main thread), so force the "C" locale
	// per-thread — the MAME ini/number parsers require it while the GUI thread
	// keeps the user's locale.  See qtui_run_embedded_native().
#if !defined(_WIN32)
	locale_t const cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t const prev = cloc ? uselocale(cloc) : (locale_t)0;
#elif defined(_WIN32)
	_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	std::string const win_prev_locale = std::setlocale(LC_ALL, nullptr);
	std::setlocale(LC_ALL, "C");
#endif

#ifdef QTUI_USE_FONTCONFIG
	FcInit();
#endif

	// Append the providers the Qt-native OSD requires, so they take precedence
	// over anything in the user's args/inis.  Harmless for headless commands
	// (-listxml, -validate, …) — those return before any window/video is created.
	// The render window itself is created lazily by qt_osd_interface::video_init()
	// via target->create_window (set by the GUI side), so a headless run opens no
	// window at all.
	args.push_back("-video");
	args.push_back("opengl");
	args.push_back("-keyboardprovider");
	args.push_back("qt");
	args.push_back("-mouseprovider");
	args.push_back("qt");
	args.push_back("-lightgunprovider");
	args.push_back("qt");
	args.push_back("-monitorprovider");
	args.push_back("qt");
	args.push_back("-sound");
	args.push_back(soundProvider.empty() ? std::string("pulse") : soundProvider);
	args.push_back("-uifontprovider");
	args.push_back("none");
	// Gamepads (hybrid, like upstream MAME): native winhybrid on Windows, the
	// SDL game-controller module on Linux (input_sdlgame.cpp).
	args.push_back("-joystickprovider");
#if defined(_WIN32)
	args.push_back("winhybrid");
#else
	args.push_back("sdlgame");
#endif

	{
		qt_options options;
		qt_osd_interface osd(options, *target, session);
		osd.register_options();
		res = emulator_info::start_frontend(options, osd, args);
	}

	session.running.store(false);

#ifdef QTUI_USE_FONTCONFIG
	FcFini();
#endif

#if !defined(_WIN32)
	if (cloc)
	{
		uselocale(prev);
		freelocale(cloc);
	}
#elif defined(_WIN32)
	std::setlocale(LC_ALL, win_prev_locale.c_str());
#endif

	return res;
}


void qtui_load_software(
		const std::string &system,
		const std::atomic<bool> *cancel,
		const std::function<void (const std::vector<qtui_software_entry> &)> &on_entries,
		const std::function<void (int, int)> &on_audited)
{
	int const index = driver_list::find(system.c_str());
	if (index < 0)
	{
		on_entries({});
		return;
	}

	auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };

	// Serialise with the auditor and force the "C" locale (see qtui_run_args()
	// for why the locale matters).
	core_guard guard;

	try
	{
		const game_driver &driver = driver_list::driver(index);

		// Load the configured paths (hashpath in particular) so the software
		// list definition files can be found.
		qt_options options;
		std::ostringstream errors;
		mame_options::parse_standard_inis(options, errors, &driver);

		// Build the machine configuration once; both phases walk it in the
		// same order (get_info() is parsed and cached on first access).
		driver_enumerator drivlist(options, driver);
		drivlist.next();
		device_t &root = drivlist.config()->root_device();

		// Phase 1: enumerate entries (fast) and hand them over immediately.
		std::vector<qtui_software_entry> entries;
		for (software_list_device &swlistdev : software_list_device_enumerator(root))
		{
			for (const software_info &info : swlistdev.get_info())
			{
				qtui_software_entry entry;
				entry.list = swlistdev.list_name();
				entry.shortname = info.shortname();
				entry.parent = info.parentname();   // cloneof, "" if a parent
				entry.description = info.longname();
				entry.year = info.year();
				entry.publisher = info.publisher();
				entry.supported = int(info.supported());
				entry.availability = QTUI_AVAIL_UNKNOWN;
				entries.push_back(std::move(entry));
			}
		}
		on_entries(entries);

		// Phase 2: fast-audit each entry's ROMs (slow).
		media_auditor auditor(drivlist);
		int i = 0;
		for (software_list_device &swlistdev : software_list_device_enumerator(root))
		{
			for (const software_info &info : swlistdev.get_info())
			{
				if (cancelled())
					return;

				int availability;
				switch (auditor.audit_software(swlistdev, info, AUDIT_VALIDATE_FAST))
				{
				case media_auditor::CORRECT:
				case media_auditor::BEST_AVAILABLE:
				case media_auditor::NONE_NEEDED:
					availability = QTUI_AVAIL_AVAILABLE;
					break;
				default:
					availability = QTUI_AVAIL_UNAVAILABLE;
					break;
				}
				on_audited(i++, availability);
			}
		}
	}
	catch (...)
	{
		// A malformed software list or machine configuration should not take
		// down the front-end.
	}
}


int qtui_system_count()
{
	return int(driver_list::total());
}


namespace {

// Map a core_options entry type to the editable qtui_option_type, or -1 if it
// is not an editable option (header/command/invalid).
int qtui_map_option_type(core_options::option_type type)
{
	switch (type)
	{
	case core_options::option_type::BOOLEAN:   return QTUI_OPT_BOOLEAN;
	case core_options::option_type::INTEGER:   return QTUI_OPT_INTEGER;
	case core_options::option_type::FLOAT:     return QTUI_OPT_FLOAT;
	case core_options::option_type::STRING:    return QTUI_OPT_STRING;
	case core_options::option_type::PATH:      return QTUI_OPT_PATH;
	case core_options::option_type::MULTIPATH: return QTUI_OPT_MULTIPATH;
	default:                                   return -1;
	}
}

} // anonymous namespace


std::vector<qtui_option_group> qtui_read_options()
{
	std::vector<qtui_option_group> groups;

	core_guard guard;

	qt_options options;
	std::ostringstream errors;
	mame_options::parse_standard_inis(options, errors);

	qtui_option_group *current = nullptr;
	for (const auto &entry : options.entries())
	{
		if (entry->type() == core_options::option_type::HEADER)
		{
			groups.emplace_back();
			current = &groups.back();
			current->header = entry->description() ? entry->description() : "";
			continue;
		}

		int const mapped = qtui_map_option_type(entry->type());
		if (mapped < 0 || entry->names().empty())
			continue;

		// Skip internal/positional entries like "<UNADORNED0>" (the system and
		// software names) which are not real ini settings.
		if (!entry->name().empty() && entry->name().front() == '<')
			continue;

		// Options before the first header land in an unnamed leading group.
		if (!current)
		{
			groups.emplace_back();
			current = &groups.back();
		}

		qtui_option opt;
		opt.name = entry->name();
		opt.description = entry->description() ? entry->description() : "";
		opt.value = entry->value() ? entry->value() : "";
		// Note: entry::default_value() abort()s on entries that don't support
		// it, so it is intentionally not read here.
		opt.minimum = entry->minimum() ? entry->minimum() : "";
		opt.maximum = entry->maximum() ? entry->maximum() : "";
		opt.type = mapped;
		current->options.push_back(std::move(opt));
	}

	return groups;
}


bool qtui_write_options(
		const std::vector<std::pair<std::string, std::string>> &changes,
		std::string *out_path)
{
	core_guard guard;

	qt_options options;

	// Find the mame.ini that is actually loaded, by searching the *default*
	// inipath (the search MAME itself uses on startup) before parse_standard_inis
	// overwrites inipath with the value the file contains.  We must write back to
	// that same file; otherwise the new mame.ini lands in a directory that is
	// searched later and gets shadowed by the one in effect, so edits silently
	// have no effect.
	std::string load_path;
	{
		std::string const search = options.ini_path();
		std::string::size_type pos = 0;
		while (pos <= search.size())
		{
			std::string::size_type const sep = search.find(';', pos);
			std::string const dir = search.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
			pos = (sep == std::string::npos) ? search.size() + 1 : sep + 1;
			if (dir.empty())
				continue;
			std::filesystem::path const candidate = std::filesystem::path(dir) / "mame.ini";
			std::error_code ec;
			if (std::filesystem::exists(candidate, ec))
			{
				load_path = candidate.string();
				break;
			}
		}
	}

	std::ostringstream errors;
	mame_options::parse_standard_inis(options, errors);

	for (const auto &change : changes)
	{
		core_options::entry::shared_ptr e = options.get_entry(change.first);
		if (e)
			e->set_value(std::string(change.second), OPTION_PRIORITY_HIGH);
	}

	// Prefer the file we loaded; otherwise create and use the first configured
	// ini directory (the directory may not exist yet, so create it like the
	// per-machine writer does).
	std::string path = load_path;
	if (path.empty())
	{
		std::string dir;
		if (options.value("inipath"))
		{
			std::string const inipath = options.value("inipath");
			std::string::size_type const sep = inipath.find(';');
			dir = (sep == std::string::npos) ? inipath : inipath.substr(0, sep);
		}
		if (dir.empty())
			dir = ".";
		std::error_code ec;
		std::filesystem::create_directories(dir, ec);
		path = dir + "/mame.ini";
	}
	else
	{
		std::error_code ec;
		std::filesystem::path const parent = std::filesystem::path(path).parent_path();
		if (!parent.empty())
			std::filesystem::create_directories(parent, ec);
	}

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (!file.is_open())
		return false;
	file << options.output_ini();
	bool const ok = file.good();
	file.close();

	if (out_path)
		*out_path = path;
	return ok;
}


namespace {

// Turn a parsed core_options into the grouped, GUI-friendly representation
// shared by the global and per-game option readers.
std::vector<qtui_option_group> groups_from_options(const core_options &options)
{
	std::vector<qtui_option_group> groups;
	qtui_option_group *current = nullptr;
	for (const auto &entry : options.entries())
	{
		if (entry->type() == core_options::option_type::HEADER)
		{
			groups.emplace_back();
			current = &groups.back();
			current->header = entry->description() ? entry->description() : "";
			continue;
		}

		int const mapped = qtui_map_option_type(entry->type());
		if (mapped < 0 || entry->names().empty())
			continue;
		if (!entry->name().empty() && entry->name().front() == '<')
			continue;

		if (!current)
		{
			groups.emplace_back();
			current = &groups.back();
		}

		qtui_option opt;
		opt.name = entry->name();
		opt.description = entry->description() ? entry->description() : "";
		opt.value = entry->value() ? entry->value() : "";
		opt.minimum = entry->minimum() ? entry->minimum() : "";
		opt.maximum = entry->maximum() ? entry->maximum() : "";
		opt.type = mapped;
		current->options.push_back(std::move(opt));
	}
	return groups;
}

// First directory listed in the "inipath" option (where per-machine and the
// global ini are written), or "." if none is configured.
std::string first_inipath_dir(const core_options &options)
{
	std::string dir;
	if (options.value("inipath"))
	{
		std::string const inipath = options.value("inipath");
		std::string::size_type const sep = inipath.find(';');
		dir = (sep == std::string::npos) ? inipath : inipath.substr(0, sep);
	}
	return dir.empty() ? std::string(".") : dir;
}

} // anonymous namespace


std::vector<qtui_option_group> qtui_read_game_options(
		const std::string &system,
		std::vector<std::string> *overridden)
{
	core_guard guard;

	int const index = driver_list::find(system.c_str());
	if (index < 0)
		return {};
	const game_driver &driver = driver_list::driver(index);

	qt_options options;
	std::ostringstream errors;
	mame_options::parse_standard_inis(options, errors, &driver);

	std::vector<qtui_option_group> groups = groups_from_options(options);

	// Report which options the machine's own ini currently overrides.
	if (overridden)
	{
		overridden->clear();
		std::string const path = first_inipath_dir(options) + "/" + system + ".ini";
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line))
		{
			std::size_t const start = line.find_first_not_of(" \t");
			if (start == std::string::npos || line[start] == '#')
				continue;
			std::size_t const end = line.find_first_of(" \t", start);
			overridden->push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
		}
	}

	return groups;
}


bool qtui_write_game_options(
		const std::string &system,
		const std::vector<std::pair<std::string, std::string>> &changes,
		std::string *out_path)
{
	core_guard guard;

	if (driver_list::find(system.c_str()) < 0)
		return false;

	qt_options options;
	std::ostringstream errors;
	mame_options::parse_standard_inis(options, errors);
	std::string const dir = first_inipath_dir(options);
	std::string const path = dir + "/" + system + ".ini";

	// The ini directory may not exist yet (per-machine inis are optional).
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	// Merge the requested changes into the existing per-machine ini, preserving
	// its other lines (comments, blank lines, and unrelated overrides).  This
	// keeps the file minimal: it holds only the options that differ for this
	// machine, layered last over the standard ini hierarchy.
	std::unordered_map<std::string, std::string> pending;
	for (const auto &c : changes)
		pending[c.first] = c.second;

	auto format_line = [] (const std::string &name, const std::string &value) {
		std::string line = name;
		if (line.size() < 25)
			line.append(25 - line.size(), ' ');
		line.push_back(' ');
		line += value;
		return line;
	};

	std::vector<std::string> out;
	std::ifstream in(path);
	if (in.is_open())
	{
		std::string line;
		while (std::getline(in, line))
		{
			std::size_t const start = line.find_first_not_of(" \t");
			if (start != std::string::npos && line[start] != '#')
			{
				std::size_t const end = line.find_first_of(" \t", start);
				std::string const key = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
				auto it = pending.find(key);
				if (it != pending.end())
				{
					out.push_back(format_line(key, it->second));
					pending.erase(it);
					continue;
				}
			}
			out.push_back(line);
		}
		in.close();
	}

	// Append any changes that were not already present.
	for (const auto &c : changes)
	{
		auto it = pending.find(c.first);
		if (it != pending.end())
		{
			out.push_back(format_line(c.first, c.second));
			pending.erase(it);
		}
	}

	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (!file.is_open())
		return false;
	for (const std::string &line : out)
		file << line << '\n';
	bool const ok = file.good();
	file.close();

	if (out_path)
		*out_path = path;
	return ok;
}


namespace {

// A minimal index over a *standard* (non-ZIP64) zip: the central directory is
// parsed once into a name -> entry map for O(1) lookups, with the file kept
// open.  This sidesteps the linear directory scan in util::archive_file, which
// is far too slow for per-row icon lookups in a 40k-entry archive.  ZIP64 and
// 7z archives fall back to util::archive_file.

inline std::uint16_t rd16(const unsigned char *p) { return std::uint16_t(p[0] | (p[1] << 8)); }
inline std::uint32_t rd32(const unsigned char *p)
{ return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }

struct zip_entry { std::uint32_t lho, csize, usize; std::uint16_t method; };

struct zip_index
{
	std::ifstream file;
	std::unordered_map<std::string, zip_entry> entries;
	bool valid = false;
};

std::unordered_map<std::string, std::shared_ptr<zip_index>> g_zip_cache;   // guarded by g_asset_mutex
// Serialises asset reads + the zip cache across the artwork/icon worker threads
// and the UI-thread existence probe (qtui_asset_exists).
std::mutex g_asset_mutex;

std::shared_ptr<zip_index> get_zip_index(const std::string &path)
{
	auto cached = g_zip_cache.find(path);
	if (cached != g_zip_cache.end())
		return cached->second;

	auto idx = std::make_shared<zip_index>();
	g_zip_cache.emplace(path, idx);   // cache the result (even if invalid) so we don't retry

	idx->file.open(path, std::ios::binary);
	if (!idx->file.is_open())
		return idx;

	idx->file.seekg(0, std::ios::end);
	std::streamoff const size = idx->file.tellg();
	std::streamoff const window = std::min<std::streamoff>(size, 65557);   // max EOCD + comment
	std::vector<unsigned char> tail(static_cast<std::size_t>(window));
	idx->file.seekg(size - window);
	idx->file.read(reinterpret_cast<char *>(tail.data()), window);
	if (!idx->file)
		return idx;

	std::streamoff eocd = -1;
	for (std::streamoff i = window - 22; i >= 0; --i)
		if (tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06)
			{ eocd = i; break; }
	if (eocd < 0)
		return idx;

	const unsigned char *e = tail.data() + eocd;
	std::uint16_t const total = rd16(e + 10);
	std::uint32_t const cdSize = rd32(e + 12);
	std::uint32_t const cdOffset = rd32(e + 16);
	if (total == 0xffff || cdSize == 0xffffffffu || cdOffset == 0xffffffffu)
		return idx;   // ZIP64 - leave invalid, caller falls back

	std::vector<unsigned char> cd(cdSize);
	idx->file.clear();
	idx->file.seekg(cdOffset);
	idx->file.read(reinterpret_cast<char *>(cd.data()), cdSize);
	if (!idx->file)
		return idx;

	std::size_t pos = 0;
	for (std::uint16_t i = 0; i < total; ++i)
	{
		if (pos + 46 > cd.size())
			break;
		const unsigned char *h = cd.data() + pos;
		if (!(h[0] == 0x50 && h[1] == 0x4b && h[2] == 0x01 && h[3] == 0x02))
			break;
		std::uint16_t const namelen = rd16(h + 28);
		std::uint16_t const extralen = rd16(h + 30);
		std::uint16_t const commentlen = rd16(h + 32);
		if (pos + 46 + namelen > cd.size())
			break;
		std::string name(reinterpret_cast<const char *>(h + 46), namelen);
		idx->entries.emplace(std::move(name),
				zip_entry{ rd32(h + 42), rd32(h + 20), rd32(h + 24), rd16(h + 10) });
		pos += std::size_t(46) + namelen + extralen + commentlen;
	}
	idx->valid = true;
	return idx;
}

std::vector<std::uint8_t> zip_extract(zip_index &idx, const std::string &entry)
{
	std::vector<std::uint8_t> out;
	auto it = idx.entries.find(entry);
	if (it == idx.entries.end())
		return out;
	const zip_entry &z = it->second;
	if (z.usize == 0 || z.usize > (256u << 20))
		return out;

	unsigned char lh[30];
	idx.file.clear();
	idx.file.seekg(z.lho);
	idx.file.read(reinterpret_cast<char *>(lh), 30);
	if (!idx.file || !(lh[0] == 0x50 && lh[1] == 0x4b && lh[2] == 0x03 && lh[3] == 0x04))
		return out;
	std::streamoff const dataoff = std::streamoff(z.lho) + 30 + rd16(lh + 26) + rd16(lh + 28);

	std::vector<unsigned char> comp(z.csize);
	idx.file.clear();
	idx.file.seekg(dataoff);
	idx.file.read(reinterpret_cast<char *>(comp.data()), z.csize);
	if (!idx.file)
		return out;

	if (z.method == 0)   // stored
	{
		out.assign(comp.begin(), comp.end());
		return out;
	}
	if (z.method != 8)   // only deflate supported here
		return out;

	out.resize(z.usize);
	z_stream strm{};
	if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
	{
		out.clear();
		return out;
	}
	strm.next_in = comp.data();
	strm.avail_in = uInt(z.csize);
	strm.next_out = out.data();
	strm.avail_out = uInt(z.usize);
	int const r = inflate(&strm, Z_FINISH);
	inflateEnd(&strm);
	if (r != Z_STREAM_END)
		out.clear();
	return out;
}

} // anonymous namespace


std::vector<std::uint8_t> qtui_load_asset(const std::string &path, const std::string &entry)
{
	std::vector<std::uint8_t> out;
	if (path.empty() || entry.empty())
		return out;

	// Serialise asset reads (artwork + icon worker threads) so concurrent
	// access to MAME's archive cache is safe.
	std::lock_guard<std::mutex> lk(g_asset_mutex);

	std::error_code ec;
	if (std::filesystem::is_directory(path, ec))
	{
		// Plain directory: read <path>/<entry> directly.
		std::filesystem::path const file = std::filesystem::path(path) / entry;
		std::ifstream in(file, std::ios::binary | std::ios::ate);
		if (in.is_open())
		{
			std::streamsize const size = in.tellg();
			if (size > 0)
			{
				out.resize(std::size_t(size));
				in.seekg(0);
				in.read(reinterpret_cast<char *>(out.data()), size);
				if (!in)
					out.clear();
			}
		}
		return out;
	}

	std::string const lower = path.size() >= 3 ? path.substr(path.size() - 3) : std::string();
	bool const sevenZip = (lower == ".7z" || lower == ".7Z");

	// Fast path: a standard zip is indexed once and read directly.
	if (!sevenZip)
	{
		std::shared_ptr<zip_index> idx = get_zip_index(path);
		if (idx->valid)
			return zip_extract(*idx, entry);
		// Not a standard zip (e.g. ZIP64) - fall back to util::archive_file.
	}

	// Fallback: util::archive_file (handles ZIP64 and 7z).
	util::archive_file::ptr archive;
	std::error_condition const err = sevenZip
			? util::archive_file::open_7z(path, archive)
			: util::archive_file::open_zip(path, archive);
	if (err || !archive)
		return out;

	if (archive->search(entry, false) < 0)
		return out;

	std::uint64_t const length = archive->current_uncompressed_length();
	if (length == 0 || length > (256u << 20))   // sanity cap at 256 MiB
		return out;

	out.resize(std::size_t(length));
	if (archive->decompress(out.data(), out.size()))
		out.clear();
	return out;
}

bool qtui_asset_exists(const std::string &path, const std::string &entry)
{
	if (path.empty() || entry.empty())
		return false;

	std::lock_guard<std::mutex> lk(g_asset_mutex);

	std::error_code ec;
	if (std::filesystem::is_directory(path, ec))
	{
		// Plain directory: a non-empty regular file <path>/<entry>.
		std::filesystem::path const file = std::filesystem::path(path) / entry;
		auto const status = std::filesystem::status(file, ec);
		if (ec || !std::filesystem::is_regular_file(status))
			return false;
		std::uintmax_t const size = std::filesystem::file_size(file, ec);
		return !ec && size > 0;
	}

	std::string const lower = path.size() >= 3 ? path.substr(path.size() - 3) : std::string();
	bool const sevenZip = (lower == ".7z" || lower == ".7Z");

	// Fast path: look the member up in the cached zip index (no extraction).
	if (!sevenZip)
	{
		std::shared_ptr<zip_index> idx = get_zip_index(path);
		if (idx->valid)
		{
			auto it = idx->entries.find(entry);
			return it != idx->entries.end() && it->second.usize > 0;
		}
		// Not a standard zip (e.g. ZIP64) - fall through to util::archive_file.
	}

	// Fallback: util::archive_file member search (handles ZIP64 and 7z).
	util::archive_file::ptr archive;
	std::error_condition const err = sevenZip
			? util::archive_file::open_7z(path, archive)
			: util::archive_file::open_zip(path, archive);
	if (err || !archive)
		return false;
	return archive->search(entry, false) >= 0;
}


std::string qtui_parent_of(const std::string &system)
{
	int const index = driver_list::find(system.c_str());
	if (index < 0)
		return {};
	const char *parent = driver_list::driver(index).parent;
	if (parent && parent[0] && std::strcmp(parent, "0") != 0)
		return parent;
	return {};
}


void qtui_audit_all(
		const std::function<void (const std::string &, int)> &progress,
		const std::atomic<bool> &cancel)
{
	// One options set / enumerator for the whole sweep; the enumerator caches
	// machine configurations as it advances.
	qt_options options;
	{
		core_guard guard;
		std::ostringstream errors;
		mame_options::parse_standard_inis(options, errors);
	}

	driver_enumerator drivlist(options);
	media_auditor auditor(drivlist);

	while (!cancel.load(std::memory_order_relaxed))
	{
		std::string name;
		int status = QTUI_AVAIL_UNKNOWN;

		{
			// Hold the core only for the per-driver work, releasing between
			// systems so on-demand software enumeration stays responsive.
			core_guard guard;
			if (!drivlist.next())
				break;

			name = drivlist.driver().name;
			try
			{
				switch (auditor.audit_media(AUDIT_VALIDATE_FAST))
				{
				case media_auditor::CORRECT:
				case media_auditor::BEST_AVAILABLE:
				case media_auditor::NONE_NEEDED:
					status = QTUI_AVAIL_AVAILABLE;
					break;
				case media_auditor::INCORRECT:
				case media_auditor::NOTFOUND:
				default:
					status = QTUI_AVAIL_UNAVAILABLE;
					break;
				}
			}
			catch (...)
			{
				status = QTUI_AVAIL_UNKNOWN;
			}
		}

		// Report every system (even UNKNOWN) so progress can reach the total;
		// the caller ignores UNKNOWN for filtering purposes.
		progress(name, status);
	}
}

void qtui_audit_all_software(
		const std::function<void (const std::string &, const std::vector<int> &, bool)> &on_system,
		const std::atomic<bool> &cancel)
{
	qt_options options;
	{
		core_guard guard;
		std::ostringstream errors;
		mame_options::parse_standard_inis(options, errors);
	}

	driver_enumerator drivlist(options);

	while (!cancel.load(std::memory_order_relaxed))
	{
		std::string name;
		std::vector<int> availability;
		bool has_software = false;

		{
			// Hold the core only for the per-driver work, releasing between
			// systems so on-demand software enumeration stays responsive.
			core_guard guard;
			if (!drivlist.next())
				break;

			name = drivlist.driver().name;
			try
			{
				device_t &root = drivlist.config()->root_device();
				media_auditor auditor(drivlist);
				for (software_list_device &swlistdev : software_list_device_enumerator(root))
				{
					for (const software_info &info : swlistdev.get_info())
					{
						has_software = true;
						int avail;
						switch (auditor.audit_software(swlistdev, info, AUDIT_VALIDATE_FAST))
						{
						case media_auditor::CORRECT:
						case media_auditor::BEST_AVAILABLE:
						case media_auditor::NONE_NEEDED:
							avail = QTUI_AVAIL_AVAILABLE;
							break;
						default:
							avail = QTUI_AVAIL_UNAVAILABLE;
							break;
						}
						availability.push_back(avail);

						if (cancel.load(std::memory_order_relaxed))
							return;
					}
				}
			}
			catch (...)
			{
				availability.clear();
				has_software = false;
			}
		}

		// Reported for every system (even those without software) so the
		// caller's progress can reach the total.
		on_system(name, availability, has_software);
	}
}
