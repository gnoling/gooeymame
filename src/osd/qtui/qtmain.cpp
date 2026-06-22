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
#include <QtCore/QString>
#include <QtCore/QtGlobal>
#include <QtWidgets/QApplication>


#ifdef _WIN32
namespace {

QtMessageHandler g_prev_message_handler = nullptr;

// MAME's executable manifest already declares the process DPI-aware
// (scripts/resources/windows/mame/mame.man, <dpiAware>true</dpiAware>), which
// Windows applies at process creation.  The Qt platform plugin then tries to
// switch the process to Per-Monitor-v2 awareness and fails harmlessly with
// ACCESS_DENIED because the awareness is already locked in.  The app stays
// DPI-aware and renders correctly; this just drops that one noisy startup
// warning while forwarding every other Qt diagnostic untouched.
void qtui_message_filter(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
	if (message.contains(QLatin1String("SetProcessDpiAwarenessContext")))
		return;
	if (g_prev_message_handler)
		g_prev_message_handler(type, context, message);
}

} // anonymous namespace
#endif


int main(int argc, char *argv[])
{
	qtui_init_process();

	// Command-line passthrough: any arguments mean "run the emulator",
	// just like SDLMAME.
	if (argc > 1)
		return qtui_run_emulation(argc, argv);

	// No arguments: show the Qt front-end.
#ifdef _WIN32
	// Filter the benign DPI-awareness warning the Qt platform plugin emits at
	// startup (see qtui_message_filter).  Installed before QApplication so it
	// catches the warning raised during platform initialisation.
	g_prev_message_handler = qInstallMessageHandler(qtui_message_filter);
#endif

	QApplication app(argc, argv);
	QApplication::setApplicationName("MAMEUI");
	QApplication::setOrganizationName("MAMEUI");

#ifdef _WIN32
	// Store GUI settings in an INI file rather than the Windows registry, so the
	// config is inspectable/portable and consistent with the .conf used on Unix
	// (location: %APPDATA%\MAMEUI\MAMEUI.ini).  Must precede any QSettings use.
	QSettings::setDefaultFormat(QSettings::IniFormat);
#endif

	// Apply the user's chosen Qt widget style and colour scheme (if any) before
	// building the UI.  Style first: it resets the palette the scheme rides on.
	osd::qtui::applyPersistedStyle();
	osd::qtui::applyPersistedColorScheme();

	osd::qtui::MainWindow window;
	window.show();

	return app.exec();
}
