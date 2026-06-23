// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  emulator.cpp - qtui emulation entry points (no Qt dependencies)
//
//  Reuses the SDL OSD as the emulation backend.  The body mirrors
//  src/osd/sdl/sdlmain.cpp, factored into functions so it can be driven
//  either by command-line passthrough or by the Qt GUI.
//
//============================================================

#include "emulator.h"

// OSD headers
#include "osdsdl.h"
#include "window.h"   // sdl_window_info (live fullscreen toggle for own-window embed)
#include "modules/lib/osdlib.h"
#include "modules/diagnostics/diagnostics_module.h"

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
#include "romload.h"       // system-BIOS rom entries (BIOS Selection menu)
#include "imagedev/cassette.h"   // cassette_image_device (Tape Control menu)
#include "machine/bcreader.h"    // barcode_reader_device (Barcode Reader menu)
#include "cheat.h"         // cheat_manager / cheat_entry (Cheat menu)
#include "ui/ui.h"
#include "ui/info.h"       // machine_info::game_info_string()/warnings_string()
#include "ui/menuitem.h"   // ui::menu_item (slider list entries)
#include "ui/slider.h"     // slider_state + SLIDER_NOCHANGE (live adjustments)
#include "bookkeeping.h"   // bookkeeping_manager (coin counters, tickets)

#include "drivenum.h"
#include "rendlay.h"   // layout_view visibility toggles (bezel/artwork)
#include "softlist_dev.h"
#include "softlist.h"

#include "corestr.h"
#include "unzip.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>     // SEEK_SET / SEEK_CUR (cassette seek)
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include "osdepend.h"
#include "strconv.h"

#include <clocale>
#include <locale.h>
#include <string>

#include "embedsession.h"

#include <SDL2/SDL.h>

#ifdef SDLMAME_UNIX
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID))
#ifndef SDLMAME_HAIKU
#include <fontconfig/fontconfig.h>
#endif
#endif
#endif

#if !defined(SDLMAME_WIN32)
#include <unistd.h>
#endif


//============================================================
//  Global variables
//
//  sdl_entered_debugger is normally defined in sdlmain.cpp, which we do
//  not compile (qtmain.cpp supplies the program entry point instead).  The
//  Qt debugger module references it, so define it here.
//============================================================

#if defined(SDLMAME_UNIX) || defined(SDLMAME_WIN32)
int sdl_entered_debugger;
#endif


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


int qtui_run_args(std::vector<std::string> &args)
{
	int res = 0;

	// MAME assumes the "C" locale: its option/ini parsing and number
	// formatting break under a UTF-8 locale (parse_ini_file() spins
	// indefinitely on plugin.ini).  A command-line MAME process never
	// changes the locale, but constructing a QApplication calls
	// setlocale(LC_ALL, "") and switches the whole process to the user's
	// locale.  Force "C" for the duration of the emulator run and restore
	// the previous locale afterwards so the Qt GUI is unaffected.
	std::string const saved_locale = std::setlocale(LC_ALL, nullptr);
	std::setlocale(LC_ALL, "C");

#ifdef SDLMAME_UNIX
	sdl_entered_debugger = 0;
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_HAIKU)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID))
	FcInit();
#endif
#endif

	{
		sdl_options options;
		sdl_osd_interface osd(options);
		osd.register_options();
		res = emulator_info::start_frontend(options, osd, args);
	}

#ifdef SDLMAME_UNIX
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_HAIKU)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID))
	if (!sdl_entered_debugger)
		FcFini();
#endif
#endif

	std::setlocale(LC_ALL, saved_locale.c_str());

	return res;
}


int qtui_run_emulation(int argc, char **argv)
{
	std::vector<std::string> args = osd_get_command_line(argc, argv);
	return qtui_run_args(args);
}


int qtui_run_system(const std::string &system)
{
	std::vector<std::string> args{ "mame", system };
	return qtui_run_args(args);
}


int qtui_run_software(const std::string &system, const std::string &software)
{
	std::vector<std::string> args{ "mame", system, software };
	return qtui_run_args(args);
}


//============================================================
//  Embedded in-process emulation
//
//  A qtui_osd_interface subclass captures the running_machine and, each frame
//  (in update(), which runs on the emulation thread), drains the UI's command
//  queue and applies it directly to the machine.  This is the faithful analog
//  of NEWUI, whose native menu is serviced by the Windows OSD's message pump
//  (src/osd/winui/newui.cpp invoke_command()).
//============================================================

namespace {

class qtui_osd_interface : public sdl_osd_interface
{
public:
	qtui_osd_interface(sdl_options &options, osd::qtui::EmbedSession &session) :
		sdl_osd_interface(options),
		m_session(session)
	{
	}

