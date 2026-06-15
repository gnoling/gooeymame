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

#include "gamelistproxy.h"   // for FolderFilter (used by a moc'd slot signature)

#include <QtWidgets/QMainWindow>

#include <functional>

class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QTableView;
class QTimer;
class QWidget;

namespace osd::qtui {

class FolderTree;
class GameListModel;
class SoftwareModel;

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
	void launchSelectedSystem();
	void launchSelectedSoftware();
	void onFolderSelected(const FolderFilter &filter);
	void onSearchTextChanged(const QString &text);
	void onStatusFilterChanged();
	void onSystemSelectionChanged();
	void refreshSoftware();

private:
	void createMenus();
	void createWidgets();
	void updateStatusCount();
	void setSoftwarePaneVisible(bool visible);

	// Short name of the currently selected system, or empty if none.
	QString selectedSystem() const;

	// Hide the window, run the emulator, restore the window; report failures.
	void runModal(const QString &label, const std::function<int ()> &runner);

	GameListModel *m_model = nullptr;
	GameListProxy *m_proxy = nullptr;
	QTableView *m_view = nullptr;
	FolderTree *m_folders = nullptr;
	SoftwareModel *m_softwareModel = nullptr;
	QSortFilterProxyModel *m_softwareProxy = nullptr;
	QTableView *m_softwareView = nullptr;
	QSplitter *m_splitter = nullptr;
	QWidget *m_softwarePane = nullptr;
	QLineEdit *m_search = nullptr;
	QLineEdit *m_softwareSearch = nullptr;
	QPushButton *m_btnWorking = nullptr;
	QPushButton *m_btnNotWorking = nullptr;
	QTimer *m_softwareTimer = nullptr;
	QAction *m_playAct = nullptr;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_MAINWINDOW_H
