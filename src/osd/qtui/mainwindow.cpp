// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mainwindow.cpp - qtui main browser window
//
//============================================================

#include "mainwindow.h"

#include "artworkpanel.h"
#include "auditmanager.h"
#include "emulator.h"
#include "foldertree.h"
#include "gamelistmodel.h"
#include "gamelistproxy.h"
#include "optionsdialog.h"
#include "softwareloader.h"
#include "softwaremodel.h"
#include "softwareproxy.h"

#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtGui/QActionGroup>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>


namespace osd::qtui {

namespace {

// Delay between a system selection settling and enumerating its software.
// Enumeration builds the machine configuration, which is comparatively
// expensive, so we debounce rapid keyboard navigation.
constexpr int SOFTWARE_DEBOUNCE_MS = 200;

// Create a compact checkable quick-filter button.
QPushButton *makeToggle(const QString &text, const QString &tip)
{
	QPushButton *button = new QPushButton(text);
	button->setCheckable(true);
	button->setToolTip(tip);
	return button;
}

// Make two toggle buttons mutually exclusive (either, or neither, but never
// both).  Unchecking the opposite is silenced so it does not re-trigger the
// filter recomputation.
void pairExclusive(QPushButton *a, QPushButton *b)
{
	QObject::connect(a, &QPushButton::toggled, b, [b] (bool on) {
		if (on) { QSignalBlocker block(b); b->setChecked(false); }
	});
	QObject::connect(b, &QPushButton::toggled, a, [a] (bool on) {
		if (on) { QSignalBlocker block(a); a->setChecked(false); }
	});
}

} // anonymous namespace

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent)
{
	setWindowTitle(tr("MAMEUI"));
	resize(1100, 680);

	createMenus();
	createWidgets();

	updateStatusCount();

	// Audit progress widgets live permanently in the status bar, hidden until
	// an audit runs.
	m_progressBar = new QProgressBar;
	m_progressBar->setMaximumWidth(220);
	m_progressBar->setVisible(false);
	m_cancelAuditButton = new QPushButton(tr("Cancel"));
	m_cancelAuditButton->setVisible(false);
	statusBar()->addPermanentWidget(m_progressBar);
	statusBar()->addPermanentWidget(m_cancelAuditButton);

	// Background ROM availability: load the cache if present, otherwise run a
	// first audit so the Available/Unavailable filters have data to work with.
	m_audit = new AuditManager(m_model, this);
	connect(m_cancelAuditButton, &QPushButton::clicked, this, [this] {
		m_cancelAuditButton->setEnabled(false);
		m_audit->cancelAudit();
		statusBar()->showMessage(tr("Cancelling audit…"));
	});
	connect(m_audit, &AuditManager::progress, this, [this] (int audited, int total) {
		m_progressBar->setVisible(true);
		m_cancelAuditButton->setVisible(true);
		m_cancelAuditButton->setEnabled(true);
		m_progressBar->setRange(0, total);
		m_progressBar->setValue(audited);
		statusBar()->showMessage(tr("Auditing ROMs… %1 of %2").arg(audited).arg(total));
	});
	connect(m_audit, &AuditManager::finished, this, [this] {
		m_progressBar->setVisible(false);
		m_cancelAuditButton->setVisible(false);
		m_auditAct->setEnabled(true);
		updateStatusCount();
	});
	if (!m_audit->loadCache())
	{
		m_auditAct->setEnabled(false);
		m_audit->startAudit();
	}

	loadSoftwareCache();
	restoreSettings();
}

void MainWindow::openOptions()
{
	OptionsDialog dialog(this);
	if (dialog.exec() == QDialog::Accepted)
	{
		// Path changes can affect availability; suggest a re-audit.
		statusBar()->showMessage(
				tr("Options saved. Use Tools ▸ Refresh ROM Availability to re-scan."),
				6000);
	}
}

