// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  inputmapdialog.h - in-game input remapping dialog for the qtui OSD
//
//  Lists the running machine's remappable inputs (this game's controls +
//  general/UI inputs) with their current bindings, and lets the user remap,
//  default, or clear each one.  Remapping is interactive: it posts a capture
//  command to the emulation thread, which runs MAME's sequence poller and
//  publishes the partial/final sequence back; this dialog shows a "press a
//  control" banner and refreshes when the capture finishes.  Joystick input is
//  captured automatically (the SDL module polls the device directly); keyboard
//  and mouse input is forwarded to the Qt input bus while capturing so it
//  reaches the poller even though this dialog holds focus.
//
//============================================================
#ifndef MAME_OSD_QTUI_INPUTMAPDIALOG_H
#define MAME_OSD_QTUI_INPUTMAPDIALOG_H

#pragma once

#include <QtWidgets/QDialog>

class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace osd::qtui {

class EmbedSession;

class InputMapDialog : public QDialog
{
	Q_OBJECT

public:
	explicit InputMapDialog(EmbedSession *session, QWidget *parent = nullptr);
	~InputMapDialog();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void rebuild();
	int  selectedIndex() const;
	void startCapture(int index, bool append);
	void endCapture();
	void cancelCapture();
	void tick();
	void updateButtons();

	EmbedSession *m_session;
	QTreeWidget  *m_tree = nullptr;
	QLabel       *m_banner = nullptr;
	QPushButton  *m_remapBtn = nullptr;
	QPushButton  *m_addBtn = nullptr;
	QPushButton  *m_defaultBtn = nullptr;
	QPushButton  *m_clearBtn = nullptr;
	QTimer       *m_timer = nullptr;

	int      m_capturing = -1;    // index being captured, or -1
	bool     m_forwarding = false; // app-level input forwarding active (capture)
	unsigned m_lastGen = 0;        // last input-map generation rendered
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_INPUTMAPDIALOG_H
