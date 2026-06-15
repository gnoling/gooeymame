// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mainwindow.cpp - qtui main browser window
//
//============================================================

#include "mainwindow.h"

#include "emulator.h"
#include "foldertree.h"
#include "gamelistmodel.h"
#include "gamelistproxy.h"
#include "softwaremodel.h"

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
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

} // anonymous namespace

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent)
{
	setWindowTitle(tr("MAMEUI"));
	resize(1100, 680);

	createMenus();
	createWidgets();

	updateStatusCount();
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

	fileMenu->addSeparator();

	QAction *exitAct = fileMenu->addAction(tr("E&xit"));
	exitAct->setShortcut(QKeySequence::Quit);
	connect(exitAct, &QAction::triggered, this, &QWidget::close);

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

	// Status modifiers: combinable toggles that refine the current list.
	m_btnWorking = new QPushButton(tr("Working"));
	m_btnWorking->setCheckable(true);
	m_btnWorking->setToolTip(tr("Show systems with working emulation"));
	connect(m_btnWorking, &QPushButton::toggled, this, &MainWindow::onStatusFilterChanged);

	m_btnNotWorking = new QPushButton(tr("Not working"));
	m_btnNotWorking->setCheckable(true);
	m_btnNotWorking->setToolTip(tr("Show systems with non-working emulation"));
	connect(m_btnNotWorking, &QPushButton::toggled, this, &MainWindow::onStatusFilterChanged);

	QWidget *systemPane = new QWidget;
	QVBoxLayout *systemLayout = new QVBoxLayout(systemPane);
	systemLayout->setContentsMargins(0, 0, 0, 0);
	QHBoxLayout *systemBar = new QHBoxLayout;
	systemBar->addWidget(m_search, 1);
	systemBar->addWidget(m_btnWorking);
	systemBar->addWidget(m_btnNotWorking);
	systemLayout->addLayout(systemBar);
	systemLayout->addWidget(m_view);

	// --- software list pane: [search] over the table ---
	m_softwareModel = new SoftwareModel(this);

	m_softwareProxy = new QSortFilterProxyModel(this);
	m_softwareProxy->setSourceModel(m_softwareModel);
	m_softwareProxy->setSortCaseSensitivity(Qt::CaseInsensitive);
	m_softwareProxy->setSortLocaleAware(true);
	m_softwareProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_softwareProxy->setFilterKeyColumn(-1);   // match against every column

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

	m_softwareSearch = new QLineEdit;
	m_softwareSearch->setClearButtonEnabled(true);
	m_softwareSearch->setPlaceholderText(tr("Search software…"));
	connect(m_softwareSearch, &QLineEdit::textChanged, this, [this] (const QString &text) {
		m_softwareProxy->setFilterFixedString(text.trimmed());
	});

	m_softwarePane = new QWidget;
	QVBoxLayout *softwareLayout = new QVBoxLayout(m_softwarePane);
	softwareLayout->setContentsMargins(0, 0, 0, 0);
	softwareLayout->addWidget(m_softwareSearch);
	softwareLayout->addWidget(m_softwareView);

	// --- folder tree ---
	m_folders = new FolderTree(m_model);
	m_folders->setMaximumWidth(260);
	connect(m_folders, &FolderTree::folderSelected, this, &MainWindow::onFolderSelected);

	// --- layout: folders | systems | software ---
	m_splitter = new QSplitter(Qt::Horizontal, this);
	m_splitter->addWidget(m_folders);
	m_splitter->addWidget(systemPane);
	m_splitter->addWidget(m_softwarePane);
	m_splitter->setStretchFactor(0, 0);
	m_splitter->setStretchFactor(1, 3);
	m_splitter->setStretchFactor(2, 2);
	setCentralWidget(m_splitter);

	// The software pane only appears for systems that have software lists.
	m_softwarePane->setVisible(false);

	// Debounce timer for software enumeration.
	m_softwareTimer = new QTimer(this);
	m_softwareTimer->setSingleShot(true);
	m_softwareTimer->setInterval(SOFTWARE_DEBOUNCE_MS);
	connect(m_softwareTimer, &QTimer::timeout, this, &MainWindow::refreshSoftware);
}

void MainWindow::setSoftwarePaneVisible(bool visible)
{
	if (m_softwarePane->isVisible() == visible)
		return;

	m_softwarePane->setVisible(visible);

	// Give the newly-shown software pane a reasonable share of the width;
	// the stretchable description columns refit to the new pane widths.
	if (visible)
	{
		int const folderWidth = m_folders->width();
		int const rest = qMax(400, m_splitter->width() - folderWidth);
		m_splitter->setSizes({ folderWidth, (rest * 3) / 5, (rest * 2) / 5 });
	}
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
	m_proxy->setStatusFilter(flags);
	updateStatusCount();
}

void MainWindow::onSystemSelectionChanged()
{
	m_playAct->setEnabled(!selectedSystem().isEmpty());
	// (Re)start the debounce; the software list refreshes once selection
	// settles.
	m_softwareModel->setEntries({});
	m_softwareTimer->start();
}

void MainWindow::refreshSoftware()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
	{
		m_softwareModel->setEntries({});
		setSoftwarePaneVisible(false);
		return;
	}

	// Synchronous, but only fired once selection settles (debounced).  A
	// future optimisation could move this to a worker thread.
	auto entries = qtui_enumerate_software(system.toStdString());
	bool const hasSoftware = !entries.empty();

	m_softwareModel->setEntries(std::move(entries));
	if (hasSoftware)
		m_softwareSearch->clear();   // start fresh for the new system
	setSoftwarePaneVisible(hasSoftware);
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
