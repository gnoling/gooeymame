// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  embedhost.h - native host widget for embedded MAME gameplay
//
//  Provides a real native (X11) window that MAME's SDL OSD attaches to via
//  -attach_window <id>, plus a centred status overlay and the QProcess
//  lifecycle for the child-process embed mode.  In-process embedding (where
//  the emulation runs on a worker thread driven by QtuiEmbedBridge) reuses the
//  same surface XID; see mainwindow.cpp / emulator.cpp.
//
//============================================================
#ifndef MAME_OSD_QTUI_EMBEDHOST_H
#define MAME_OSD_QTUI_EMBEDHOST_H

#pragma once

#include <QtWidgets/QWidget>

class QLabel;
class QProcess;

namespace osd::qtui {

class NativeSurface;

//============================================================
//  EmbedHost - the play page shown in the central QStackedWidget.
//============================================================
class EmbedHost : public QWidget
{
	Q_OBJECT

public:
	explicit EmbedHost(QWidget *parent = nullptr);
	~EmbedHost() override;

	// X11 XID (window handle) MAME attaches its renderer to.
	unsigned long long surfaceId() const;

	// Force X11 keyboard input focus onto the embedded surface.  The attached
	// SDL window doesn't reliably hold focus after a re-attach (a second
	// in-process launch reuses this same window), so the launch path calls this
	// a few times once the game window is mapped.  No-op off X11.
	void nudgeFocus();

	// Status overlay text (e.g. "Launching…", "Quit MAME to return").
	void setStatus(const QString &text);
	void showOverlay(bool visible);

	// Child-process embed: re-exec this binary with the given CLI args
	// (-attach_window is appended automatically).  When wantsGl is true the
	// SDL foreign-window OpenGL hint is passed via the environment so the
	// child can make a GL context on our window.  Emits finished() on exit.
	void startChildProcess(const QStringList &baseArgs, bool wantsGl);
	bool isChildRunning() const;
	void stopChild();

signals:
	void finished(int exitCode);

private:
	NativeSurface *m_surface = nullptr;
	QLabel *m_overlay = nullptr;
	QProcess *m_process = nullptr;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_EMBEDHOST_H
