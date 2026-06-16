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

#include <QtCore/QHash>
#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>

#include <functional>
#include <vector>

class QActionGroup;
class QCloseEvent;
class QComboBox;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTimer;
class QTreeView;
class QWidget;
class QAbstractItemView;
class QModelIndex;

namespace osd::qtui {

class ArtworkPanel;
class AuditManager;
class CheckableComboBox;
class FamilyTreeModel;
class FolderTree;
class GameListModel;
class GridView;
class SoftwareLoader;
class SoftwareModel;
class SoftwareProxy;
class TreeFilterProxy;

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
	void openProperties();
	void showSystemContextMenu(const QPoint &pos);
	void showSoftwareContextMenu(const QPoint &pos);
	void launchSelectedSystem();
	void launchSelectedSoftware();
	void onFolderSelected(const FolderFilter &filter);
	void onSearchTextChanged(const QString &text);
	void onStatusFilterChanged();
	void onVersionFilterChanged();
	void onSoftwareFilterChanged();
	void onSystemSelectionChanged();
	void onSoftwareSelectionChanged();
	void refreshSoftware();
	void onSoftwareLoaded(const std::vector<qtui_software_entry> &entries);
	void onSoftwareAvailabilityReady(const QVector<int> &availability);

private:
	// Arrangement of the system list, software list, and artwork panes.
	enum MainLayout { SoftwareBesideArt = 0, SoftwareUnderSystems };

	void createMenus();
	void createWidgets();
	void updateStatusCount();
	void setSoftwarePaneVisible(bool visible);
	void selectPendingSoftware();   // re-select the restored software item once


	void applyMainLayout(int layout);   // (re)assemble the splitters
	void applyIconSize(int size);   // icon size + matching row height

	// List / Grouped / Grid view modes (per pane).
	enum ViewMode { ViewList = 0, ViewGrouped = 1, ViewGrid = 2 };
	QWidget *buildGridBar(QSlider *&size, QComboBox *&source, CheckableComboBox *&caption);
	void setMachineViewMode(int mode);
	void setSoftwareViewMode(int mode);
	void applyMachineThumbSource();
	void applySoftwareThumbSource();

	// Active view + source-row mapping (the three machine views differ).
	QAbstractItemView *activeMachineView() const;
	QAbstractItemView *activeSoftwareView() const;
	int machineSourceRow(QAbstractItemView *view, const QModelIndex &viewIndex) const;
	int softwareSourceRow(QAbstractItemView *view, const QModelIndex &viewIndex) const;
	void selectSystemInActiveView(const QString &shortName);
	void saveSettings() const;
	void restoreSettings();

	// Per-system software ROM-availability cache (avoids re-auditing on every
	// re-selection); persisted to disk and invalidated on a ROM re-audit.
	QString softwareCachePath() const;
	void loadSoftwareCache();
	void saveSoftwareCache() const;
	void clearSoftwareCache();

	// Short name of the currently selected system, or empty if none.
	QString selectedSystem() const;

	// Hide the window, run the emulator, restore the window; report failures.
	void runModal(const QString &label, const std::function<int ()> &runner);

	GameListModel *m_model = nullptr;
	GameListProxy *m_proxy = nullptr;
	QTableView *m_view = nullptr;
	GridView *m_grid = nullptr;
	QTreeView *m_tree = nullptr;
	FamilyTreeModel *m_treeModel = nullptr;
	TreeFilterProxy *m_treeProxy = nullptr;
	QStackedWidget *m_systemStack = nullptr;
	QWidget *m_gridBar = nullptr;
	QSlider *m_gridSize = nullptr;
	QComboBox *m_gridSource = nullptr;
	CheckableComboBox *m_gridCaption = nullptr;
	QComboBox *m_viewMode = nullptr;
	FolderTree *m_folders = nullptr;
	SoftwareModel *m_softwareModel = nullptr;
	SoftwareProxy *m_softwareProxy = nullptr;
	QTableView *m_softwareView = nullptr;
	GridView *m_softwareGrid = nullptr;
	QTreeView *m_softwareTree = nullptr;
	FamilyTreeModel *m_swTreeModel = nullptr;
	TreeFilterProxy *m_swTreeProxy = nullptr;
	QStackedWidget *m_softwareStack = nullptr;
	QWidget *m_softwareGridBar = nullptr;
	QSlider *m_softwareGridSize = nullptr;
	QComboBox *m_softwareGridSource = nullptr;
	CheckableComboBox *m_softwareGridCaption = nullptr;
	QComboBox *m_softwareViewMode = nullptr;
	QSplitter *m_splitter = nullptr;
	QSplitter *m_rightSplitter = nullptr;
	ArtworkPanel *m_artwork = nullptr;
	QWidget *m_systemPane = nullptr;
	QWidget *m_softwarePane = nullptr;
	int m_mainLayout = SoftwareBesideArt;
	QLineEdit *m_search = nullptr;
	QLineEdit *m_softwareSearch = nullptr;
	// Machine list filters, shared between the "Filters" bar button and the
	// View ▸ Filters menu.
	QAction *m_actWorking = nullptr;
	QAction *m_actNotWorking = nullptr;
	QAction *m_actAvailable = nullptr;
	QAction *m_actUnavailable = nullptr;
	QAction *m_actHideClones = nullptr;
	QAction *m_actHideBootlegs = nullptr;
	QAction *m_actHideHacks = nullptr;
	QAction *m_actHidePrototypes = nullptr;
	// Software-list filters (shared by the software "Filters" button and the
	// View ▸ Software Filters menu).
	QAction *m_actSwSupported = nullptr;
	QAction *m_actSwPartial = nullptr;
	QAction *m_actSwUnsupported = nullptr;
	QAction *m_actSwAvailable = nullptr;
	QAction *m_actSwUnavailable = nullptr;
	QAction *m_actSwHideClones = nullptr;
	QAction *m_actSwHideBootlegs = nullptr;
	QAction *m_actSwHideHacks = nullptr;
	QAction *m_actSwHidePrototypes = nullptr;
	QTimer *m_softwareTimer = nullptr;
	QAction *m_playAct = nullptr;
	QAction *m_propertiesAct = nullptr;
	QAction *m_auditAct = nullptr;
	QActionGroup *m_iconSizeGroup = nullptr;
	QActionGroup *m_layoutGroup = nullptr;
	AuditManager *m_audit = nullptr;
	SoftwareLoader *m_softwareLoader = nullptr;
	QProgressBar *m_progressBar = nullptr;
	QPushButton *m_cancelAuditButton = nullptr;

	QHash<QString, QVector<int>> m_softwareAvail;   // system -> availability
	QString m_softwareLoadSystem;                   // system of the active load

	// Software selection to restore once its list loads after a session restore
	// (applied a single time, then cleared).
	QString m_pendingSoftwareList;
	QString m_pendingSoftwareName;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_MAINWINDOW_H
