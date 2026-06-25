// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  emulator.h - qtui emulation entry points
//
//  Thin, Qt-free wrapper around the MAME core + SDL OSD backend so the
//  Qt front-end can launch machines in-process (modal launch) and so the
//  qtui executable can pass command-line invocations straight through to
//  the emulator, exactly like SDLMAME.
//
//============================================================
#ifndef MAME_OSD_QTUI_EMULATOR_H
#define MAME_OSD_QTUI_EMULATOR_H

#pragma once

#include "embedsession.h"
#include "qtembedtarget.h"   // osd::qtui::QtEmbedTarget (Qt-free)

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// One-time process initialisation (stdio buffering, crash diagnostics).
// Safe to call once at startup regardless of GUI vs CLI mode.
void qtui_init_process();

// Run the emulator with raw command-line arguments (argv[0] is the program
// name).  Used for command-line passthrough.  Returns the emulator exit code.
int qtui_run_emulation(int argc, char **argv);

// Run the emulator with an explicit argument vector (args[0] is the program
// name, e.g. {"mame", "pacman"}).  Used by the GUI to launch a selected
// system in-process.  Returns the emulator exit code.
int qtui_run_args(std::vector<std::string> &args);

// Launch a single system by short name (e.g. "pacman") in-process, reading
// configured options from the usual .ini files.  Convenience wrapper around
// qtui_run_args() used by the Qt front-end's modal launch.  Returns the
// emulator exit code.
int qtui_run_system(const std::string &system);

// Launch a system with a piece of software from one of its software lists
// (e.g. system "nes", software "smb").  Returns the emulator exit code.
int qtui_run_software(const std::string &system, const std::string &software);

// True if the system's configured -video renderer needs an OpenGL-capable
// window (everything except "soft"/"none").  Used to decide whether to set the
// SDL foreign-window OpenGL hint when embedding.  Safe to call from any thread.
bool qtui_renderer_needs_gl(const std::string &system);

// Run `system` (+ optional `software`, "" for none) embedded into the native
// window `attach_window_id` (an X11 XID), in-process on the CALLING thread.
// Intended to run on a dedicated worker thread so the Qt event loop keeps
// running; `session` is the command/status bridge the UI uses to drive the
// live machine (pause, reset, save state, …).  Returns the emulator exit code.
int qtui_run_embedded(
		const std::string &system,
		const std::string &software,
		unsigned long long attach_window_id,
		osd::qtui::EmbedSession &session);

// Phase 13 (Qt-native OSD): run `system` (+ optional `software`) rendering into
// the QWindow carried by `target`, using a Qt-native OSD window + OpenGL context
// instead of SDL's foreign-window path.  Runs in-process on the CALLING thread
// (intended for a worker thread).  The target must outlive this call.  Returns
// the emulator exit code.
int qtui_run_embedded_native(
		const std::string &system,
		const std::string &software,
		osd::qtui::QtEmbedTarget *target,
		osd::qtui::EmbedSession &session,
		bool useBgfx,
		const std::string &bgfxBackend,    // "" / "auto" = let BGFX choose
		const std::string &soundProvider); // non-SDL audio: pulse/pipewire/portaudio/none

// A single software-list entry, flattened to plain data for the GUI.
struct qtui_software_entry
{
	std::string list;        // software list short name (e.g. "nes")
	std::string shortname;   // software short name (e.g. "smb")
	std::string parent;      // cloneof short name, or "" if this is a parent
	std::string description; // human-readable title
	std::string year;
	std::string publisher;
	int         supported;   // 0 = supported, 1 = partial, 2 = unsupported
	int         availability; // qtui_availability (ROM presence)
};

// Load a system's software lists in two phases, off the UI thread:
//   on_entries - called once with every entry (availability UNKNOWN) as soon
//                as the lists are parsed; this is fast.
//   on_audited - called per entry with (entry index, qtui_availability) as
//                each entry's ROMs are fast-audited; this is the slow part.
// Both callbacks run on the calling thread.  Aborts promptly if *cancel
// becomes true (when non-null).  The audit visits entries in the same order
// as on_entries, so the index lines up with that vector.
void qtui_load_software(
		const std::string &system,
		const std::atomic<bool> *cancel,
		const std::function<void (const std::vector<qtui_software_entry> &)> &on_entries,
		const std::function<void (int, int)> &on_audited);