void MainWindow::openProperties()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
		return;

	OptionsDialog dialog(system, this);
	if (dialog.exec() == QDialog::Accepted)
		statusBar()->showMessage(tr("Saved properties for %1.").arg(system), 4000);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
	saveSettings();
	saveSoftwareCache();
	QMainWindow::closeEvent(event);
}

void MainWindow::saveSettings() const
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("mainwindow"));
	settings.setValue(QStringLiteral("geometry"), saveGeometry());
	settings.setValue(QStringLiteral("mainLayout"), m_mainLayout);
	settings.setValue(QStringLiteral("splitter"), m_splitter->saveState());
	settings.setValue(QStringLiteral("rightSplitter"), m_rightSplitter->saveState());
	settings.setValue(QStringLiteral("systemHeader"), m_view->horizontalHeader()->saveState());
	settings.setValue(QStringLiteral("softwareHeader"), m_softwareView->horizontalHeader()->saveState());
	settings.setValue(QStringLiteral("selected"), selectedSystem());
	settings.endGroup();
}

QString MainWindow::softwareCachePath() const
{
	QString const dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir().mkpath(dir);
	return dir + QStringLiteral("/software_availability.cache");
}

void MainWindow::loadSoftwareCache()
{
	QFile file(softwareCachePath());
	if (!file.open(QIODevice::ReadOnly))
		return;
	QDataStream in(&file);
	quint32 magic = 0, version = 0;
	in >> magic >> version;
	if (magic != 0x53574156u /* "SWAV" */ || version != 1u)
		return;
	in >> m_softwareAvail;
}

void MainWindow::saveSoftwareCache() const
{
	QFile file(softwareCachePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;
	QDataStream out(&file);
	out << quint32(0x53574156u) << quint32(1u) << m_softwareAvail;
}

void MainWindow::clearSoftwareCache()
{
	m_softwareAvail.clear();
	QFile::remove(softwareCachePath());
}

void MainWindow::restoreSettings()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("mainwindow"));

	QByteArray const geometry = settings.value(QStringLiteral("geometry")).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);

	// Apply the saved pane arrangement before restoring splitter states (which
	// depend on it) and tick the matching menu entry.
	int const mainLayout = settings.value(QStringLiteral("mainLayout"), int(SoftwareBesideArt)).toInt();
	applyMainLayout(mainLayout);
	for (QAction *act : m_layoutGroup->actions())
		if (act->data().toInt() == mainLayout)
			act->setChecked(true);

	QByteArray const splitterState = settings.value(QStringLiteral("splitter")).toByteArray();
	if (!splitterState.isEmpty())
		m_splitter->restoreState(splitterState);

	QByteArray const rightState = settings.value(QStringLiteral("rightSplitter")).toByteArray();
	if (!rightState.isEmpty())
		m_rightSplitter->restoreState(rightState);

	QByteArray const systemHeader = settings.value(QStringLiteral("systemHeader")).toByteArray();
	if (!systemHeader.isEmpty())
		m_view->horizontalHeader()->restoreState(systemHeader);

	QByteArray const softwareHeader = settings.value(QStringLiteral("softwareHeader")).toByteArray();
	if (!softwareHeader.isEmpty())
		m_softwareView->horizontalHeader()->restoreState(softwareHeader);

	int const iconSize = settings.value(QStringLiteral("iconSize"), 24).toInt();
	QString const selected = settings.value(QStringLiteral("selected")).toString();
	settings.endGroup();

	// Apply the saved icon size and tick the matching menu entry.
	applyIconSize(iconSize);
	for (QAction *act : m_iconSizeGroup->actions())
		if (act->data().toInt() == iconSize)
			act->setChecked(true);

	// Re-select the last system, if it is still visible under the current
	// folder/filters.
	if (!selected.isEmpty())
	{
		QModelIndexList const hits = m_proxy->match(
				m_proxy->index(0, GameListModel::COLUMN_NAME),
				GameListModel::ShortNameRole, selected, 1, Qt::MatchExactly);
		if (!hits.isEmpty())
		{
			m_view->setCurrentIndex(hits.first());
			m_view->scrollTo(hits.first(), QAbstractItemView::PositionAtCenter);
		}
	}
}

