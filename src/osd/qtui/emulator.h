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

#endif // MAME_OSD_QTUI_EMULATOR_H
