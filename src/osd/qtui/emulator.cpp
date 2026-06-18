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
#include "modules/lib/osdlib.h"
#include "modules/diagnostics/diagnostics_module.h"

// MAME headers
#include "emu.h"
#include "emuopts.h"
#include "main.h"
#include "mame.h"
#include "mameopts.h"
#include "audit.h"
#include "natkeyboard.h"
#include "ui/ui.h"

#include "drivenum.h"
#include "softlist_dev.h"
#include "softlist.h"

#include "corestr.h"
#include "unzip.h"

#include <zlib.h>

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
	}

	virtual void update(bool skip_redraw) override
	{
		drain_commands();
		if (m_machine)
			m_session.paused.store(m_machine->paused());
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
			break;   // TODO: SDL window fullscreen toggle (deferred; needs OSD window hook)
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
			break;   // TODO: render-target orientation (deferred)
		case EmbedCommand::KeyboardEmulated:
			m.natkeyboard().set_in_use(false);
			break;
		case EmbedCommand::KeyboardNatural:
			m.natkeyboard().set_in_use(true);
			break;
		case EmbedCommand::Paste:
			m.natkeyboard().paste();
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

	osd::qtui::EmbedSession &m_session;
	running_machine *m_machine = nullptr;
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

	// Force the "C" locale for this thread only (uselocale is per-thread,
	// unlike setlocale): the MAME ini/number parsers require it, but the Qt GUI
	// thread keeps running in the user's locale.  See qtui_run_args() for why.
	// uselocale/newlocale are POSIX (not in MinGW's CRT); embedded play is
	// X11-only so this path never runs on Windows, but it must still compile.
#ifdef SDLMAME_UNIX
	locale_t const cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	locale_t const prev = cloc ? uselocale(cloc) : (locale_t)0;
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
	args.push_back("-attach_window");
	args.push_back(std::to_string(attach_window_id));

	// MAME's attach path calls SDL_CreateWindowFrom() without flagging the
	// window for OpenGL, so the GL-based renderers (opengl/accel) abort with
	// "the specified window isn't an OpenGL window".  SDL's foreign-window hint
	// fixes that, BUT it must be set only when the renderer actually needs GL:
	// the software renderer can't get a window surface from a GL-flagged window,
	// and requesting both GL and Vulkan flags fails outright ("Vulkan and OpenGL
	// not supported on same window").  So enable the GL flag for everything
	// except soft/none, and never request the Vulkan flag alongside it.
	SDL_SetHintWithPriority(SDL_HINT_VIDEO_FOREIGN_WINDOW_OPENGL,
			qtui_renderer_needs_gl(system) ? "1" : "0", SDL_HINT_OVERRIDE);

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

std::unordered_map<std::string, std::shared_ptr<zip_index>> g_zip_cache;   // guarded by s_asset_mutex

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
	static std::mutex s_asset_mutex;
	std::lock_guard<std::mutex> lk(s_asset_mutex);

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