MainWindow::~MainWindow()
{
}

void MainWindow::createMenus()
{
	QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

	m_playAct = fileMenu->addAction(tr("&Play"));
	m_playAct->setShortcut(Qt::Key_Return);
	m_playAct->setEnabled(false);
	connect(m_playAct, &QAction::triggered, this, &MainWindow::launchSelectedSystem);

	m_propertiesAct = fileMenu->addAction(tr("P&roperties…"));
	m_propertiesAct->setShortcut(Qt::ALT | Qt::Key_Return);
	m_propertiesAct->setEnabled(false);
	connect(m_propertiesAct, &QAction::triggered, this, &MainWindow::openProperties);

	fileMenu->addSeparator();

	QAction *exitAct = fileMenu->addAction(tr("E&xit"));
	exitAct->setShortcut(QKeySequence::Quit);
	connect(exitAct, &QAction::triggered, this, &QWidget::close);

	QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

	QMenu *layoutMenu = viewMenu->addMenu(tr("&Layout"));
	m_layoutGroup = new QActionGroup(this);
	struct { const char *label; int layout; } const layouts[] = {
		{ "Software beside artwork", SoftwareBesideArt },
		{ "Software under system list", SoftwareUnderSystems },
	};
	for (const auto &choice : layouts)
	{
		QAction *act = layoutMenu->addAction(tr(choice.label));
		act->setCheckable(true);
		act->setData(choice.layout);
		m_layoutGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, layout = choice.layout] {
			applyMainLayout(layout);
			QSettings().setValue(QStringLiteral("mainwindow/mainLayout"), layout);
		});
	}

	QMenu *iconMenu = viewMenu->addMenu(tr("&Icon Size"));
	m_iconSizeGroup = new QActionGroup(this);
	struct { const char *label; int size; } const iconSizes[] = {
		{ "Small (16)", 16 }, { "Medium (24)", 24 }, { "Large (32)", 32 }, { "Extra Large (48)", 48 }
	};
	for (const auto &choice : iconSizes)
	{
		QAction *act = iconMenu->addAction(tr(choice.label));
		act->setCheckable(true);
		act->setData(choice.size);
		m_iconSizeGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, size = choice.size] {
			applyIconSize(size);
			QSettings().setValue(QStringLiteral("mainwindow/iconSize"), size);
		});
	}

	QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
	QAction *optionsAct = toolsMenu->addAction(tr("&Options…"));
	connect(optionsAct, &QAction::triggered, this, &MainWindow::openOptions);
	toolsMenu->addSeparator();
	m_auditAct = toolsMenu->addAction(tr("&Refresh ROM Availability"));
	connect(m_auditAct, &QAction::triggered, this, [this] {
		if (m_audit && !m_audit->isRunning())
		{
			// ROMs may have changed; the cached software availability is stale.
			clearSoftwareCache();
			m_auditAct->setEnabled(false);
			m_audit->startAudit();
		}
	});

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	QAction *aboutAct = helpMenu->addAction(tr("&About"));
	connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::createWidgets()
{
	m_model = new GameListModel(this);

	m_proxy = new GameListProxy(this);
	m_proxy->setSourceModel(m_model);

	// --- system list pane: [search | status] over the table ---
	m_view = new QTableView;
	m_view->setModel(m_proxy);
	m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_view->setSelectionMode(QAbstractItemView::SingleSelection);
	m_view->setSortingEnabled(true);
	m_view->setAlternatingRowColors(true);
	m_view->setShowGrid(false);
	m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_view->verticalHeader()->setVisible(false);
	// Let the description column absorb width changes (e.g. when the software
	// pane appears/disappears); keep the rest interactive at sane widths.
	m_view->horizontalHeader()->setStretchLastSection(false);
	m_view->horizontalHeader()->setSectionResizeMode(GameListModel::COLUMN_DESCRIPTION, QHeaderView::Stretch);
	m_view->sortByColumn(GameListModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
	m_view->setColumnWidth(GameListModel::COLUMN_NAME, 100);
	m_view->setColumnWidth(GameListModel::COLUMN_YEAR, 56);
	m_view->setColumnWidth(GameListModel::COLUMN_MANUFACTURER, 200);
	m_view->setColumnWidth(GameListModel::COLUMN_STATUS, 110);

	connect(m_view, &QTableView::doubleClicked, this, &MainWindow::launchSelectedSystem);
	connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSystemSelectionChanged);

	m_search = new QLineEdit;
	m_search->setClearButtonEnabled(true);
	m_search->setPlaceholderText(tr("Search systems…"));
	connect(m_search, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

	// Status quick-filters: combinable toggles that refine the current list.
	// Opposite pairs are mutually exclusive; the two groups (emulation,
	// availability) AND together.
	m_btnWorking = makeToggle(tr("Working"), tr("Show systems with working emulation"));
	m_btnNotWorking = makeToggle(tr("Not working"), tr("Show systems with non-working emulation"));
	m_btnAvailable = makeToggle(tr("Available"), tr("Show systems whose ROMs are present"));
	m_btnUnavailable = makeToggle(tr("Unavailable"), tr("Show systems whose ROMs are missing"));

	pairExclusive(m_btnWorking, m_btnNotWorking);
	pairExclusive(m_btnAvailable, m_btnUnavailable);

	for (QPushButton *button : { m_btnWorking, m_btnNotWorking, m_btnAvailable, m_btnUnavailable })
		connect(button, &QPushButton::toggled, this, &MainWindow::onStatusFilterChanged);

	m_systemPane = new QWidget;
	QVBoxLayout *systemLayout = new QVBoxLayout(m_systemPane);
	systemLayout->setContentsMargins(0, 0, 0, 0);

	// Quick-filter bar over the system list.
	QHBoxLayout *systemBar = new QHBoxLayout;
	systemBar->addWidget(m_search, 1);
	systemBar->addWidget(m_btnWorking);
	systemBar->addWidget(m_btnNotWorking);
	systemBar->addWidget(m_btnAvailable);
	systemBar->addWidget(m_btnUnavailable);
	systemLayout->addLayout(systemBar);
	systemLayout->addWidget(m_view);

	// --- software list pane: quick-filter bar over the table ---
	m_softwareModel = new SoftwareModel(this);

	m_softwareProxy = new SoftwareProxy(this);
	m_softwareProxy->setSourceModel(m_softwareModel);

	m_softwareView = new QTableView;
	m_softwareView->setModel(m_softwareProxy);
	m_softwareView->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_softwareView->setSelectionMode(QAbstractItemView::SingleSelection);
	m_softwareView->setSortingEnabled(true);
	m_softwareView->setAlternatingRowColors(true);
	m_softwareView->setShowGrid(false);
	m_softwareView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_softwareView->verticalHeader()->setVisible(false);
	m_softwareView->horizontalHeader()->setStretchLastSection(false);
	m_softwareView->horizontalHeader()->setSectionResizeMode(SoftwareModel::COLUMN_DESCRIPTION, QHeaderView::Stretch);
	connect(m_softwareView, &QTableView::doubleClicked, this, &MainWindow::launchSelectedSoftware);
	connect(m_softwareView->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSoftwareSelectionChanged);

	m_softwareSearch = new QLineEdit;
	m_softwareSearch->setClearButtonEnabled(true);
	m_softwareSearch->setPlaceholderText(tr("Search software…"));
	connect(m_softwareSearch, &QLineEdit::textChanged, this, [this] (const QString &text) {
		m_softwareProxy->setSearchText(text);
	});

	// Software runs through a background loader so per-entry ROM auditing
	// never blocks the UI and selection changes cancel in-flight work.
	m_softwareLoader = new SoftwareLoader(this);
	connect(m_softwareLoader, &SoftwareLoader::loaded, this, &MainWindow::onSoftwareLoaded);
	connect(m_softwareLoader, &SoftwareLoader::availabilityReady, this, &MainWindow::onSoftwareAvailabilityReady);

	// Support-level and availability quick-filters for the software list.
	m_btnSupported = makeToggle(tr("Supported"), tr("Show fully supported software"));
	m_btnPartial = makeToggle(tr("Partial"), tr("Show partially supported software"));
	m_btnUnsupported = makeToggle(tr("Unsupported"), tr("Show unsupported software"));
	m_btnSwAvailable = makeToggle(tr("Available"), tr("Show software whose ROMs are present"));
	m_btnSwUnavailable = makeToggle(tr("Unavailable"), tr("Show software whose ROMs are missing"));
	pairExclusive(m_btnSwAvailable, m_btnSwUnavailable);
	for (QPushButton *button : { m_btnSupported, m_btnPartial, m_btnUnsupported, m_btnSwAvailable, m_btnSwUnavailable })
		connect(button, &QPushButton::toggled, this, &MainWindow::onSoftwareFilterChanged);

	m_softwarePane = new QWidget;
	QVBoxLayout *softwareLayout = new QVBoxLayout(m_softwarePane);
	softwareLayout->setContentsMargins(0, 0, 0, 0);
	QHBoxLayout *softwareBar = new QHBoxLayout;
	softwareBar->addWidget(m_softwareSearch, 1);
	softwareBar->addWidget(m_btnSupported);
	softwareBar->addWidget(m_btnPartial);
	softwareBar->addWidget(m_btnUnsupported);
	softwareBar->addWidget(m_btnSwAvailable);
	softwareBar->addWidget(m_btnSwUnavailable);
	softwareLayout->addLayout(softwareBar);
	softwareLayout->addWidget(m_softwareView);

	// --- artwork panel ---
	m_artwork = new ArtworkPanel;

	// --- folder tree ---
	m_folders = new FolderTree(m_model);
	m_folders->setMaximumWidth(260);
	connect(m_folders, &FolderTree::folderSelected, this, &MainWindow::onFolderSelected);

	// The two splitters are populated by applyMainLayout(), which the menu and
	// restoreSettings() drive.  m_rightSplitter (vertical) always carries the
	// software pane at the bottom; m_splitter (horizontal) is the central one.
	m_rightSplitter = new QSplitter(Qt::Vertical);
	m_splitter = new QSplitter(Qt::Horizontal, this);
	setCentralWidget(m_splitter);
	applyMainLayout(m_mainLayout);

	// Debounce timer for software enumeration.
	m_softwareTimer = new QTimer(this);
	m_softwareTimer->setSingleShot(true);
	m_softwareTimer->setInterval(SOFTWARE_DEBOUNCE_MS);
	connect(m_softwareTimer, &QTimer::timeout, this, &MainWindow::refreshSoftware);
}

void MainWindow::applyIconSize(int size)
{
	m_view->setIconSize(QSize(size, size));
	m_view->verticalHeader()->setDefaultSectionSize(size + 6);
}

void MainWindow::applyMainLayout(int layout)
{
	m_mainLayout = layout;

	// Detach every movable pane so the splitters can be re-assembled in a
	// deterministic order regardless of the previous arrangement.
	QWidget *const movable[] = {
		static_cast<QWidget *>(m_folders), m_systemPane, m_softwarePane,
		static_cast<QWidget *>(m_artwork), static_cast<QWidget *>(m_rightSplitter) };
	for (QWidget *w : movable)
		w->setParent(nullptr);

	if (layout == SoftwareUnderSystems)
	{
		// folders | (systems / software) | artwork
		m_rightSplitter->addWidget(m_systemPane);
		m_rightSplitter->addWidget(m_softwarePane);
		m_rightSplitter->setStretchFactor(0, 3);
		m_rightSplitter->setStretchFactor(1, 2);

		m_splitter->addWidget(m_folders);
		m_splitter->addWidget(m_rightSplitter);
		m_splitter->addWidget(m_artwork);
		m_splitter->setStretchFactor(1, 3);
		m_splitter->setStretchFactor(2, 2);
	}
	else
	{
		// folders | systems | (artwork / software)
		m_rightSplitter->addWidget(m_artwork);
		m_rightSplitter->addWidget(m_softwarePane);
		m_rightSplitter->setStretchFactor(0, 2);
		m_rightSplitter->setStretchFactor(1, 3);

		m_splitter->addWidget(m_folders);
		m_splitter->addWidget(m_systemPane);
		m_splitter->addWidget(m_rightSplitter);
		m_splitter->setStretchFactor(1, 3);
		m_splitter->setStretchFactor(2, 2);
	}
	m_splitter->setStretchFactor(0, 0);   // folders keep their width

	// Re-detaching can hide panes; restore the software pane's expected state.
	m_softwarePane->setVisible(m_softwareModel->rowCount() > 0);
	m_folders->show();
	m_systemPane->show();
	m_artwork->show();
	m_rightSplitter->show();
}

void MainWindow::setSoftwarePaneVisible(bool visible)
{
	// The software pane lives in the right (vertical) splitter beneath the
	// artwork; showing/hiding it lets the splitter redistribute the space.
	m_softwarePane->setVisible(visible);
}

void MainWindow::onFolderSelected(const FolderFilter &filter)
{
	m_proxy->setFolderFilter(filter);
	updateStatusCount();
}

void MainWindow::onSearchTextChanged(const QString &text)
{
	m_proxy->setSearchText(text);
	updateStatusCount();
}

void MainWindow::onStatusFilterChanged()
{
	int flags = 0;
	if (m_btnWorking->isChecked())
		flags |= StatusWorking;
	if (m_btnNotWorking->isChecked())
		flags |= StatusNotWorking;
	if (m_btnAvailable->isChecked())
		flags |= StatusAvailable;
	if (m_btnUnavailable->isChecked())
		flags |= StatusUnavailable;
	m_proxy->setStatusFilter(flags);
	updateStatusCount();
}

void MainWindow::onSoftwareFilterChanged()
{
	int support = 0;
	if (m_btnSupported->isChecked())
		support |= SwSupported;
	if (m_btnPartial->isChecked())
		support |= SwPartial;
	if (m_btnUnsupported->isChecked())
		support |= SwUnsupported;
	m_softwareProxy->setSupportFilter(support);

	int avail = 0;
	if (m_btnSwAvailable->isChecked())
		avail |= SwAvailable;
	if (m_btnSwUnavailable->isChecked())
		avail |= SwUnavailable;
	m_softwareProxy->setAvailabilityFilter(avail);
}

void MainWindow::onSystemSelectionChanged()
{
	QString const system = selectedSystem();
	m_playAct->setEnabled(!system.isEmpty());
	m_propertiesAct->setEnabled(!system.isEmpty());

	// Artwork loads quickly (cached zip lookup); update it immediately.
	m_artwork->setSystem(system);

	// (Re)start the debounce; the software list refreshes once selection
	// settles.
	m_softwareModel->setEntries({});
	m_softwareTimer->start();
}

void MainWindow::onSoftwareSelectionChanged()
{
	QModelIndex const index = m_softwareView->selectionModel()->currentIndex();
	if (index.isValid())
	{
		int const sourceRow = m_softwareProxy->mapToSource(index).row();
		QString const list = m_softwareModel->listForRow(sourceRow);
		QString const software = m_softwareModel->shortNameForRow(sourceRow);
		if (!list.isEmpty() && !software.isEmpty())
		{
			m_artwork->setSoftware(list, software);
			return;
		}
	}
	// Nothing selected: fall back to the system's artwork.
	m_artwork->setSystem(selectedSystem());
}

void MainWindow::refreshSoftware()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
	{
		m_softwareLoader->cancel();
		m_softwareModel->setEntries({});
		setSoftwarePaneVisible(false);
		return;
	}

	// Enumerate + audit on a worker thread; results arrive in onSoftwareLoaded.
	m_softwareLoadSystem = system;
	m_softwareLoader->load(system);
}

