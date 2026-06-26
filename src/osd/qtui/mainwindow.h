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
class QDialog;
class QPlainTextEdit;
class QEvent;
class QObject;
class QLineEdit;
class QMenu;
class QMenuBar;
class QProgressBar;
class QPushButton;
class QSlider;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTimer;
class QTreeView;
class QWidget;
class QWindow;
class QAbstractItemView;
class QModelIndex;

namespace osd::qtui {

class ArtworkPanel;
class AuditManager;
class SoftwareAuditManager;
class CheckableComboBox;
class FamilyTreeModel;
class FolderTree;
class GameListModel;
class GridView;
class InfoLoader;
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

	// Launch directly into an embedded play window (game + in-game menu bar) with no browser,
	// quitting the app when it closes.  Used by the `--gooey <system> [software]` CLI flag.
	// --gooey launcher.  renderer = "bgfx"/"opengl"/"" (empty = persisted setting);
	// bgfxBackend = "auto"/"opengl"/"vulkan"/""; shader = effect chain name to apply
	// once the game starts (e.g. "crt-geom"), or "".
	void startStandaloneEmbedded(const QString &system, const QString &software,
			const QString &renderer = QString(), const QString &bgfxBackend = QString(),
			const QString &shader = QString());

	// CLI passthrough (`./mame <args>`): run the command line through the
	// Qt-native OSD on a worker thread, with the browser never shown.  The render
	// window is created lazily only if the run needs video, so headless commands
	// (-listxml, …) open no window.  Closing the game window quits the app.
	void runCliPassthrough(const std::vector<std::string> &args);

protected:
	void closeEvent(QCloseEvent *event) override;
	void changeEvent(QEvent *event) override;
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

	// Where the Qt-native game surface is shown: full central area (browser list
	// replaced) or in a pane beside the list.
	enum NativePlacement { PlaceCentral = 0, PlacePane = 1 };

	// Renderer for the Qt-native window: OpenGL (drawogl) or BGFX (drawbgfx,
	// which unlocks the shader chains).
	enum NativeRenderer { RendererOpenGL = 0, RendererBgfx = 1 };

	// Non-SDL audio backend for the Qt-native OSD: an index into the platform's
	// kSoundProviders table (mainwindow.cpp), 0 = the platform default.

	// Launch routing: all play goes through the Qt-native OSD.
	void launchSystem(const QString &system, const QString &software);
	// Render into a QWindow via the Qt-native OSD (OpenGL or BGFX), no SDL.
	void launchEmbeddedNativeGl(const QString &label, const QString &system, const QString &software);
	// GUI-thread surface factory for CLI passthrough: create + show a top-level
	// render window and block until it is exposed.  Invoked from the worker via a
	// blocking-queued call inside qt_osd_interface::video_init().  Returns false
	// if the surface never became exposed.
	bool createCliRenderWindow(osd::qtui::QtEmbedTarget *target);
	void setNativePlacement(int placement);   // central (full) vs pane (beside list)
	void setNativeRenderer(int renderer);     // OpenGL vs BGFX for the Qt-native window
	void setBgfxBackend(int backend);         // Auto/OpenGL/Vulkan for the BGFX renderer
	void setSoundProvider(int provider);      // non-SDL audio for the Qt-native OSD
	void updateNativeGlSize();   // publish the QWindow's device-pixel size to the worker
	// Stop a running embedded game without quitting.
	void stopEmbedded();
	bool embedRunning() const;
	// Restore the browser UI after a Qt-native run ends.
	void returnFromEmbed();

	// NEWUI-parity in-game controls (active only during an in-process embed).
	// Capability keys for show/hide relevance gating (see applyMenuRelevance).
	enum CapKey { CapDips, CapConfigs, CapBios, CapSlots, CapImages, CapTape,
			CapNetwork, CapBarcode, CapCrosshair, CapSound, CapNaturalKeyboard,
			CapCheat, CapInput /* natural keyboard OR crosshair */ };
	void buildMachineMenu();
	void addInGameMenus(QMenuBar *bar);          // add the in-game top-level menus to a bar (main + detached window)
	void rebuildMediaMenu(QMenu *menu);          // (re)populate the Media submenu from the live image snapshot
	void rebuildSlotsMenu(QMenu *menu);          // (re)populate the Slots submenu from the live slot snapshot
	void rebuildSettingsMenu(QMenu *menu, bool config); // DIP switches / machine-config submenu from the live snapshot
	void rebuildBiosMenu(QMenu *menu);           // BIOS selection (hard reset to apply)
	void rebuildTapeMenu(QMenu *menu);           // cassette transport
	void rebuildNetworkMenu(QMenu *menu);        // network device interface assignment
	void rebuildBarcodeMenu(QMenu *menu);        // barcode reader entry
	void rebuildCrosshairMenu(QMenu *menu);      // per-player crosshair visibility
	void rebuildCheatMenu(QMenu *menu);          // cheat enable + per-entry state
	void rebuildVideoMenu(QMenu *menu);          // whole Video menu: pixels/view/artwork + geometry + image + performance
	void rebuildAudioMenu(QMenu *menu);          // volume sliders from the live slider snapshot
	void addSliderControl(QMenu *menu, const EmbedSlider &s, int index); // submenu with a live QSlider widget
	void showInfoText(const QString &title, const QString &text); // shared read-only Info dialog
	void showRunningHistory();                   // load + show the running game's history (async)
	void applyMenuRelevance(const EmbedCaps &caps); // hide menus/submenus the running machine lacks
	void setEmbedFullscreen(bool on);            // GUI-level fullscreen of the embedded game surface
	void showReloadOverlay(const QString &message); // brief overlay over a media/slot reset gap
	void postEmbed(const EmbedAction &action);   // no-op if no in-process session
	void setMachineControlsActive(bool active);
	void updateEmbedStatus();                    // sync the Pause check + menu relevance from the live machine

