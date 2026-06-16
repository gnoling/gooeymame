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
#include "familytreemodel.h"
#include "foldertree.h"
#include "frontendpaths.h"
#include "gamelistmodel.h"
#include "gamelistproxy.h"
#include "gridview.h"
#include "optionsdialog.h"
#include "softwareauditmanager.h"
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
#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#include <QtGui/QCloseEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>


namespace osd::qtui {

namespace {

// Delay between a system selection settling and enumerating its software.
// Enumeration builds the machine configuration, which is comparatively
// expensive, so we debounce rapid keyboard navigation.
constexpr int SOFTWARE_DEBOUNCE_MS = 200;


// Make two checkable actions mutually exclusive (either, or neither).
void actionsExclusive(QAction *a, QAction *b)
{
	QObject::connect(a, &QAction::toggled, b, [b] (bool on) {
		if (on) { QSignalBlocker block(b); b->setChecked(false); }
	});
	QObject::connect(b, &QAction::toggled, a, [a] (bool on) {
		if (on) { QSignalBlocker block(a); a->setChecked(false); }
	});
}

// Caption field ids (0..3) pack into a bitmask for persistence.
int captionMask(const QList<int> &ids)
{
	int mask = 0;
	for (int id : ids)
		mask |= (1 << id);
	return mask;
}

QList<int> captionIds(int mask)
{
	QList<int> ids;
	for (int id = 0; id < 4; id++)
		if (mask & (1 << id))
			ids << id;
	return ids;
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
	m_softwareAudit = new SoftwareAuditManager(this);
	connect(m_cancelAuditButton, &QPushButton::clicked, this, [this] {
		m_cancelAuditButton->setEnabled(false);
		m_audit->cancelAudit();          // only one audit runs at a time
		m_softwareAudit->cancelAudit();
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
		m_softwareAuditAct->setEnabled(true);
		updateStatusCount();
	});

	// Bulk software-availability audit: streams per-system results into the
	// software cache (the lazy per-selection caching still applies otherwise).
	connect(m_softwareAudit, &SoftwareAuditManager::systemAudited, this,
			[this] (const QString &system, const QVector<int> &availability) {
		m_softwareAvail.insert(system, availability);
	});
	connect(m_softwareAudit, &SoftwareAuditManager::progress, this, [this] (int audited, int total) {
		m_progressBar->setVisible(true);
		m_cancelAuditButton->setVisible(true);
		m_cancelAuditButton->setEnabled(true);
		m_progressBar->setRange(0, total);
		m_progressBar->setValue(audited);
		statusBar()->showMessage(tr("Auditing software… %1 of %2").arg(audited).arg(total));
	});
	connect(m_softwareAudit, &SoftwareAuditManager::finished, this, [this] {
		m_progressBar->setVisible(false);
		m_cancelAuditButton->setVisible(false);
		m_softwareAuditAct->setEnabled(true);
		m_auditAct->setEnabled(true);
		saveSoftwareCache();
		// Re-apply availability to the currently shown software list, if any.
		auto it = m_softwareAvail.constFind(m_softwareLoadSystem);
		if (it != m_softwareAvail.constEnd() && it->size() == m_softwareModel->rowCount())
			m_softwareModel->setAvailabilities(*it);
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
		// Version/region preferences may have changed the representatives.
		m_model->reloadVersionSettings();
		m_softwareModel->reloadVersionSettings();
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

	int const row = m_model->rowForName(system);
	QString const description = (row >= 0)
			? m_model->index(row, GameListModel::COLUMN_DESCRIPTION).data(Qt::DisplayRole).toString()
			: QString();

	OptionsDialog dialog(system, description, this);
	if (dialog.exec() == QDialog::Accepted)
		statusBar()->showMessage(tr("Saved properties for %1.").arg(system), 4000);
}

void MainWindow::showSystemContextMenu(const QPoint &pos)
{
	// Works for whichever system view (table or grid) emitted the request.
	auto *view = qobject_cast<QAbstractItemView *>(sender());
	if (!view)
		view = m_view;

	// Right-click selects the row under the cursor so the actions apply to it.
	QModelIndex const index = view->indexAt(pos);
	if (index.isValid())
		view->setCurrentIndex(index);
	if (selectedSystem().isEmpty())
		return;

	QMenu menu(this);
	menu.addAction(m_playAct);
	menu.addAction(m_propertiesAct);

	// Alternate versions (clone family): pick which one is the default.
	int const sourceRow = machineSourceRow(view, index);
	if (sourceRow >= 0)
	{
		QList<int> const members = m_model->familyMemberRows(sourceRow);
		if (members.size() > 1)
		{
			int const rep = m_model->representativeRow(sourceRow);
			QMenu *versions = menu.addMenu(tr("Versions"));
			for (int member : members)
			{
				QString const desc = m_model->index(member, GameListModel::COLUMN_DESCRIPTION)
						.data(Qt::DisplayRole).toString();
				QString const name = m_model->index(member, 0).data(GameListModel::ShortNameRole).toString();
				QAction *act = versions->addAction(desc);
				act->setCheckable(true);
				act->setChecked(member == rep);
				connect(act, &QAction::triggered, this, [this, sourceRow, name] {
					m_model->setVersionOverride(sourceRow, name);
					// Follow the family to its new representative's sorted position.
					int const newRep = m_model->representativeRow(sourceRow);
					selectSystemInActiveView(
							m_model->index(newRep, 0).data(GameListModel::ShortNameRole).toString());
				});
			}
		}
	}

	menu.exec(view->viewport()->mapToGlobal(pos));
}

void MainWindow::showSoftwareContextMenu(const QPoint &pos)
{
	auto *view = qobject_cast<QAbstractItemView *>(sender());
	if (!view)
		view = m_softwareView;

	QModelIndex const index = view->indexAt(pos);
	if (index.isValid())
		view->setCurrentIndex(index);

	int const sourceRow = softwareSourceRow(view, index);
	if (sourceRow < 0 || m_softwareModel->shortNameForRow(sourceRow).isEmpty())
		return;

	QMenu menu(this);
	menu.addAction(tr("Play"), this, &MainWindow::launchSelectedSoftware);
	// MAME has no per-software ini; options are overridden at the host machine.
	menu.addAction(tr("Machine Properties…"), this, &MainWindow::openProperties);

	// Alternate versions (clone family) for this software item.
	QList<int> const members = m_softwareModel->familyMemberRows(sourceRow);
	if (members.size() > 1)
	{
		int const rep = m_softwareModel->representativeRow(sourceRow);
		QMenu *versions = menu.addMenu(tr("Versions"));
		for (int member : members)
		{
			QString const desc = m_softwareModel->index(member, SoftwareModel::COLUMN_DESCRIPTION)
					.data(Qt::DisplayRole).toString();
			QString const name = m_softwareModel->shortNameForRow(member);
			QAction *act = versions->addAction(desc);
			act->setCheckable(true);
			act->setChecked(member == rep);
			connect(act, &QAction::triggered, this, [this, sourceRow, name] {
				m_softwareModel->setVersionOverride(sourceRow, name);
				selectSoftwareRow(m_softwareModel->representativeRow(sourceRow));
			});
		}
	}

	menu.exec(view->viewport()->mapToGlobal(pos));
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
	settings.setValue(QStringLiteral("folderPath"), m_folders->currentPath());
	settings.setValue(QStringLiteral("search"), m_search->text());

	// Currently selected software item (re-selected when its list reloads).
	QString swList, swName;
	QAbstractItemView *swView = activeSoftwareView();
	int const swSourceRow = (swView && swView->selectionModel())
			? softwareSourceRow(swView, swView->selectionModel()->currentIndex()) : -1;
	if (swSourceRow >= 0)
	{
		swList = m_softwareModel->listForRow(swSourceRow);
		swName = m_softwareModel->shortNameForRow(swSourceRow);
	}
	settings.setValue(QStringLiteral("softwareList"), swList);
	settings.setValue(QStringLiteral("softwareName"), swName);
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
	QString const folderPath = settings.value(QStringLiteral("folderPath")).toString();
	QString const searchText = settings.value(QStringLiteral("search")).toString();
	m_pendingSoftwareList = settings.value(QStringLiteral("softwareList")).toString();
	m_pendingSoftwareName = settings.value(QStringLiteral("softwareName")).toString();
	settings.endGroup();

	// Apply the saved icon size and tick the matching menu entry.
	applyIconSize(iconSize);
	for (QAction *act : m_iconSizeGroup->actions())
		if (act->data().toInt() == iconSize)
			act->setChecked(true);

	// Restore per-pane grid view state (group already ended, so use full keys).
	m_gridSize->setValue(settings.value(QStringLiteral("view/machineThumb"), 128).toInt());
	m_gridSource->setCurrentIndex(settings.value(QStringLiteral("view/machineSource"), 0).toInt());
	m_gridCaption->setCheckedIds(captionIds(settings.value(QStringLiteral("view/machineCaption"), 1).toInt()));
	{
		int const idx = m_viewMode->findData(settings.value(QStringLiteral("view/machineMode"), int(ViewList)).toInt());
		m_viewMode->setCurrentIndex(idx >= 0 ? idx : 0);
		setMachineViewMode(m_viewMode->currentData().toInt());
	}

	m_softwareGridSize->setValue(settings.value(QStringLiteral("view/softwareThumb"), 128).toInt());
	m_softwareGridSource->setCurrentIndex(settings.value(QStringLiteral("view/softwareSource"), 0).toInt());
	m_softwareGridCaption->setCheckedIds(captionIds(settings.value(QStringLiteral("view/softwareCaption"), 1).toInt()));
	{
		int const idx = m_softwareViewMode->findData(settings.value(QStringLiteral("view/softwareMode"), int(ViewList)).toInt());
		m_softwareViewMode->setCurrentIndex(idx >= 0 ? idx : 0);
		setSoftwareViewMode(m_softwareViewMode->currentData().toInt());
	}

	// Restore all machine-list filters.  Block signals while setting the action
	// states so an early toggle doesn't write back the not-yet-restored others,
	// then apply once.
	QAction *const filterActs[] = {
		m_actWorking, m_actNotWorking, m_actAvailable, m_actUnavailable,
		m_actHideClones, m_actHideBootlegs, m_actHideHacks, m_actHidePrototypes };
	for (QAction *act : filterActs)
		act->blockSignals(true);
	m_actWorking->setChecked(settings.value(QStringLiteral("filters/working"), false).toBool());
	m_actNotWorking->setChecked(settings.value(QStringLiteral("filters/notWorking"), false).toBool());
	m_actAvailable->setChecked(settings.value(QStringLiteral("filters/available"), false).toBool());
	m_actUnavailable->setChecked(settings.value(QStringLiteral("filters/unavailable"), false).toBool());
	m_actHideClones->setChecked(settings.value(QStringLiteral("filters/hideClones"), false).toBool());
	m_actHideBootlegs->setChecked(settings.value(QStringLiteral("filters/hideBootlegs"), false).toBool());
	m_actHideHacks->setChecked(settings.value(QStringLiteral("filters/hideHacks"), false).toBool());
	m_actHidePrototypes->setChecked(settings.value(QStringLiteral("filters/hidePrototypes"), false).toBool());
	for (QAction *act : filterActs)
		act->blockSignals(false);
	onStatusFilterChanged();
	onVersionFilterChanged();

	// Software-pane filters (same block-then-apply-once pattern).
	QAction *const swActs[] = {
		m_actSwSupported, m_actSwPartial, m_actSwUnsupported, m_actSwAvailable, m_actSwUnavailable,
		m_actSwHideClones, m_actSwHideBootlegs, m_actSwHideHacks, m_actSwHidePrototypes };
	for (QAction *act : swActs)
		act->blockSignals(true);
	m_actSwSupported->setChecked(settings.value(QStringLiteral("filters/swSupported"), false).toBool());
	m_actSwPartial->setChecked(settings.value(QStringLiteral("filters/swPartial"), false).toBool());
	m_actSwUnsupported->setChecked(settings.value(QStringLiteral("filters/swUnsupported"), false).toBool());
	m_actSwAvailable->setChecked(settings.value(QStringLiteral("filters/swAvailable"), false).toBool());
	m_actSwUnavailable->setChecked(settings.value(QStringLiteral("filters/swUnavailable"), false).toBool());
	m_actSwHideClones->setChecked(settings.value(QStringLiteral("filters/swHideClones"), false).toBool());
	m_actSwHideBootlegs->setChecked(settings.value(QStringLiteral("filters/swHideBootlegs"), false).toBool());
	m_actSwHideHacks->setChecked(settings.value(QStringLiteral("filters/swHideHacks"), false).toBool());
	m_actSwHidePrototypes->setChecked(settings.value(QStringLiteral("filters/swHidePrototypes"), false).toBool());
	for (QAction *act : swActs)
		act->blockSignals(false);
	onSoftwareFilterChanged();

	// Restore the search text and selected folder (each applies its filter via
	// its normal signal) before re-selecting the system under those filters.
	if (!searchText.isEmpty())
		m_search->setText(searchText);
	if (!folderPath.isEmpty())
		m_folders->selectPath(folderPath);

	// Re-select the last system in whichever view mode is active.
	if (!selected.isEmpty())
		selectSystemInActiveView(selected);
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

	// Machine-list filters: shared QActions used by both the View ▸ Filters
	// menu and the "Filters" button in the list's bar.
	auto makeFilterAction = [this] (const QString &text, const QString &tip) {
		QAction *act = new QAction(text, this);
		act->setCheckable(true);
		act->setToolTip(tip);
		return act;
	};
	m_actWorking = makeFilterAction(tr("Working"), tr("Show systems with working emulation"));
	m_actNotWorking = makeFilterAction(tr("Not working"), tr("Show systems with non-working emulation"));
	m_actAvailable = makeFilterAction(tr("Available"), tr("Show systems whose ROMs are present"));
	m_actUnavailable = makeFilterAction(tr("Unavailable"), tr("Show systems whose ROMs are missing"));
	m_actHideClones = makeFilterAction(tr("Hide clones"), tr("Show only each family's primary version"));
	m_actHideBootlegs = makeFilterAction(tr("Hide bootlegs"), tr("Hide bootleg sets"));
	m_actHideHacks = makeFilterAction(tr("Hide hacks && homebrew"), tr("Hide hacks and unofficial/homebrew sets"));
	m_actHidePrototypes = makeFilterAction(tr("Hide prototypes"), tr("Hide prototype/incomplete sets"));

	actionsExclusive(m_actWorking, m_actNotWorking);
	actionsExclusive(m_actAvailable, m_actUnavailable);
	for (QAction *act : { m_actWorking, m_actNotWorking, m_actAvailable, m_actUnavailable })
		connect(act, &QAction::toggled, this, &MainWindow::onStatusFilterChanged);
	for (QAction *act : { m_actHideClones, m_actHideBootlegs, m_actHideHacks, m_actHidePrototypes })
		connect(act, &QAction::toggled, this, &MainWindow::onVersionFilterChanged);

	QMenu *filtersMenu = viewMenu->addMenu(tr("&Filters"));
	filtersMenu->addAction(m_actWorking);
	filtersMenu->addAction(m_actNotWorking);
	filtersMenu->addAction(m_actAvailable);
	filtersMenu->addAction(m_actUnavailable);
	filtersMenu->addSeparator();
	filtersMenu->addAction(m_actHideClones);
	filtersMenu->addAction(m_actHideBootlegs);
	filtersMenu->addAction(m_actHideHacks);
	filtersMenu->addAction(m_actHidePrototypes);

	// Software-list filters (same pattern as the machine list).
	m_actSwSupported = makeFilterAction(tr("Supported"), tr("Show fully supported software"));
	m_actSwPartial = makeFilterAction(tr("Partial"), tr("Show partially supported software"));
	m_actSwUnsupported = makeFilterAction(tr("Unsupported"), tr("Show unsupported software"));
	m_actSwAvailable = makeFilterAction(tr("Available"), tr("Show software whose ROMs are present"));
	m_actSwUnavailable = makeFilterAction(tr("Unavailable"), tr("Show software whose ROMs are missing"));
	m_actSwHideClones = makeFilterAction(tr("Hide clones"), tr("Show only each software family's primary version"));
	m_actSwHideBootlegs = makeFilterAction(tr("Hide bootlegs"), tr("Hide bootleg software"));
	m_actSwHideHacks = makeFilterAction(tr("Hide hacks && homebrew"), tr("Hide hacks/homebrew software"));
	m_actSwHidePrototypes = makeFilterAction(tr("Hide prototypes"), tr("Hide prototype software"));

	actionsExclusive(m_actSwAvailable, m_actSwUnavailable);
	for (QAction *act : { m_actSwSupported, m_actSwPartial, m_actSwUnsupported,
			m_actSwAvailable, m_actSwUnavailable, m_actSwHideClones,
			m_actSwHideBootlegs, m_actSwHideHacks, m_actSwHidePrototypes })
		connect(act, &QAction::toggled, this, &MainWindow::onSoftwareFilterChanged);

	QMenu *swFiltersMenu = viewMenu->addMenu(tr("Soft&ware Filters"));
	swFiltersMenu->addAction(m_actSwSupported);
	swFiltersMenu->addAction(m_actSwPartial);
	swFiltersMenu->addAction(m_actSwUnsupported);
	swFiltersMenu->addAction(m_actSwAvailable);
	swFiltersMenu->addAction(m_actSwUnavailable);
	swFiltersMenu->addSeparator();
	swFiltersMenu->addAction(m_actSwHideClones);
	swFiltersMenu->addAction(m_actSwHideBootlegs);
	swFiltersMenu->addAction(m_actSwHideHacks);
	swFiltersMenu->addAction(m_actSwHidePrototypes);

	QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
	QAction *optionsAct = toolsMenu->addAction(tr("&Options…"));
	connect(optionsAct, &QAction::triggered, this, &MainWindow::openOptions);
	toolsMenu->addSeparator();
	m_auditAct = toolsMenu->addAction(tr("&Refresh ROM Availability"));
	connect(m_auditAct, &QAction::triggered, this, [this] {
		if (m_audit && !m_audit->isRunning() && !m_softwareAudit->isRunning())
		{
			// ROMs may have changed; the cached software availability is stale.
			clearSoftwareCache();
			m_auditAct->setEnabled(false);
			m_audit->startAudit();
		}
	});

	m_softwareAuditAct = toolsMenu->addAction(tr("Refresh &Software Availability (all)"));
	connect(m_softwareAuditAct, &QAction::triggered, this, [this] {
		if (m_softwareAudit && !m_softwareAudit->isRunning() && !m_audit->isRunning())
		{
			m_softwareAuditAct->setEnabled(false);
			m_auditAct->setEnabled(false);
			m_softwareAudit->startAudit();
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

	// Re-filter when clone-family representatives change (version preference).
	connect(m_model, &GameListModel::versionsChanged, this, [this] {
		m_proxy->invalidate();
		invalidateMachineViews();
		updateStatusCount();
	});

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
	// All columns interactively resizable (including Description); the last
	// section absorbs leftover width so the row still fills the pane.
	m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_view->horizontalHeader()->setStretchLastSection(true);
	m_view->sortByColumn(GameListModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
	m_view->setColumnWidth(GameListModel::COLUMN_DESCRIPTION, 320);
	m_view->setColumnWidth(GameListModel::COLUMN_NAME, 100);
	m_view->setColumnWidth(GameListModel::COLUMN_YEAR, 56);
	m_view->setColumnWidth(GameListModel::COLUMN_MANUFACTURER, 200);
	m_view->setColumnWidth(GameListModel::COLUMN_STATUS, 110);

	connect(m_view, &QTableView::doubleClicked, this, &MainWindow::launchSelectedSystem);
	connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSystemSelectionChanged);
	m_view->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_view, &QWidget::customContextMenuRequested,
			this, &MainWindow::showSystemContextMenu);

	m_search = new QLineEdit;
	m_search->setClearButtonEnabled(true);
	m_search->setPlaceholderText(tr("Search systems…"));
	connect(m_search, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

	// The status/version filters (created in createMenus) are reached from a
	// single "Filters" button that drops the same shared actions as a menu,
	// keeping the bar uncluttered.
	QToolButton *filtersButton = new QToolButton;
	filtersButton->setText(tr("Filters"));
	filtersButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	filtersButton->setPopupMode(QToolButton::InstantPopup);
	QMenu *barFiltersMenu = new QMenu(filtersButton);
	barFiltersMenu->addAction(m_actWorking);
	barFiltersMenu->addAction(m_actNotWorking);
	barFiltersMenu->addAction(m_actAvailable);
	barFiltersMenu->addAction(m_actUnavailable);
	barFiltersMenu->addSeparator();
	barFiltersMenu->addAction(m_actHideClones);
	barFiltersMenu->addAction(m_actHideBootlegs);
	barFiltersMenu->addAction(m_actHideHacks);
	barFiltersMenu->addAction(m_actHidePrototypes);
	filtersButton->setMenu(barFiltersMenu);

	// Flat grid: every member as a tile (shares the flat proxy + selection).
	m_grid = new GridView;
	m_grid->setModel(m_proxy);
	m_grid->setSelectionModel(m_view->selectionModel());
	connect(m_grid, &QAbstractItemView::doubleClicked, this, &MainWindow::launchSelectedSystem);
	m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_grid, &QWidget::customContextMenuRequested, this, &MainWindow::showSystemContextMenu);

	// Grouped grid: one tile per clone family (representatives only), via a
	// proxy that defers the rest of the filtering to the flat proxy.
	m_gridProxy = new RepresentativeProxy(m_model, m_proxy,
			[this] (int row) { return m_model->isRepresentative(row); }, this);
	m_gridProxy->sort(GameListModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
	m_gridGrouped = new GridView;
	m_gridGrouped->setModel(m_gridProxy);
	connect(m_gridGrouped, &QAbstractItemView::doubleClicked, this, &MainWindow::launchSelectedSystem);
	connect(m_gridGrouped->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSystemSelectionChanged);
	m_gridGrouped->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_gridGrouped, &QWidget::customContextMenuRequested, this, &MainWindow::showSystemContextMenu);

	// Grouped (tree) view: families as expandable groups, filtered in lock-step
	// with the flat proxy.
	m_treeModel = new FamilyTreeModel(m_model,
			[this] { return m_model->groupRows(); },
			[this] (int rep) { QList<int> m = m_model->familyMemberRows(rep); if (!m.isEmpty()) m.removeFirst(); return m; },
			this);
	m_treeProxy = new TreeFilterProxy(m_treeModel, m_proxy, m_model, this);
	m_tree = new QTreeView;
	m_tree->setModel(m_treeProxy);
	m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_tree->setSortingEnabled(true);
	m_tree->setAlternatingRowColors(true);
	m_tree->setUniformRowHeights(true);
	m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_tree->sortByColumn(GameListModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
	m_tree->header()->setStretchLastSection(true);
	m_tree->setColumnWidth(GameListModel::COLUMN_DESCRIPTION, 320);
	connect(m_tree, &QTreeView::doubleClicked, this, &MainWindow::launchSelectedSystem);
	connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSystemSelectionChanged);
	m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_tree, &QWidget::customContextMenuRequested, this, &MainWindow::showSystemContextMenu);
	// Rebuild the grouping when representatives change.
	connect(m_model, &GameListModel::versionsChanged, this, [this] {
		m_treeModel->rebuild();
		m_treeProxy->invalidate();
	});

	m_systemStack = new QStackedWidget;
	m_systemStack->addWidget(m_view);          // index 0 = List
	m_systemStack->addWidget(m_tree);          // index 1 = Grouped
	m_systemStack->addWidget(m_grid);          // index 2 = Grid
	m_systemStack->addWidget(m_gridGrouped);   // index 3 = Grid Grouped

	m_viewMode = new QComboBox;
	m_viewMode->addItem(tr("List"), ViewList);
	m_viewMode->addItem(tr("Grouped"), ViewGrouped);
	m_viewMode->addItem(tr("Grid"), ViewGrid);
	m_viewMode->addItem(tr("Grid Grouped"), ViewGridGrouped);
	connect(m_viewMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] (int i) {
		setMachineViewMode(m_viewMode->itemData(i).toInt());
	});
	m_gridBar = buildGridBar(m_gridSize, m_gridSource, m_gridCaption);
	m_gridBar->setVisible(false);
	connect(m_gridSize, &QSlider::valueChanged, this, [this] (int v) {
		m_grid->setThumbnailSize(v);
		m_gridGrouped->setThumbnailSize(v);
		QSettings().setValue(QStringLiteral("view/machineThumb"), v);
	});
	connect(m_gridSource, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] (int i) {
		applyMachineThumbSource();
		QSettings().setValue(QStringLiteral("view/machineSource"), i);
	});
	connect(m_gridCaption, &CheckableComboBox::checkedChanged, this, [this] {
		m_grid->setCaptionColumns(m_gridCaption->checkedIds());
		m_gridGrouped->setCaptionColumns(m_gridCaption->checkedIds());
		QSettings().setValue(QStringLiteral("view/machineCaption"), captionMask(m_gridCaption->checkedIds()));
	});

	m_systemPane = new QWidget;
	QVBoxLayout *systemLayout = new QVBoxLayout(m_systemPane);
	systemLayout->setContentsMargins(0, 0, 0, 0);

	// Quick-filter bar over the system list.
	QHBoxLayout *systemBar = new QHBoxLayout;
	systemBar->addWidget(m_search, 1);
	systemBar->addWidget(filtersButton);
	systemBar->addWidget(m_viewMode);
	systemLayout->addLayout(systemBar);
	systemLayout->addWidget(m_gridBar);
	systemLayout->addWidget(m_systemStack);

	// --- software list pane: quick-filter bar over the table ---
	m_softwareModel = new SoftwareModel(this);
	connect(m_softwareModel, &SoftwareModel::versionsChanged, this, [this] {
		if (m_softwareProxy)
			m_softwareProxy->invalidate();
	});

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
	m_softwareView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
	m_softwareView->horizontalHeader()->setStretchLastSection(true);
	m_softwareView->setColumnWidth(SoftwareModel::COLUMN_DESCRIPTION, 280);
	connect(m_softwareView, &QTableView::doubleClicked, this, &MainWindow::launchSelectedSoftware);
	connect(m_softwareView->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSoftwareSelectionChanged);
	m_softwareView->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_softwareView, &QWidget::customContextMenuRequested,
			this, &MainWindow::showSoftwareContextMenu);

	// Flat software grid: every member as a tile (shares the flat proxy).
	m_softwareGrid = new GridView;
	m_softwareGrid->setModel(m_softwareProxy);
	m_softwareGrid->setSelectionModel(m_softwareView->selectionModel());
	connect(m_softwareGrid, &QAbstractItemView::doubleClicked, this, &MainWindow::launchSelectedSoftware);
	m_softwareGrid->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_softwareGrid, &QWidget::customContextMenuRequested,
			this, &MainWindow::showSoftwareContextMenu);

