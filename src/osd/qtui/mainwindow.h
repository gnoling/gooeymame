// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mainwindow.h - qtui main browser window
//
//============================================================
#ifndef MAME_OSD_QTUI_MAINWINDOW_H
#define MAME_OSD_QTUI_MAINWINDOW_H

#pragma once

#include <QtWidgets/QMainWindow>

class QSortFilterProxyModel;
class QTableView;

namespace osd::qtui {

class GameListModel;

//============================================================
//  The main MAMEUI browser window.
//============================================================
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	virtual ~MainWindow();

private slots:
	void showAbout();

private:
	void createMenus();
	void createGameList();
	void updateStatusCount();

	GameListModel *m_model = nullptr;
	QSortFilterProxyModel *m_proxy = nullptr;
	QTableView *m_view = nullptr;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_MAINWINDOW_H