void MainWindow::onSoftwareLoaded(const std::vector<qtui_software_entry> &entries)
{
	bool const hasSoftware = !entries.empty();
	m_softwareModel->setEntries(entries);
	setSoftwarePaneVisible(hasSoftware);
	if (!hasSoftware)
		return;

	m_softwareSearch->clear();   // start fresh for the new system

	// If this system's availability is already cached (and still aligns with
	// the freshly enumerated list), apply it immediately and skip the audit.
	auto const it = m_softwareAvail.constFind(m_softwareLoadSystem);
	if (it != m_softwareAvail.constEnd() && it->size() == int(entries.size()))
	{
		m_softwareLoader->cancel();   // stop the redundant background audit
		m_softwareModel->setAvailabilities(*it);
		updateStatusCount();
		return;
	}

	statusBar()->showMessage(tr("Checking software availability…"));
}

void MainWindow::onSoftwareAvailabilityReady(const QVector<int> &availability)
{
	m_softwareModel->setAvailabilities(availability);
	updateStatusCount();
	// Cache for instant re-selection (and persistence across sessions).
	if (!m_softwareLoadSystem.isEmpty() && !availability.isEmpty())
		m_softwareAvail.insert(m_softwareLoadSystem, availability);
}

QString MainWindow::selectedSystem() const
{
	if (!m_view)
		return QString();

	QModelIndex const index = m_view->selectionModel()->currentIndex();
	if (!index.isValid())
		return QString();

	return index.data(GameListModel::ShortNameRole).toString();
}

