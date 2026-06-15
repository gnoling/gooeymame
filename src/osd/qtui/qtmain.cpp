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

	osd::qtui::MainWindow window;
	window.show();

	return app.exec();
}
