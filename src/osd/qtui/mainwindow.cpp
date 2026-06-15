// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mainwindow.cpp - qtui main browser window
//
//============================================================

#include "mainwindow.h"

#include "gamelistmodel.h"

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

void MainWindow::showAbout()
{
	QMessageBox::about(
			this,
			tr("About MAMEUI"),
			tr("MAMEUI – a cross-platform Qt front-end for MAME."));
}

} // namespace osd::qtui
