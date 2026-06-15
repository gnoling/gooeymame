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

#include "emulator.h"        // for qtui_software_entry (moc'd slot signature)
#include "gamelistproxy.h"   // for FolderFilter (used by a moc'd slot signature)

#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>

#include <functional>
#include <vector>

class QActionGroup;
class QCloseEvent;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTableView;
class QTimer;
class QWidget;

namespace osd::qtui {

class ArtworkPanel;
class AuditManager;
class FolderTree;
class GameListModel;
class SoftwareLoader;
class SoftwareModel;
class SoftwareProxy;

//============================================================
//  The main MAMEUI browser window.
//============================================================
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	virtual ~MainWindow();

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void showAbout();
	void openOptions();
	void launchSelectedSystem();
	void launchSelectedSoftware();
	void onFolderSelected(const FolderFilter &filter);
	void onSearchTextChanged(const QString &text);
	void onStatusFilterChanged();
	void onSoftwareFilterChanged();
	void onSystemSelectionChanged();
	void onSoftwareSelectionChanged();
	void refreshSoftware();
	void onSoftwareLoaded(const std::vector<qtui_software_entry> &entries);
	void onSoftwareAvailabilityReady(const QVector<int> &availability);

private:
	void createMenus();
	void createWidgets();
	void updateStatusCount();
	void setSoftwarePaneVisible(bool visible);
	void applyIconSize(int size);   // icon size + matching row height
	void saveSettings() const;
	void restoreSettings();

	// Short name of the currently selected system, or empty if none.
	QString selectedSystem() const;

	// Hide the window, run the emulator, restore the window; report failures.
	void runModal(const QString &label, const std::function<int ()> &runner);

	GameListModel *m_model = nullptr;
	GameListProxy *m_proxy = nullptr;
	QTableView *m_view = nullptr;
	FolderTree *m_folders = nullptr;
	SoftwareModel *m_softwareModel = nullptr;
	SoftwareProxy *m_softwareProxy = nullptr;
	QTableView *m_softwareView = nullptr;
	QSplitter *m_splitter = nullptr;
	QSplitter *m_rightSplitter = nullptr;
	ArtworkPanel *m_artwork = nullptr;
	QWidget *m_softwarePane = nullptr;
	QLineEdit *m_search = nullptr;
	QLineEdit *m_softwareSearch = nullptr;
	QPushButton *m_btnWorking = nullptr;
	QPushButton *m_btnNotWorking = nullptr;
	QPushButton *m_btnAvailable = nullptr;
	QPushButton *m_btnUnavailable = nullptr;
	QPushButton *m_btnSupported = nullptr;
	QPushButton *m_btnPartial = nullptr;
	QPushButton *m_btnUnsupported = nullptr;
	QPushButton *m_btnSwAvailable = nullptr;
	QPushButton *m_btnSwUnavailable = nullptr;
	QTimer *m_softwareTimer = nullptr;
	QAction *m_playAct = nullptr;
	QAction *m_auditAct = nullptr;
	QActionGroup *m_iconSizeGroup = nullptr;
	AuditManager *m_audit = nullptr;
	SoftwareLoader *m_softwareLoader = nullptr;
	QProgressBar *m_progressBar = nullptr;
	QPushButton *m_cancelAuditButton = nullptr;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_MAINWINDOW_H