	// Grouped software grid: one tile per family (representatives only).
	m_swGridProxy = new RepresentativeProxy(m_softwareModel, m_softwareProxy,
			[this] (int row) { return m_softwareModel->isRepresentative(row); }, this);
	m_swGridProxy->sort(SoftwareModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
	m_swGridGrouped = new GridView;
	m_swGridGrouped->setModel(m_swGridProxy);
	connect(m_swGridGrouped, &QAbstractItemView::doubleClicked, this, &MainWindow::launchSelectedSoftware);
	connect(m_swGridGrouped->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSoftwareSelectionChanged);
	m_swGridGrouped->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_swGridGrouped, &QWidget::customContextMenuRequested,
			this, &MainWindow::showSoftwareContextMenu);

	// Grouped (tree) view for software.
	m_swTreeModel = new FamilyTreeModel(m_softwareModel,
			[this] { return m_softwareModel->groupRows(); },
			[this] (int rep) { QList<int> m = m_softwareModel->familyMemberRows(rep); if (!m.isEmpty()) m.removeFirst(); return m; },
			this);
	m_swTreeProxy = new TreeFilterProxy(m_swTreeModel, m_softwareProxy, m_softwareModel, this);
	m_softwareTree = new QTreeView;
	m_softwareTree->setModel(m_swTreeProxy);
	m_softwareTree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_softwareTree->setSelectionMode(QAbstractItemView::SingleSelection);
	m_softwareTree->setSortingEnabled(true);
	m_softwareTree->setAlternatingRowColors(true);
	m_softwareTree->setUniformRowHeights(true);
	m_softwareTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_softwareTree->header()->setStretchLastSection(true);
	m_softwareTree->setColumnWidth(SoftwareModel::COLUMN_DESCRIPTION, 280);
	connect(m_softwareTree, &QTreeView::doubleClicked, this, &MainWindow::launchSelectedSoftware);
	connect(m_softwareTree->selectionModel(), &QItemSelectionModel::selectionChanged,
			this, &MainWindow::onSoftwareSelectionChanged);
	m_softwareTree->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_softwareTree, &QWidget::customContextMenuRequested,
			this, &MainWindow::showSoftwareContextMenu);
	// Rebuild the software grouping when its list reloads or representatives change.
	connect(m_softwareModel, &QAbstractItemModel::modelReset, this, [this] {
		m_swTreeModel->rebuild();
		invalidateSoftwareViews();
	});
	connect(m_softwareModel, &SoftwareModel::versionsChanged, this, [this] {
		m_swTreeModel->rebuild();
		invalidateSoftwareViews();
	});

