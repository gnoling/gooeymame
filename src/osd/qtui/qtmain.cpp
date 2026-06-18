// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtmain.cpp - program entry point for the qtui (Qt front-end) OSD.
//
//  Mirrors MAMEUI's integrated model (src/osd/winui/mui_main.cpp):
//    * invoked with arguments  -> behave exactly like SDLMAME and run the
//                                  requested system on the command line;
//    * invoked with no arguments -> launch the Qt browser front-end.
//
//  Keeping this translation unit Qt-only (the emulator/SDL world lives in
//  emulator.cpp) avoids any macro clashes between Qt and the MAME headers.
//
//============================================================

#include "emulator.h"
#include "mainwindow.h"

#include <QtCore/QSettings>
#include <QtWidgets/QApplication>


int main(int argc, char *argv[])
{
	qtui_init_process();

	// Command-line passthrough: any arguments mean "run the emulator",
	// just like SDLMAME.
	if (argc > 1)
		return qtui_run_emulation(argc, argv);

	// No arguments: show the Qt front-end.
	QApplication app(argc, argv);
	QApplication::setApplicationName("MAMEUI");
	QApplication::setOrganizationName("MAMEUI");

#ifdef _WIN32
	// Store GUI settings in an INI file rather than the Windows registry, so the
	// config is inspectable/portable and consistent with the .conf used on Unix
	// (location: %APPDATA%\MAMEUI\MAMEUI.ini).  Must precede any QSettings use.
	QSettings::setDefaultFormat(QSettings::IniFormat);
#endif

	osd::qtui::MainWindow window;
	window.show();

	return app.exec();
}