	virtual void init(running_machine &machine) override
	{
		sdl_osd_interface::init(machine);
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

	virtual void update(bool skip_redraw) override
	{
		drain_commands();
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
				refresh_info();
				refresh_bios();
				refresh_tape();
				refresh_network();
				refresh_barcode();
				refresh_crosshairs();
				refresh_cheats();
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
		}
		sdl_osd_interface::update(skip_redraw);
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
			// Only posted in own-window mode (no -attach_window); the GUI handles
			// fullscreen for attached surfaces, where destroy+recreate is unsafe.
			// Toggle the live SDL window between windowed and fullscreen.
			for (const auto &win : window_list())
			{
				if (osd_window *const w = win.get())
				{
					static_cast<sdl_window_info *>(w)->toggle_full_screen();
					break;
				}
			}
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
			// live on the next frame, no reset.  Re-set the SDL render hint too so
			// the SDL2/SDL3 accelerated renderers pick it up on rebuild.
			video_config.filter = a.ival ? 1 : 0;
			SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, video_config.filter ? "1" : "0");
			for (const auto &win : window_list())
				if (osd_window *const w = win.get())
					static_cast<sdl_window_info *>(w)->notify_changed();
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

	osd::qtui::EmbedSession &m_session;
	running_machine *m_machine = nullptr;
	unsigned m_imageRefreshTick = 0;
	bool m_capsInit = false;   // capabilities published on the first update() frame
};

} // anonymous namespace


bool qtui_renderer_needs_gl(const std::string &system)
{
	int const idx = driver_list::find(system.c_str());
	if (idx < 0)
		return true;

	// Resolve the effective -video value through the standard ini hierarchy.
	// core_guard serialises core access and forces the "C" locale the ini
	// parser needs, so this is safe to call from the GUI thread too.
	core_guard guard;
	try
	{
		sdl_options probe;
		std::ostringstream errors;
		mame_options::parse_standard_inis(probe, errors, &driver_list::driver(idx));
		std::string const v = probe.video();
		return (v != "soft") && (v != "none");
	}
	catch (...)
	{
		return true;
	}
}


int qtui_run_embedded(
		const std::string &system,
		const std::string &software,
		unsigned long long attach_window_id,
		osd::qtui::EmbedSession &session)
{
	int res = 0;

	// Force the "C" locale for this thread only: the MAME ini/number parsers
	// require it, but the Qt GUI thread keeps running in the user's locale (an
	// embedded run shares the process with the live GUI).  See qtui_run_args().
	// Unix uses POSIX uselocale; Windows uses per-thread CRT locale (the
	// own-window embed mode runs this path on Windows too).
#ifdef SDLMAME_UNIX
	locale_t const cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t const prev = cloc ? uselocale(cloc) : (locale_t)0;
#elif defined(_WIN32)
	_configthreadlocale(_ENABLE_PER_THREAD_LOCALE);
	std::string const win_prev_locale = std::setlocale(LC_ALL, nullptr);
	std::setlocale(LC_ALL, "C");
#endif

#ifdef SDLMAME_UNIX
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_HAIKU)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID))
	FcInit();
#endif
#endif

	std::vector<std::string> args{ "mame", system };
	if (!software.empty())
		args.push_back(software);
	args.push_back("-window");
	// attach_window_id == 0 means "no attach": MAME opens its OWN window (the
	// separate-window-with-live-controls mode, which also works on Windows since
	// it avoids the foreign-window keyboard-focus problem of SDL_CreateWindowFrom).
	if (attach_window_id != 0)
	{
		args.push_back("-attach_window");
		args.push_back(std::to_string(attach_window_id));

		// MAME's attach path calls SDL_CreateWindowFrom() without flagging the
		// window for OpenGL, so the GL-based renderers (opengl/accel) abort with
		// "the specified window isn't an OpenGL window".  SDL's foreign-window hint
		// fixes that, BUT it must be set only when the renderer actually needs GL:
		// the software renderer can't get a window surface from a GL-flagged window,
		// and requesting both GL and Vulkan flags fails outright ("Vulkan and OpenGL
		// not supported on same window").  So enable the GL flag for everything
		// except soft/none, and never request the Vulkan flag alongside it.  Only
		// relevant for a foreign window — MAME's own window is created normally.
		SDL_SetHintWithPriority(SDL_HINT_VIDEO_FOREIGN_WINDOW_OPENGL,
				qtui_renderer_needs_gl(system) ? "1" : "0", SDL_HINT_OVERRIDE);
	}

	{
		sdl_options options;
		qtui_osd_interface osd(options, session);
		osd.register_options();
		res = emulator_info::start_frontend(options, osd, args);
	}

	session.running.store(false);

#ifdef SDLMAME_UNIX
#if (!defined(SDLMAME_MACOSX)) && (!defined(SDLMAME_HAIKU)) && (!defined(SDLMAME_EMSCRIPTEN)) && (!defined(SDLMAME_ANDROID))
	FcFini();
#endif
#endif

#ifdef SDLMAME_UNIX
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
		sdl_options options;
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

	sdl_options options;
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

	sdl_options options;

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

	sdl_options options;
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

	sdl_options options;
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
	sdl_options options;
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
	sdl_options options;
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
