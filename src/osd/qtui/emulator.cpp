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
#include "mameopts.h"

#include "drivenum.h"
#include "softlist_dev.h"
#include "softlist.h"

#include "corestr.h"

#include <sstream>

#include "osdepend.h"
#include "strconv.h"

#include <clocale>
#include <string>

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


std::vector<qtui_software_entry> qtui_enumerate_software(const std::string &system)
{
	std::vector<qtui_software_entry> out;

	int const index = driver_list::find(system.c_str());
	if (index < 0)
		return out;

	// MAME parsing assumes the "C" locale; QApplication switches the process
	// to the user's (UTF-8) locale, which breaks ini/XML parsing.  See
	// qtui_run_args() for the full explanation.
	std::string const saved_locale = std::setlocale(LC_ALL, nullptr);
	std::setlocale(LC_ALL, "C");

	try
	{
		const game_driver &driver = driver_list::driver(index);

		// Load the configured paths (hashpath in particular) so the software
		// list definition files can be found.
		sdl_options options;
		std::ostringstream errors;
		mame_options::parse_standard_inis(options, errors, &driver);

		// Build the machine configuration so we can discover its software
		// list devices.
		driver_enumerator drivlist(options, driver);
		drivlist.next();

		for (software_list_device &swlistdev : software_list_device_enumerator(drivlist.config()->root_device()))
		{
			for (const software_info &info : swlistdev.get_info())
			{
				qtui_software_entry entry;
				entry.list = swlistdev.list_name();
				entry.shortname = info.shortname();
				entry.description = info.longname();
				entry.year = info.year();
				entry.publisher = info.publisher();
				entry.supported = int(info.supported());
				out.push_back(std::move(entry));
			}
		}
	}
	catch (...)
	{
		// A malformed software list or machine configuration should not take
		// down the front-end; just report no software.
		out.clear();
	}

	std::setlocale(LC_ALL, saved_locale.c_str());
	return out;
}
