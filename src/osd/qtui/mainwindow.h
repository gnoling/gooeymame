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
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtWidgets/QMainWindow>

#include <functional>
#include <memory>
#include <thread>
#include <vector>

class QActionGroup;
class QCloseEvent;
class QComboBox;
class QEvent;
class QObject;
class QLineEdit;
class QMenu;
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
class SoftwareAuditManager;
class CheckableComboBox;
class EmbedHost;
class FamilyTreeModel;
class FolderTree;
class GameListModel;
class GridView;
class RepresentativeProxy;
class SoftwareLoader;
class SoftwareModel;
class SoftwareProxy;
class TreeFilterProxy;

// Apply the persisted Qt widget style (QSettings appearance/style) at startup,
// recording the platform default first so it can be restored.  Call once after
// QApplication is constructed and the QSettings format is chosen.
void applyPersistedStyle();
// The widget style in effect before any user override (the platform default).
QString defaultStyleName();
// Apply the persisted colour scheme (QSettings appearance/colorScheme) at
// startup.  Call once after QApplication is constructed, the QSettings format
// is chosen, and applyPersistedStyle() has run.
void applyPersistedColorScheme();

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
	bool eventFilter(QObject *watched, QEvent *event) override;

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
	void onEmbeddedFinished(int exitCode);

private:
	// Arrangement of the system list, software list, and artwork panes.
	enum MainLayout { SoftwareBesideArt = 0, SoftwareUnderSystems };

	// How a selected system/software is launched.
	enum EmbedMode { EmbedSeparate = 0, EmbedInProcess = 1, EmbedChild = 2 };

	// Where an embedded game's video surface is shown.  (LocMainPane is retired
	// but its value is kept so old settings remap cleanly.)  LocBrowser hosts
	// the game in the right (artwork) pane via the panel's game view modes.
	enum EmbedLocation { LocMainPane = 0, LocBrowser = 1, LocWindow = 2 };

	// True when the platform supports attaching MAME to a Qt window (X11/xcb).
	static bool embeddingSupported();
	// Launch routing: dispatches on m_embedMode.
	void launchSystem(const QString &system, const QString &software);
	void launchEmbeddedChild(const QString &label, const QStringList &mameArgs);
	void launchEmbeddedInProcess(const QString &label, const QString &system, const QString &software);
	void setEmbedMode(int mode);
	void setEmbedLocation(int location);
	// Reparent the embed host into the configured location (pane/dock/window)
	// and show it, ready to be attached to once it has been laid out.
	void placeEmbedSurface();
	// Stop a running embedded game (in-process or child) without quitting.
	void stopEmbedded();
	bool embedRunning() const;
	// Restore the UI after an embedded run ends (per location).
	void returnFromEmbed();

	// NEWUI-parity in-game controls (active only during an in-process embed).
	void buildMachineMenu();
	void populateMachineMenu(QMenu *menu);       // fill a Machine menu (main bar or detached window)
	void rebuildMediaMenu(QMenu *menu);          // (re)populate the Media submenu from the live image snapshot
	void rebuildSlotsMenu(QMenu *menu);          // (re)populate the Slots submenu from the live slot snapshot
	void showReloadOverlay(const QString &message); // brief overlay over a media/slot reset gap
	void postEmbed(const EmbedAction &action);   // no-op if no in-process session
	void setMachineControlsActive(bool active);
	void updateEmbedStatus();                    // sync the Pause check from the live machine

	void createMenus();
	void createWidgets();
	void updateStatusCount();
	void setSoftwarePaneVisible(bool visible);
	// After a machine-list re-filter, drop/refresh the software pane if the
	// system it is showing is no longer the (visible) selection.
	void syncSoftwarePane();
	void selectPendingSoftware();   // re-select the restored software item once


	void applyMainLayout(int layout);   // (re)assemble the splitters
	void applyIconSize(int size);   // icon size + matching row height
	void applyStyle(const QString &name);   // set + persist the Qt widget style ("" = default)
	void applyColorScheme(const QString &scheme);   // set + persist colour scheme ("dark"/"light"/"")

	// List / Grouped / Grid view modes (per pane).
	enum ViewMode { ViewList = 0, ViewGrouped = 1, ViewGrid = 2, ViewGridGrouped = 3 };
	QWidget *buildGridBar(QSlider *&size, QComboBox *&source, CheckableComboBox *&caption);
	void setMachineViewMode(int mode);
	void setSoftwareViewMode(int mode);
	void applyMachineThumbSource();
	void applySoftwareThumbSource();
	// Ordered art-type labels to fall back through (configured in Options), and
	// whether to also try related sets (clone parent / other regions).
	QStringList gridFallbackLabels(bool *family) const;

	// Active view + source-row mapping (the three machine views differ).
	QAbstractItemView *activeMachineView() const;
	QAbstractItemView *activeSoftwareView() const;
	int machineSourceRow(QAbstractItemView *view, const QModelIndex &viewIndex) const;
	int softwareSourceRow(QAbstractItemView *view, const QModelIndex &viewIndex) const;
	void selectSystemInActiveView(const QString &shortName);
	void selectSoftwareRow(int sourceRow);   // select a source row in the active software view
	void invalidateMachineViews();           // re-filter the tree + grid proxies
	void invalidateSoftwareViews();
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
	GridView *m_grid = nullptr;          // flat grid (every member)
	GridView *m_gridGrouped = nullptr;   // one tile per family (representatives)
	RepresentativeProxy *m_gridProxy = nullptr;
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
	GridView *m_softwareGrid = nullptr;          // flat grid
	GridView *m_swGridGrouped = nullptr;         // one tile per family
	RepresentativeProxy *m_swGridProxy = nullptr;
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
	QStackedWidget *m_centralStack = nullptr;   // browser page / embedded play page
	EmbedHost *m_embedHost = nullptr;
	int m_embedMode = EmbedSeparate;
	int m_embedLocation = LocWindow;
	bool m_hideBrowserWhilePlaying = false;
	QActionGroup *m_embedModeGroup = nullptr;
	QActionGroup *m_embedLocationGroup = nullptr;
	QAction *m_hideBrowserAct = nullptr;
	QMainWindow *m_embedWindow = nullptr;   // host when LocWindow (own menu bar)
	std::unique_ptr<EmbedSession> m_embedSession;   // in-process embed command bridge
	std::thread m_embedThread;                      // runs the in-process emulation
	QMenu *m_machineMenu = nullptr;                 // NEWUI-parity in-game controls (main bar)
	QList<QAction *> m_machineActions;              // all control actions (both bars), for enable/disable
	QList<QAction *> m_pauseActions;                // Pause toggles to keep in sync
	bool m_machineControlsActive = false;           // current enable state (new actions inherit it)
	// A launch requested while a game is still running: the running game is
	// stopped, then this one starts once it finishes.
	bool m_hasPendingLaunch = false;
	QString m_pendingLaunchSystem;
	QString m_pendingLaunchSoftware;
	QTimer *m_embedStatusTimer = nullptr;           // polls live paused state
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
	QAction *m_softwareAuditAct = nullptr;
	QActionGroup *m_iconSizeGroup = nullptr;
	QActionGroup *m_layoutGroup = nullptr;
	QActionGroup *m_styleGroup = nullptr;
	QActionGroup *m_colorSchemeGroup = nullptr;
	AuditManager *m_audit = nullptr;
	SoftwareAuditManager *m_softwareAudit = nullptr;
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