	m_softwareStack = new QStackedWidget;
	m_softwareStack->addWidget(m_softwareView);     // index 0 = List
	m_softwareStack->addWidget(m_softwareTree);     // index 1 = Grouped
	m_softwareStack->addWidget(m_softwareGrid);     // index 2 = Grid
	m_softwareStack->addWidget(m_swGridGrouped);    // index 3 = Grid Grouped

	m_softwareSearch = new QLineEdit;
	m_softwareSearch->setClearButtonEnabled(true);
	m_softwareSearch->setPlaceholderText(tr("Search software…"));
	connect(m_softwareSearch, &QLineEdit::textChanged, this, [this] (const QString &text) {
		m_softwareProxy->setSearchText(text);
		invalidateSoftwareViews();
	});

	// Software runs through a background loader so per-entry ROM auditing
	// never blocks the UI and selection changes cancel in-flight work.
	m_softwareLoader = new SoftwareLoader(this);
	connect(m_softwareLoader, &SoftwareLoader::loaded, this, &MainWindow::onSoftwareLoaded);
	connect(m_softwareLoader, &SoftwareLoader::availabilityReady, this, &MainWindow::onSoftwareAvailabilityReady);

	// Software filters consolidated under a "Filters" button (shared actions).
	QToolButton *swFiltersButton = new QToolButton;
	swFiltersButton->setText(tr("Filters"));
	swFiltersButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
	swFiltersButton->setPopupMode(QToolButton::InstantPopup);
	QMenu *swBarFiltersMenu = new QMenu(swFiltersButton);
	swBarFiltersMenu->addAction(m_actSwSupported);
	swBarFiltersMenu->addAction(m_actSwPartial);
	swBarFiltersMenu->addAction(m_actSwUnsupported);
	swBarFiltersMenu->addAction(m_actSwAvailable);
	swBarFiltersMenu->addAction(m_actSwUnavailable);
	swBarFiltersMenu->addSeparator();
	swBarFiltersMenu->addAction(m_actSwHideClones);
	swBarFiltersMenu->addAction(m_actSwHideBootlegs);
	swBarFiltersMenu->addAction(m_actSwHideHacks);
	swBarFiltersMenu->addAction(m_actSwHidePrototypes);
	swFiltersButton->setMenu(swBarFiltersMenu);