// Total number of systems in the build (for audit progress reporting).
int qtui_system_count();


//============================================================
//  Emulator options (mame.ini) access
//============================================================

// Editable option kinds (mirrors the meaningful core_options::option_type
// values; HEADER/COMMAND/INVALID are not exposed as editable rows).
enum qtui_option_type
{
	QTUI_OPT_BOOLEAN = 0,
	QTUI_OPT_INTEGER,
	QTUI_OPT_FLOAT,
	QTUI_OPT_STRING,
	QTUI_OPT_PATH,
	QTUI_OPT_MULTIPATH
};

struct qtui_option
{
	std::string name;
	std::string description;
	std::string value;
	std::string default_value;
	std::string minimum;
	std::string maximum;
	int         type;       // qtui_option_type
};

// A group of options under one ini header (e.g. "CORE SEARCH PATH OPTIONS").
struct qtui_option_group
{
	std::string header;
	std::vector<qtui_option> options;
};

// Read all editable emulator options, grouped by ini header, with their
// current values loaded from the standard ini files.
std::vector<qtui_option_group> qtui_read_options();

// Apply name/value changes and write the full mame.ini back out.  Returns
// true on success; *out_path (when non-null) receives the file written.
bool qtui_write_options(
		const std::vector<std::pair<std::string, std::string>> &changes,
		std::string *out_path);

// Read the effective options for one machine (the standard ini hierarchy with
// the machine's own <system>.ini applied).  If `overridden` is non-null it is
// filled with the option names the machine's ini currently sets.
std::vector<qtui_option_group> qtui_read_game_options(
		const std::string &system,
		std::vector<std::string> *overridden = nullptr);

// Merge name/value changes into the per-machine <inipath>/<system>.ini
// (preserving its other lines).  Returns true on success; *out_path receives
// the file written.
bool qtui_write_game_options(
		const std::string &system,
		const std::vector<std::pair<std::string, std::string>> &changes,
		std::string *out_path);


//============================================================
//  Artwork / asset loading (MAME EXTRAs)
//============================================================

// Load an asset (e.g. a PNG) named `entry` from `path`, which may be either a
// directory or a .zip/.7z archive.  Returns the raw file bytes, or an empty
// vector if the path or entry does not exist.
std::vector<std::uint8_t> qtui_load_asset(const std::string &path, const std::string &entry);

// Cheap existence probe for an asset: like qtui_load_asset but only reports
// whether `entry` is present (and non-empty) under `path`, without reading or
// decompressing the data.  Used to hide art tabs that would have no content.
bool qtui_asset_exists(const std::string &path, const std::string &entry);

// Short name of a system's parent/clone source (for artwork fallback), or an
// empty string if it has none.
std::string qtui_parent_of(const std::string &system);

// ROM availability of a system.
enum qtui_availability
{
	QTUI_AVAIL_UNKNOWN = 0,
	QTUI_AVAIL_AVAILABLE = 1,
	QTUI_AVAIL_UNAVAILABLE = 2
};

// Fast-audit every system's ROMs (CRC only), invoking progress(shortname,
// availability) for each one as it is checked.  Intended to run on a worker
// thread; aborts promptly when cancel becomes true.  Internally serialised
// with qtui_enumerate_software() so the two never touch the MAME core at the
// same time.
void qtui_audit_all(
		const std::function<void (const std::string &, int)> &progress,
		const std::atomic<bool> &cancel);

// Fast-audit *software-list* ROMs for every system in one sweep (builds each
// machine configuration, so it is expensive).  on_system is invoked once per
// system with its short name, the per-entry availability (qtui_availability,
// aligned with qtui_load_software()'s order), and whether it has any software.
// Runs on a worker thread; aborts promptly when cancel becomes true.
void qtui_audit_all_software(
		const std::function<void (const std::string &, const std::vector<int> &, bool)> &on_system,
		const std::atomic<bool> &cancel);

#endif // MAME_OSD_QTUI_EMULATOR_H