void MainWindow::runModal(const QString &label, const std::function<int ()> &runner)
{
	statusBar()->showMessage(tr("Launching %1…").arg(label));
	hide();
	QApplication::processEvents();

	int const result = runner();

	show();
	raise();
	activateWindow();
	m_view->setFocus();

	if (result != 0)
	{
		statusBar()->showMessage(tr("%1 exited with code %2").arg(label).arg(result));
		QMessageBox::warning(
				this,
				tr("Launch failed"),
				tr("Running \"%1\" failed (exit code %2).\n\n"
				   "Check that the ROMs are available and the paths are configured.")
						.arg(label).arg(result));
	}
	else
	{
		updateStatusCount();
	}
}

void MainWindow::launchSelectedSystem()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
		return;

	runModal(system, [system] { return qtui_run_system(system.toStdString()); });
}

void MainWindow::launchSelectedSoftware()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
		return;

	QModelIndex const index = m_softwareView->selectionModel()->currentIndex();
	if (!index.isValid())
		return;

	// The view sits behind a sort proxy; map back to the source row.
	int const sourceRow = m_softwareProxy->mapToSource(index).row();
	QString const software = m_softwareModel->shortNameForRow(sourceRow);
	if (software.isEmpty())
		return;

	runModal(QStringLiteral("%1 %2").arg(system, software),
			[system, software] { return qtui_run_software(system.toStdString(), software.toStdString()); });
}

void MainWindow::updateStatusCount()
{
	int const count = m_proxy ? m_proxy->rowCount() : 0;
	statusBar()->showMessage(tr("%n system(s)", nullptr, count));
}

void MainWindow::showAbout()
{
	QMessageBox::about(
			this,
			tr("About MAMEUI"),
			tr("MAMEUI – a cross-platform Qt front-end for MAME."));
}

} // namespace osd::qtui