	m_softwareViewMode = new QComboBox;
	m_softwareViewMode->addItem(tr("List"), ViewList);
	m_softwareViewMode->addItem(tr("Grouped"), ViewGrouped);
	m_softwareViewMode->addItem(tr("Grid"), ViewGrid);
	m_softwareViewMode->addItem(tr("Grid Grouped"), ViewGridGrouped);
	connect(m_softwareViewMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] (int i) {
		setSoftwareViewMode(m_softwareViewMode->itemData(i).toInt());
	});
	m_softwareGridBar = buildGridBar(m_softwareGridSize, m_softwareGridSource, m_softwareGridCaption);
	m_softwareGridBar->setVisible(false);
	connect(m_softwareGridSize, &QSlider::valueChanged, this, [this] (int v) {
		m_softwareGrid->setThumbnailSize(v);
		m_swGridGrouped->setThumbnailSize(v);
		QSettings().setValue(QStringLiteral("view/softwareThumb"), v);
	});
	connect(m_softwareGridSource, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] (int i) {
		applySoftwareThumbSource();
		QSettings().setValue(QStringLiteral("view/softwareSource"), i);
	});
	connect(m_softwareGridCaption, &CheckableComboBox::checkedChanged, this, [this] {
		m_softwareGrid->setCaptionColumns(m_softwareGridCaption->checkedIds());
		m_swGridGrouped->setCaptionColumns(m_softwareGridCaption->checkedIds());
		QSettings().setValue(QStringLiteral("view/softwareCaption"), captionMask(m_softwareGridCaption->checkedIds()));
	});

	m_softwarePane = new QWidget;
	QVBoxLayout *softwareLayout = new QVBoxLayout(m_softwarePane);
	softwareLayout->setContentsMargins(0, 0, 0, 0);
	QHBoxLayout *softwareBar = new QHBoxLayout;
	softwareBar->addWidget(m_softwareSearch, 1);
	softwareBar->addWidget(swFiltersButton);
	softwareBar->addWidget(m_softwareViewMode);
	softwareLayout->addLayout(softwareBar);
	softwareLayout->addWidget(m_softwareGridBar);
	softwareLayout->addWidget(m_softwareStack);

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