	void createMenus();
	void createWidgets();
	void updateStatusCount();
	void setSoftwarePaneVisible(bool visible);
	// After a machine-list re-filter, drop/refresh the software pane if the
	// system it is showing is no longer the (visible) selection.
	void syncSoftwarePane();
	void selectPendingSoftware();   // re-select the restored software item once


	void applyMainLayout(int layout);   // (re)assemble the splitters
	void applyPaneVisibility();         // show/hide folders, machine, artwork panes per the toggles
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
	QStackedWidget *m_centralStack = nullptr;   // browser page / Qt-native play page
	bool m_standaloneEmbed = false;   // launched via --gooey: closing the game quits the app
	std::unique_ptr<EmbedSession> m_embedSession;   // in-process embed command bridge
	std::thread m_embedThread;                      // runs the in-process emulation
	// Phase 13 Qt-native OSD: GUI-thread-owned render surface handed to the worker
	QWindow *m_nativeGlWindow = nullptr;
	QWidget *m_nativeGlContainer = nullptr;   // hosts m_nativeGlWindow (central stack or artwork pane)
	std::unique_ptr<osd::qtui::QtEmbedTarget> m_nativeGlTarget;
	int m_nativeGlLastMouseX = 0, m_nativeGlLastMouseY = 0;   // for relative mouse deltas
	int m_nativePlacement = PlaceCentral;          // current Qt-native placement preference
	bool m_nativeGlPlacedInPane = false;           // placement actually used by the live run
	bool m_quitAfterStop = false;                  // close-X in pane mode: quit once the game stops
	QString m_pendingShaderChain;                  // CLI --shader: applied once the BGFX chains publish
	int m_nativeRenderer = RendererOpenGL;         // OpenGL vs BGFX for the Qt-native window
	int m_bgfxBackend = 0;                          // 0=auto, 1=opengl, 2=vulkan (BGFX backend)
	int m_soundProvider = 0;                        // index into kSoundProviders
	QActionGroup *m_nativePlacementGroup = nullptr;
	QActionGroup *m_nativeRendererGroup = nullptr;
	QActionGroup *m_bgfxBackendGroup = nullptr;
	QActionGroup *m_soundProviderGroup = nullptr;
	QList<QMenu *> m_machineMenus;                  // in-game top-level menus (both bars), shown only while embedded
	QList<QAction *> m_machineActions;              // all control actions (both bars), for enable/disable
	std::vector<std::pair<QAction *, int>> m_relevanceActions; // (menu/submenu action, CapKey) hidden when the machine lacks it
	unsigned m_lastCapsGen = 0;                     // last applied capability generation (relevance refresh)
	QList<QAction *> m_pauseActions;                // Pause toggles to keep in sync
	QList<QAction *> m_fullscreenActions;           // Fullscreen toggles to keep in sync
	bool m_embedFullscreen = false;                 // GUI fullscreen active for the embedded game
	bool m_machineControlsActive = false;           // current enable state (new actions inherit it)
	// A launch requested while a game is still running: the running game is
	// stopped, then this one starts once it finishes.
	bool m_hasPendingLaunch = false;
	QString m_pendingLaunchSystem;
	QString m_pendingLaunchSoftware;
	QTimer *m_embedStatusTimer = nullptr;           // polls live paused state
	QDialog *m_infoDialog = nullptr;                // shared read-only Info dialog
	QPlainTextEdit *m_infoTextView = nullptr;       // its text area
	InfoLoader *m_embedInfoLoader = nullptr;        // async loader for the running game's History
	quint64 m_embedInfoEpoch = 0;                   // discards stale History results
	QString m_runningSystem;                        // system short name of the current embedded run
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
	QAction *m_actShowFolders = nullptr;    // View ▸ Panes toggles
	QAction *m_actShowSystems = nullptr;
	QAction *m_actShowArtwork = nullptr;
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
