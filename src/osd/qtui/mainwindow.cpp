// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mainwindow.cpp - qtui main browser window
//
//============================================================

#include "mainwindow.h"

#include "emulator.h"
#include "gamelistmodel.h"

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSortFilterProxyModel>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableView>


namespace osd::qtui {

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent)
{
	setWindowTitle(tr("MAMEUI"));
	resize(900, 600);

	createMenus();
	createGameList();

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
	connect(m_playAct, &QAction::triggered, this, &MainWindow::launchSelected);

	fileMenu->addSeparator();

	QAction *exitAct = fileMenu->addAction(tr("E&xit"));
	exitAct->setShortcut(QKeySequence::Quit);
	connect(exitAct, &QAction::triggered, this, &QWidget::close);

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	QAction *aboutAct = helpMenu->addAction(tr("&About"));
	connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::createGameList()
{
	m_model = new GameListModel(this);

	// A sort/filter proxy sits between the model and the view so that column
	// sorting (and, in later phases, search/folder filtering) works without
	// disturbing the underlying driver list.
	m_proxy = new QSortFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
	m_proxy->setSortLocaleAware(true);

	m_view = new QTableView(this);
	m_view->setModel(m_proxy);
	m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_view->setSelectionMode(QAbstractItemView::SingleSelection);
	m_view->setSortingEnabled(true);
	m_view->setAlternatingRowColors(true);
	m_view->setShowGrid(false);
	m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_view->verticalHeader()->setVisible(false);
	m_view->horizontalHeader()->setStretchLastSection(true);
	m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);

	// Double-click a row to launch that system.
	connect(m_view, &QTableView::doubleClicked, this, &MainWindow::launchSelected);

	// Keep the Play action in step with the selection.
	connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, [this] (const QItemSelection &, const QItemSelection &) {
				m_playAct->setEnabled(!selectedSystem().isEmpty());
			});

	// Sort by Description ascending to start.
	m_view->sortByColumn(GameListModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);

	// Reasonable initial column widths; the user can resize from here.
	m_view->setColumnWidth(GameListModel::COLUMN_DESCRIPTION, 320);
	m_view->setColumnWidth(GameListModel::COLUMN_NAME, 110);
	m_view->setColumnWidth(GameListModel::COLUMN_YEAR, 60);
	m_view->setColumnWidth(GameListModel::COLUMN_MANUFACTURER, 220);

	setCentralWidget(m_view);
}

void MainWindow::updateStatusCount()
{
	int const count = m_proxy ? m_proxy->rowCount() : 0;
	statusBar()->showMessage(tr("%n system(s)", nullptr, count));
}

QString MainWindow::selectedSystem() const
{
	if (!m_view)
		return QString();

	QModelIndex const index = m_view->selectionModel()->currentIndex();
	if (!index.isValid())
		return QString();

	// The proxy forwards custom roles to the source model, so this resolves
	// to the short name regardless of which column is current or how the
	// view is sorted.
	return index.data(GameListModel::ShortNameRole).toString();
}

void MainWindow::launchSelected()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
		return;

	// Modal, in-process launch (the faithful MAMEUI model): hide the browser,
	// hand the process over to the SDL OSD for the duration of the run, then
	// restore the window.  The Qt event loop is blocked while the emulator
	// runs; only one event loop is ever active at a time.
	statusBar()->showMessage(tr("Launching %1…").arg(system));
	hide();
	QApplication::processEvents();

	int const result = qtui_run_system(system.toStdString());

	show();
	raise();
	activateWindow();
	m_view->setFocus();

	if (result != 0)
	{
		statusBar()->showMessage(tr("%1 exited with code %2").arg(system).arg(result));
		QMessageBox::warning(
				this,
				tr("Launch failed"),
				tr("Running \"%1\" failed (exit code %2).\n\n"
				   "Check that the ROMs are available and the rompath is configured.")
						.arg(system).arg(result));
	}
	else
	{
		updateStatusCount();
	}
}

void MainWindow::showAbout()
{
	QMessageBox::about(
			this,
			tr("About MAMEUI"),
			tr("MAMEUI – a cross-platform Qt front-end for MAME."));
}

} // namespace osd::qtui