QWidget *MainWindow::buildGridBar(QSlider *&size, QComboBox *&source, CheckableComboBox *&caption)
{
	QWidget *bar = new QWidget;
	QHBoxLayout *h = new QHBoxLayout(bar);
	h->setContentsMargins(0, 0, 0, 0);

	h->addWidget(new QLabel(tr("Size:")));
	size = new QSlider(Qt::Horizontal);
	size->setRange(64, 256);
	size->setValue(128);
	size->setMaximumWidth(140);
	h->addWidget(size);

	h->addWidget(new QLabel(tr("Image:")));
	source = new QComboBox;
	for (std::size_t i = 0; i < THUMBNAIL_SOURCE_COUNT; i++)
		source->addItem(tr(THUMBNAIL_SOURCES[i].label));
	h->addWidget(source);

	h->addWidget(new QLabel(tr("Caption:")));
	caption = new CheckableComboBox;
	caption->addCheckItem(tr("Description"), 0, true);
	caption->addCheckItem(tr("Short name"), 1, false);
	caption->addCheckItem(tr("Year"), 2, false);
	caption->addCheckItem(tr("Maker"), 3, false);
	h->addWidget(caption);

	h->addStretch();
	return bar;
}

void MainWindow::applyMachineThumbSource()
{
	int const i = m_gridSource->currentIndex();
	if (i >= 0 && i < int(THUMBNAIL_SOURCE_COUNT))
		m_model->setThumbnailSource(QString::fromLatin1(THUMBNAIL_SOURCES[i].machineKey));
}

