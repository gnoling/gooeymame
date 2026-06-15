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

#include <atomic>
#include <functional>
#include <string>
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

// A single software-list entry, flattened to plain data for the GUI.
struct qtui_software_entry
{
	std::string list;        // software list short name (e.g. "nes")
	std::string shortname;   // software short name (e.g. "smb")
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

#endif // MAME_OSD_QTUI_EMULATOR_H
