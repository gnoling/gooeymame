// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtmain.cpp - program entry point for the qtui (Qt front-end) OSD.
//
//  Single integrated binary:
//    * invoked with arguments  -> behave like a command-line MAME and run the
//                                  requested system;
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
#include <QtCore/QStringList>

#include <cstdio>
#include <cstring>
#include <QtCore/QtGlobal>
#include <QtGui/QIcon>
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

	// A leading "--gooey <system> [software]" launches straight into an embedded play window
	// (the game inside its own window with the in-game menu bar, no browser).  Any other
	// arguments pass through to the emulator exactly like SDLMAME.
	QString gooeySystem, gooeySoftware, gooeyRenderer, gooeyBackend, gooeyShader;
	bool gooeyMode = false;
	if (argc > 1 && std::strcmp(argv[1], "--gooey") == 0)
	{
		if (argc < 3)
		{
			std::fprintf(stderr,
					"Usage: %s --gooey <system> [software] [options]\n"
					"  --opengl                 use the OpenGL renderer\n"
					"  --bgfx                   use the BGFX renderer (shader chains)\n"
					"  --bgfx-backend <b>       auto | opengl | vulkan (implies --bgfx)\n"
					"  --shader <name>          apply a BGFX effect chain, e.g. crt-geom (implies --bgfx)\n",
					argv[0]);
			return 1;
		}
		gooeyMode = true;
		gooeySystem = QString::fromLocal8Bit(argv[2]);
		for (int i = 3; i < argc; ++i)
		{
			char const *const a = argv[i];
			if (std::strcmp(a, "--bgfx") == 0)
				gooeyRenderer = QStringLiteral("bgfx");
			else if (std::strcmp(a, "--opengl") == 0)
				gooeyRenderer = QStringLiteral("opengl");
			else if (std::strcmp(a, "--bgfx-backend") == 0 && (i + 1) < argc)
				gooeyBackend = QString::fromLocal8Bit(argv[++i]);
			else if (std::strcmp(a, "--shader") == 0 && (i + 1) < argc)
				gooeyShader = QString::fromLocal8Bit(argv[++i]);
			else if (a[0] != '-' && gooeySoftware.isEmpty())
				gooeySoftware = QString::fromLocal8Bit(a);   // first bare arg = software
		}
	}

	// Every entry point now needs Qt: a plain CLI passthrough (`./mame pacman`)
	// runs through the Qt-native OSD on a worker thread, so it builds a
	// QApplication too (and a render window, lazily, only if the run uses video).
#ifdef _WIN32
	// Filter the benign DPI-awareness warning the Qt platform plugin emits at
	// startup (see qtui_message_filter).  Installed before QApplication so it
	// catches the warning raised during platform initialisation.
	g_prev_message_handler = qInstallMessageHandler(qtui_message_filter);
#endif

	QApplication app(argc, argv);
	QApplication::setApplicationName("GooeyMAME");
	QApplication::setOrganizationName("GooeyMAME");

	// Application icon (window title bar, taskbar, alt-tab).  The resource embeds
	// the artwork at every size so Qt picks the best match per surface; on Windows
	// the taskbar also uses the .ico compiled into the executable.  Q_INIT_RESOURCE
	// forces the linker to keep the resource object (it lives in a static lib).
	Q_INIT_RESOURCE(gooeymame);
	{
		QIcon appIcon;
		for (int const sz : { 16, 24, 32, 48, 64, 128, 256, 512 })
			appIcon.addFile(QStringLiteral(":/gooeymame/icon%1").arg(sz), QSize(sz, sz));
		QApplication::setWindowIcon(appIcon);
	}

#ifdef _WIN32
	// Store GUI settings in an INI file rather than the Windows registry, so the
	// config is inspectable/portable and consistent with the .conf used on Unix
	// (location: %APPDATA%\GooeyMAME\GooeyMAME.ini).  Must precede any QSettings use.
	QSettings::setDefaultFormat(QSettings::IniFormat);
#endif

	// Apply the user's chosen Qt widget style and colour scheme (if any) before
	// building the UI.  Style first: it resets the palette the scheme rides on.
	osd::qtui::applyPersistedStyle();
	osd::qtui::applyPersistedColorScheme();

	osd::qtui::MainWindow window;
	if (gooeyMode)
	{
		window.startStandaloneEmbedded(gooeySystem, gooeySoftware, gooeyRenderer, gooeyBackend, gooeyShader);
	}
	else if (argc > 1)
	{
		// CLI passthrough through the Qt-native OSD (browser never shown).  Decode
		// the command line after QApplication has stripped any Qt-specific options.
		std::vector<std::string> args = qtui_command_line(argc, argv);
		window.runCliPassthrough(args);
	}
	else
	{
		window.show();
	}

	return app.exec();
}