void MainWindow::applySoftwareThumbSource()
{
	int const i = m_softwareGridSource->currentIndex();
	if (i >= 0 && i < int(THUMBNAIL_SOURCE_COUNT))
		m_softwareModel->setThumbnailSource(
				QString::fromLatin1(THUMBNAIL_SOURCES[i].softwareKey),
				QString::fromLatin1(THUMBNAIL_SOURCES[i].machineKey));
}

void MainWindow::setMachineViewMode(int mode)
{
	QString const keep = selectedSystem();   // carry selection across modes
	m_systemStack->setCurrentIndex(mode);
	bool const gridMode = (mode == ViewGrid || mode == ViewGridGrouped);
	m_gridBar->setVisible(gridMode);
	if (gridMode)
	{
		for (GridView *g : { m_grid, m_gridGrouped })
		{
			g->setThumbnailSize(m_gridSize->value());
			g->setCaptionColumns(m_gridCaption->checkedIds());
		}
		applyMachineThumbSource();
	}
	if (!keep.isEmpty())
		selectSystemInActiveView(keep);
	QSettings().setValue(QStringLiteral("view/machineMode"), mode);
	onSystemSelectionChanged();
}

void MainWindow::setSoftwareViewMode(int mode)
{
	m_softwareStack->setCurrentIndex(mode);
	bool const gridMode = (mode == ViewGrid || mode == ViewGridGrouped);
	m_softwareGridBar->setVisible(gridMode);
	if (gridMode)
	{
		for (GridView *g : { m_softwareGrid, m_swGridGrouped })
		{
			g->setThumbnailSize(m_softwareGridSize->value());
			g->setCaptionColumns(m_softwareGridCaption->checkedIds());
		}
		applySoftwareThumbSource();
	}
	QSettings().setValue(QStringLiteral("view/softwareMode"), mode);
}

