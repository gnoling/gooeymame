// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  pluginmenudialog.h - in-game Plugin Options menu for the qtui OSD
//
//  Surfaces MAME's Lua-driven plugin menus (autofire, hiscore, cheat finder,
//  data/DAT, …; mirrors ui/pluginopt.cpp) in the embedded play window.  The
//  worker (EmbedController) publishes the static list of registered plugin
//  menus and, while one is open, its current navigation-driven item list; this
//  dialog renders the items and posts navigation events (select / left / right /
//  back / clear / typed characters) back to the emulation thread, which calls
//  the plugin's Lua callbacks on the live machine.
//
//============================================================
#ifndef MAME_OSD_QTUI_PLUGINMENUDIALOG_H
#define MAME_OSD_QTUI_PLUGINMENUDIALOG_H

#pragma once

#include "embedsession.h"

#include <QtWidgets/QDialog>

#include <string>
#include <vector>

class QEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QObject;
class QPushButton;
class QTimer;

namespace osd::qtui {

class EmbedSession;

class PluginMenuDialog : public QDialog
{
	Q_OBJECT

public:
	explicit PluginMenuDialog(EmbedSession *session, QWidget *parent = nullptr);
	~PluginMenuDialog();

protected:
	// Filter the list's key events: Up/Down navigate (default), but Left/Right/
	// Backspace/typed characters are forwarded to the plugin's Lua callback.
	bool eventFilter(QObject *obj, QEvent *event) override;
	void closeEvent(QCloseEvent *event) override;
	void reject() override;

private:
	void tick();              // poll the worker for generation changes
	void rebuild();           // rebuild the list from the latest snapshot
	void updateAdjustBar();   // sync the mouse controls to the highlighted row
	void setForwarding(bool on);  // app-level key/mouse forwarding while a plugin polls input
	void onActivated(QListWidgetItem *item);
	void onBack();
	void postEvent(int index, const char *key);

	EmbedSession *m_session;
	QLabel       *m_header = nullptr;
	QListWidget  *m_list = nullptr;
	QLabel       *m_hint = nullptr;
	QPushButton  *m_selectBtn = nullptr;
	QPushButton  *m_leftBtn = nullptr;
	QPushButton  *m_rightBtn = nullptr;
	QPushButton  *m_clearBtn = nullptr;
	QPushButton  *m_back = nullptr;
	QTimer       *m_timer = nullptr;

	unsigned          m_lastGen = 0;
	bool              m_active = false;   // a plugin menu is currently open
	bool              m_nokeys = false;   // active menu suppresses typed input
	bool              m_forwarding = false; // app-level input forwarding (poller active)
	std::vector<EmbedPluginItem> m_items; // active menu items (parallel to list rows)
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_PLUGINMENUDIALOG_H
