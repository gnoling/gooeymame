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
};

// Enumerate the software available to a system across all of its software
// lists.  Builds the system's machine configuration on demand, so this is
// relatively expensive and should be called off the UI thread or debounced.
// Returns an empty vector for systems that have no software lists.
std::vector<qtui_software_entry> qtui_enumerate_software(const std::string &system);

#endif // MAME_OSD_QTUI_EMULATOR_H