QAbstractItemView *MainWindow::activeMachineView() const
{
	return qobject_cast<QAbstractItemView *>(m_systemStack->currentWidget());
}

QAbstractItemView *MainWindow::activeSoftwareView() const
{
	return qobject_cast<QAbstractItemView *>(m_softwareStack->currentWidget());
}

int MainWindow::machineSourceRow(QAbstractItemView *view, const QModelIndex &viewIndex) const
{
	if (!viewIndex.isValid())
		return -1;
	if (view == m_tree)
		return m_treeModel->sourceRow(m_treeProxy->mapToSource(viewIndex));
	if (view == m_gridGrouped)
		return m_gridProxy->mapToSource(viewIndex).row();
	return m_proxy->mapToSource(viewIndex).row();   // table or flat grid
}

int MainWindow::softwareSourceRow(QAbstractItemView *view, const QModelIndex &viewIndex) const
{
	if (!viewIndex.isValid())
		return -1;
	if (view == m_softwareTree)
		return m_swTreeModel->sourceRow(m_swTreeProxy->mapToSource(viewIndex));
	if (view == m_swGridGrouped)
		return m_swGridProxy->mapToSource(viewIndex).row();
	return m_softwareProxy->mapToSource(viewIndex).row();
}

void MainWindow::selectSystemInActiveView(const QString &shortName)
{
	int row = m_model->rowForName(shortName);
	if (row < 0)
		return;
	QAbstractItemView *view = activeMachineView();
	if (!view)
		return;
	QModelIndex viewIndex;
	if (view == m_tree)
	{
		viewIndex = m_treeProxy->mapFromSource(m_treeModel->indexForSourceRow(row));
	}
	else if (view == m_gridGrouped)
	{
		row = m_model->representativeRow(row);   // grouped grid shows representatives only
		viewIndex = m_gridProxy->mapFromSource(m_model->index(row, 0));
	}
	else
	{
		viewIndex = m_proxy->mapFromSource(m_model->index(row, 0));   // table or flat grid
	}
	if (viewIndex.isValid())
	{
		view->setCurrentIndex(viewIndex);
		view->scrollTo(viewIndex, QAbstractItemView::PositionAtCenter);
	}
}

void MainWindow::selectSoftwareRow(int sourceRow)
{
	if (sourceRow < 0)
		return;
	QAbstractItemView *view = activeSoftwareView();
	if (!view)
		return;
	QModelIndex viewIndex;
	if (view == m_softwareTree)
	{
		viewIndex = m_swTreeProxy->mapFromSource(m_swTreeModel->indexForSourceRow(sourceRow));
	}
	else if (view == m_swGridGrouped)
	{
		sourceRow = m_softwareModel->representativeRow(sourceRow);
		viewIndex = m_swGridProxy->mapFromSource(m_softwareModel->index(sourceRow, 0));
	}
	else
	{
		viewIndex = m_softwareProxy->mapFromSource(m_softwareModel->index(sourceRow, 0));
	}
	if (viewIndex.isValid())
	{
		view->setCurrentIndex(viewIndex);
		view->scrollTo(viewIndex, QAbstractItemView::PositionAtCenter);
	}
}

void MainWindow::invalidateMachineViews()
{
	if (m_treeProxy)
		m_treeProxy->invalidate();
	if (m_gridProxy)
		m_gridProxy->invalidate();
}

void MainWindow::invalidateSoftwareViews()
{
	if (m_swTreeProxy)
		m_swTreeProxy->invalidate();
	if (m_swGridProxy)
		m_swGridProxy->invalidate();
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
	invalidateMachineViews();
	updateStatusCount();
}

void MainWindow::onSearchTextChanged(const QString &text)
{
	m_proxy->setSearchText(text);
	invalidateMachineViews();
	updateStatusCount();
}

void MainWindow::onStatusFilterChanged()
{
	int flags = 0;
	if (m_actWorking->isChecked())
		flags |= StatusWorking;
	if (m_actNotWorking->isChecked())
		flags |= StatusNotWorking;
	if (m_actAvailable->isChecked())
		flags |= StatusAvailable;
	if (m_actUnavailable->isChecked())
		flags |= StatusUnavailable;
	m_proxy->setStatusFilter(flags);
	invalidateMachineViews();

	QSettings settings;
	settings.setValue(QStringLiteral("filters/working"), m_actWorking->isChecked());
	settings.setValue(QStringLiteral("filters/notWorking"), m_actNotWorking->isChecked());
	settings.setValue(QStringLiteral("filters/available"), m_actAvailable->isChecked());
	settings.setValue(QStringLiteral("filters/unavailable"), m_actUnavailable->isChecked());

	updateStatusCount();
}

void MainWindow::onVersionFilterChanged()
{
	m_proxy->setHideClones(m_actHideClones->isChecked());
	m_proxy->setHideBootlegs(m_actHideBootlegs->isChecked());
	m_proxy->setHideHacks(m_actHideHacks->isChecked());
	m_proxy->setHidePrototypes(m_actHidePrototypes->isChecked());
	invalidateMachineViews();

	QSettings settings;
	settings.setValue(QStringLiteral("filters/hideClones"), m_actHideClones->isChecked());
	settings.setValue(QStringLiteral("filters/hideBootlegs"), m_actHideBootlegs->isChecked());
	settings.setValue(QStringLiteral("filters/hideHacks"), m_actHideHacks->isChecked());
	settings.setValue(QStringLiteral("filters/hidePrototypes"), m_actHidePrototypes->isChecked());

	updateStatusCount();
}

void MainWindow::onSoftwareFilterChanged()
{
	int support = 0;
	if (m_actSwSupported->isChecked())
		support |= SwSupported;
	if (m_actSwPartial->isChecked())
		support |= SwPartial;
	if (m_actSwUnsupported->isChecked())
		support |= SwUnsupported;
	m_softwareProxy->setSupportFilter(support);

	int avail = 0;
	if (m_actSwAvailable->isChecked())
		avail |= SwAvailable;
	if (m_actSwUnavailable->isChecked())
		avail |= SwUnavailable;
	m_softwareProxy->setAvailabilityFilter(avail);

	m_softwareProxy->setHideClones(m_actSwHideClones->isChecked());
	m_softwareProxy->setHideBootlegs(m_actSwHideBootlegs->isChecked());
	m_softwareProxy->setHideHacks(m_actSwHideHacks->isChecked());
	m_softwareProxy->setHidePrototypes(m_actSwHidePrototypes->isChecked());
	invalidateSoftwareViews();

	QSettings settings;
	settings.setValue(QStringLiteral("filters/swSupported"), m_actSwSupported->isChecked());
	settings.setValue(QStringLiteral("filters/swPartial"), m_actSwPartial->isChecked());
	settings.setValue(QStringLiteral("filters/swUnsupported"), m_actSwUnsupported->isChecked());
	settings.setValue(QStringLiteral("filters/swAvailable"), m_actSwAvailable->isChecked());
	settings.setValue(QStringLiteral("filters/swUnavailable"), m_actSwUnavailable->isChecked());
	settings.setValue(QStringLiteral("filters/swHideClones"), m_actSwHideClones->isChecked());
	settings.setValue(QStringLiteral("filters/swHideBootlegs"), m_actSwHideBootlegs->isChecked());
	settings.setValue(QStringLiteral("filters/swHideHacks"), m_actSwHideHacks->isChecked());
	settings.setValue(QStringLiteral("filters/swHidePrototypes"), m_actSwHidePrototypes->isChecked());
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

void MainWindow::selectPendingSoftware()
{
	if (m_pendingSoftwareName.isEmpty())
		return;

	QAbstractItemView *view = activeSoftwareView();
	for (int row = 0; view && row < m_softwareModel->rowCount(); row++)
	{
		if (m_softwareModel->shortNameForRow(row) == m_pendingSoftwareName
				&& m_softwareModel->listForRow(row) == m_pendingSoftwareList)
		{
			QModelIndex viewIndex;
			if (view == m_softwareTree)
				viewIndex = m_swTreeProxy->mapFromSource(m_swTreeModel->indexForSourceRow(row));
			else
				viewIndex = m_softwareProxy->mapFromSource(
						m_softwareModel->index(row, SoftwareModel::COLUMN_DESCRIPTION));
			if (viewIndex.isValid())
			{
				view->setCurrentIndex(viewIndex);
				// Defer the scroll: the just-shown view hasn't laid out its rows
				// yet (large lists), so an immediate scrollTo lands off-target.
				QPersistentModelIndex const persistent(viewIndex);
				QTimer::singleShot(0, this, [this, view, persistent] {
					if (persistent.isValid())
						view->scrollTo(persistent, QAbstractItemView::PositionAtCenter);
				});
			}
			break;
		}
	}

	// One-shot: applies only to the first load after a session restore.
	m_pendingSoftwareList.clear();
	m_pendingSoftwareName.clear();
}

void MainWindow::onSoftwareSelectionChanged()
{
	QAbstractItemView *view = activeSoftwareView();
	int const sourceRow = (view && view->selectionModel())
			? softwareSourceRow(view, view->selectionModel()->currentIndex()) : -1;
	if (sourceRow >= 0)
	{
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
	m_softwareModel->setHostSystem(m_softwareLoadSystem);   // for thumbnail fallback art
	setSoftwarePaneVisible(hasSoftware);
	if (!hasSoftware)
		return;

	m_softwareSearch->clear();   // start fresh for the new system
	selectPendingSoftware();     // restore the saved software item (once)

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
	if (!m_systemStack)
		return QString();
	QAbstractItemView *view = activeMachineView();
	if (!view || !view->selectionModel())
		return QString();
	int const row = machineSourceRow(view, view->selectionModel()->currentIndex());
	if (row < 0)
		return QString();
	return m_model->index(row, 0).data(GameListModel::ShortNameRole).toString();
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

	QAbstractItemView *view = activeSoftwareView();
	if (!view || !view->selectionModel())
		return;
	int const sourceRow = softwareSourceRow(view, view->selectionModel()->currentIndex());
	if (sourceRow < 0)
		return;
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
