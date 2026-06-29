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
#include "inputmapdialog.h"
#include "audioeffectsdialog.h"
#include "pluginmenudialog.h"
#include "qtinput.h"          // Qt-native input bus (Phase 13b)
#include "qtmonitors.h"       // Qt-native monitor snapshot (Phase 13c)
#include "threadutil.h"       // low-priority background worker threads
#include "familytreemodel.h"
#include "foldertree.h"
#include "frontendpaths.h"
#include "gamelistmodel.h"
#include "gamelistproxy.h"
#include "gridview.h"
#include "infoloader.h"
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
#include <QtCore/QEvent>
#include <QtCore/QStandardPaths>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#include <QtGui/QClipboard>
#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QPalette>
#include <QtCore/QThread>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QtGui/QStyleHints>
#endif
#include <QtGui/QCursor>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QScreen>
#include <QtGui/QSurfaceFormat>
#include <QtGui/QWheelEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleFactory>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include <QtWidgets/QWidgetAction>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTableView>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QTreeView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>


namespace osd::qtui {

// The platform's default widget style, captured before any user override so the
// "System default" choice can restore it.
static QString g_defaultStyleName;

QString defaultStyleName()
{
	return g_defaultStyleName;
}

void applyPersistedStyle()
{
	if (QStyle *const current = QApplication::style())
		g_defaultStyleName = current->name();

	QString const saved = QSettings().value(QStringLiteral("appearance/style")).toString();
	if (!saved.isEmpty())
	{
		if (QStyle *const style = QStyleFactory::create(saved))
			QApplication::setStyle(style);   // takes ownership
	}
}

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
// The application palette in effect before any dark-scheme override, used to
// restore the "Light"/"System default" choice on Qt < 6.8 (which has no
// QStyleHints::setColorScheme).
static QPalette g_defaultPalette;
static bool g_defaultPaletteCaptured = false;

// A hand-tuned dark palette (Fusion-style).  Honoured by palette-aware styles
// (Fusion, qt6ct); native styles that ignore QPalette won't darken, in which
// case the user should pair this with the Fusion style.
static QPalette darkPalette()
{
	QColor const window(53, 53, 53);
	QColor const base(35, 35, 35);
	QColor const text(220, 220, 220);
	QColor const disabled(127, 127, 127);
	QColor const highlight(42, 130, 218);

	QPalette p;
	p.setColor(QPalette::Window, window);
	p.setColor(QPalette::WindowText, text);
	p.setColor(QPalette::Base, base);
	p.setColor(QPalette::AlternateBase, window);
	p.setColor(QPalette::ToolTipBase, window);
	p.setColor(QPalette::ToolTipText, text);
	p.setColor(QPalette::Text, text);
	p.setColor(QPalette::Button, window);
	p.setColor(QPalette::ButtonText, text);
	p.setColor(QPalette::BrightText, Qt::red);
	p.setColor(QPalette::Link, highlight);
	p.setColor(QPalette::Highlight, highlight);
	p.setColor(QPalette::HighlightedText, Qt::black);
	p.setColor(QPalette::PlaceholderText, disabled);
	p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
	p.setColor(QPalette::Disabled, QPalette::Text, disabled);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
	p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
	return p;
}
#endif

// Apply a colour scheme by key: "dark", "light", or "" (follow the system).
// On Qt 6.8+ this drives the platform style-hint (Fusion and the Windows 11
// style follow it); on older Qt it swaps in a dark QPalette for "dark" and
// restores the captured baseline otherwise.
static void applyColorSchemeName(const QString &scheme)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
	Qt::ColorScheme cs = Qt::ColorScheme::Unknown;   // Unknown == follow the OS
	if (scheme.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0)
		cs = Qt::ColorScheme::Dark;
	else if (scheme.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0)
		cs = Qt::ColorScheme::Light;
	QGuiApplication::styleHints()->setColorScheme(cs);
#else
	if (!g_defaultPaletteCaptured)
	{
		g_defaultPalette = QApplication::palette();
		g_defaultPaletteCaptured = true;
	}
	bool const dark = scheme.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0;

	// Some native styles (the Windows and macOS families) ignore QPalette, so the
	// dark palette below would have no visible effect under them.  When dark is
	// requested while such a style is active, pair it with Fusion (which honours
	// QPalette) and restore the user's chosen style when leaving dark.  Styles
	// that already honour the palette (Fusion, Breeze, …) are left untouched, so
	// this is a no-op on a typical Linux desktop.  (Qt 6.8+ uses setColorScheme.)
	static bool s_styleForcedForDark = false;
	auto const styleIgnoresPalette = [](const QString &n) {
		static char const *const kIgnore[] = {
			"windows", "windowsvista", "windowsxp", "windows11", "macos", "macintosh" };
		for (char const *const s : kIgnore)
			if (n.compare(QLatin1String(s), Qt::CaseInsensitive) == 0)
				return true;
		return false;
	};

	if (dark)
	{
		QStyle *const cur = QApplication::style();
		if (cur && styleIgnoresPalette(cur->name()))
		{
			if (QStyle *const fusion = QStyleFactory::create(QStringLiteral("Fusion")))
			{
				QApplication::setStyle(fusion);   // takes ownership
				s_styleForcedForDark = true;
			}
		}
		QApplication::setPalette(darkPalette());
	}
	else
	{
		if (s_styleForcedForDark)
		{
			// Restore the user's explicit style (or the recorded platform default).
			// NB: don't call applyPersistedStyle() here — it would re-capture the
			// (currently Fusion) style as the default.
			s_styleForcedForDark = false;
			QString const saved = QSettings().value(QStringLiteral("appearance/style")).toString();
			QString const target = saved.isEmpty() ? defaultStyleName() : saved;
			if (!target.isEmpty())
				if (QStyle *const s = QStyleFactory::create(target))
					QApplication::setStyle(s);
		}
		QApplication::setPalette(g_defaultPalette);   // light/system share the baseline
	}
#endif
}

void applyPersistedColorScheme()
{
	applyColorSchemeName(QSettings().value(QStringLiteral("appearance/colorScheme")).toString());
}

namespace {

// Delay between a system selection settling and enumerating its software.
// Enumeration builds the machine configuration, which is comparatively
// expensive, so we debounce rapid keyboard navigation.
constexpr int SOFTWARE_DEBOUNCE_MS = 200;

// How long a software load may run before the "Loading…" placeholder appears.
// Fast loads finish well within this, so they never flash the indicator.
constexpr int SOFTWARE_LOADING_INDICATOR_MS = 200;


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

// Per-platform Qt-native audio backends: {menu label, MAME -sound provider}.
// The first entry is the default.  (The chosen index is persisted in QSettings;
// it's consistent per platform since a user runs one OS.)  The Qt-native OSD
// links no SDL, so the SDL sound module is never offered here.
struct SoundProviderInfo { const char *label; const char *prov; };
#if defined(_WIN32)
const SoundProviderInfo kSoundProviders[] = {
	{ "WASAPI",        "wasapi" },
	{ "XAudio2",       "xaudio2" },
	{ "DirectSound",   "dsound" },
	{ "PortAudio",     "portaudio" },
	{ "None (silent)", "none" },
};
#elif defined(__APPLE__)
const SoundProviderInfo kSoundProviders[] = {
	{ "CoreAudio",     "coreaudio" },
	{ "PortAudio",     "portaudio" },
	{ "None (silent)", "none" },
};
#else
const SoundProviderInfo kSoundProviders[] = {
	{ "PulseAudio",    "pulse" },
	{ "PipeWire",      "pipewire" },
	{ "PortAudio",     "portaudio" },
	{ "None (silent)", "none" },
};
#endif
constexpr int kSoundProviderCount = int(sizeof(kSoundProviders) / sizeof(kSoundProviders[0]));

// Per-platform Qt-native gamepad backends: {menu label, MAME -joystickprovider}.
// The first entry is the default (persisted in QSettings).  Keyboard/mouse/lightgun
// come from the Qt providers; only the joystick provider is offered here.  On
// Windows winhybrid = XInput (Xbox-class pads) + DirectInput (everything else);
// pure xinput is window-independent and exists as a fallback.  On Linux the SDL
// game-controller module (gamecontroller only, no SDL video) is the joystick path.
struct JoystickProviderInfo { const char *label; const char *prov; };
#if defined(_WIN32)
const JoystickProviderInfo kJoystickProviders[] = {
	{ "Hybrid (XInput + DirectInput)", "winhybrid" },
	{ "XInput only",                   "xinput" },
	{ "DirectInput only",              "dinput" },
	{ "None",                          "none" },
};
#else
const JoystickProviderInfo kJoystickProviders[] = {
	{ "SDL Game Controller", "sdlgame" },
	{ "None",                "none" },
};
#endif
constexpr int kJoystickProviderCount = int(sizeof(kJoystickProviders) / sizeof(kJoystickProviders[0]));

} // anonymous namespace

MainWindow::MainWindow(QWidget *parent) :
	QMainWindow(parent)
{
	setWindowTitle(tr("GooeyMAME"));
	resize(1100, 680);

	createMenus();
	createWidgets();

	updateStatusCount();

	// Back osd_get/set_clipboard_text (in-game natural-keyboard Paste, etc.) with
	// Qt's QClipboard.  These run on the emulation worker thread, but QClipboard
	// is GUI-thread-only, so marshal there (directly if already on it).
	qtui_set_clipboard_hooks(
			[this] () -> std::string {
				std::string result;
				auto read = [&result] {
					if (const QClipboard *const cb = QGuiApplication::clipboard())
						result = cb->text().toStdString();
				};
				if (QThread::currentThread() == this->thread())
					read();
				else
					QMetaObject::invokeMethod(this, read, Qt::BlockingQueuedConnection);
				return result;
			},
			[this] (const std::string &text) -> bool {
				auto write = [text] {
					if (QClipboard *const cb = QGuiApplication::clipboard())
						cb->setText(QString::fromStdString(text));
				};
				if (QThread::currentThread() == this->thread())
					write();
				else
					QMetaObject::invokeMethod(this, write, Qt::BlockingQueuedConnection);
				return true;
			});

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
		// Persist incrementally so a long sweep (or a cancel/quit partway) keeps
		// what it has — the full software audit can take a very long time.
		if (audited - m_softwareAvailSavedAt >= 250)
		{
			m_softwareAvailSavedAt = audited;
			saveSoftwareCache();
		}
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
	loadScreenlessCache();
	restoreSettings();
}

// Read the ROM/hash search-path options that determine availability, joined
// into one string for cheap before/after comparison around the Options dialog.
static QString romSearchPathFingerprint()
{
	QString fp;
	for (const auto &group : qtui_read_options())
		for (const auto &opt : group.options)
			if (opt.name == "rompath" || opt.name == "hashpath")
				fp += QString::fromStdString(opt.name) + '=' +
						QString::fromStdString(opt.value) + '\n';
	return fp;
}

void MainWindow::openOptions()
{
	// Capture the ROM/hash paths before editing so we can tell whether the saved
	// changes actually affect ROM availability (and thus invalidate the caches).
	QString const pathsBefore = romSearchPathFingerprint();

	OptionsDialog dialog(this);
	if (dialog.exec() == QDialog::Accepted)
	{
		// Version/region preferences may have changed the representatives.
		m_model->reloadVersionSettings();
		m_softwareModel->reloadVersionSettings();
		// Grid artwork fallback order/folders may have changed.
		applyMachineThumbSource();
		applySoftwareThumbSource();
		// Art-view image scaling may have changed.
		m_artwork->reloadScaling();

		// If the ROM/hash search paths changed, the cached machine + software
		// availability is stale — invalidate and re-audit automatically (the same
		// path as Tools ▸ Refresh ROM Availability) instead of asking the user to.
		if (romSearchPathFingerprint() != pathsBefore
				&& m_audit && !m_audit->isRunning()
				&& m_softwareAudit && !m_softwareAudit->isRunning())
		{
			clearSoftwareCache();
			if (m_auditAct)
				m_auditAct->setEnabled(false);
			m_audit->startAudit();
			statusBar()->showMessage(
					tr("ROM paths changed — re-scanning availability…"), 6000);
		}
		else
		{
			statusBar()->showMessage(tr("Options saved."), 4000);
		}
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
	// While a game runs embedded, the window's close button normally means "stop
	// the game and return to the browser", not "quit the application" (the game
	// fills the window).  The stop is asynchronous; onEmbeddedFinished() restores
	// the UI when the run ends.
	//
	// Exception: in Qt-native *pane* mode the browser is fully visible beside the
	// game, so the window X reads as "quit the app" — stop the game first, then
	// quit once it has torn down safely.
	if (embedRunning())
	{
		if (m_nativeGlPlacedInPane)
			m_quitAfterStop = true;
		stopEmbedded();
		event->ignore();
		return;
	}

	saveSettings();
	saveSoftwareCache();
	QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
	// Phase 13 Qt-native OSD: closing the top-level OpenGL render window must
	// stop the machine, not just hide the window — and we must NOT let the
	// window (and its GL surface) be destroyed while the worker is still
	// rendering into it.  Post Exit and consume the close; onEmbeddedFinished
	// destroys the window once the worker has joined.
	if (watched == m_nativeGlWindow)
	{
		switch (event->type())
		{
		case QEvent::Close:
			if (embedRunning())
			{
				stopEmbedded();
				event->ignore();
				return true;
			}
			break;
		// Publish size changes to the worker (in device pixels) so the OpenGL
		// renderer rescales; never touched off the GUI thread.
		case QEvent::Resize:
		case QEvent::Expose:
			if (m_nativeGlTarget)
			{
				updateNativeGlSize();
				// Signal the worker (CLI lazy path) that the native surface now
				// exists, so it may bind its GL/BGFX context.
				if (m_nativeGlWindow->isExposed())
					m_nativeGlTarget->exposed.store(true, std::memory_order_release);
			}
			break;
		// Feed keyboard input to the Qt-native input module via the bus.
		case QEvent::KeyPress:
		case QEvent::KeyRelease:
		{
			auto *const ke = static_cast<QKeyEvent *>(event);
			// Committed text → char events for the natural keyboard / UI (allow
			// auto-repeat so held keys keep typing).
			if (event->type() == QEvent::KeyPress)
			{
				for (uint cp : ke->text().toUcs4())
				{
					if (!cp)
						continue;
					osd::qtui::QtInputEvent ce;
					ce.type = osd::qtui::QtInputType::Char;
					ce.codepoint = cp;
					osd::qtui::QtInputBus::instance().pushKeyboard(ce);
				}
			}
			// Raw key state (positional), no auto-repeat.
			if (!ke->isAutoRepeat())
			{
				osd::qtui::QtInputEvent e;
				e.type = (event->type() == QEvent::KeyPress)
						? osd::qtui::QtInputType::KeyPress : osd::qtui::QtInputType::KeyRelease;
				e.key = ke->key();
				e.nativeScanCode = ke->nativeScanCode();
				e.modifiers = unsigned(ke->modifiers());
				osd::qtui::QtInputBus::instance().pushKeyboard(e);
			}
			return true;   // consumed by the game
		}
		case QEvent::MouseMove:
		{
			auto *const me = static_cast<QMouseEvent *>(event);
			QPointF const p = me->position();
			osd::qtui::QtInputEvent e;
			e.type = osd::qtui::QtInputType::MouseMove;
			e.x = int(p.x());
			e.y = int(p.y());
			e.dx = e.x - m_nativeGlLastMouseX;
			e.dy = e.y - m_nativeGlLastMouseY;
			e.surfaceW = m_nativeGlWindow->width();
			e.surfaceH = m_nativeGlWindow->height();
			m_nativeGlLastMouseX = e.x;
			m_nativeGlLastMouseY = e.y;
			osd::qtui::QtInputBus::instance().pushMouse(e);
			break;
		}
		case QEvent::MouseButtonPress:
		case QEvent::MouseButtonRelease:
		{
			auto *const me = static_cast<QMouseEvent *>(event);
			int idx = -1;
			switch (me->button())
			{
			case Qt::LeftButton:   idx = 0; break;
			case Qt::RightButton:  idx = 1; break;
			case Qt::MiddleButton: idx = 2; break;
			default: break;
			}
			if (idx >= 0)
			{
				QPointF const p = me->position();
				osd::qtui::QtInputEvent e;
				e.type = osd::qtui::QtInputType::MouseButton;
				e.button = idx;
				e.value = (event->type() == QEvent::MouseButtonPress) ? 1 : 0;
				e.x = int(p.x());
				e.y = int(p.y());
				e.surfaceW = m_nativeGlWindow->width();
				e.surfaceH = m_nativeGlWindow->height();
				osd::qtui::QtInputBus::instance().pushMouse(e);
			}
			return true;
		}
		case QEvent::Wheel:
		{
			auto *const we = static_cast<QWheelEvent *>(event);
			osd::qtui::QtInputEvent e;
			e.type = osd::qtui::QtInputType::MouseWheel;
			e.value = we->angleDelta().y();
			osd::qtui::QtInputBus::instance().pushMouse(e);
			return true;
		}
		case QEvent::FocusIn:
		{
			osd::qtui::QtInputBus::instance().setFocused(true);
			osd::qtui::QtInputEvent e;
			e.type = osd::qtui::QtInputType::FocusGained;
			osd::qtui::QtInputBus::instance().pushKeyboard(e);
			break;
		}
		case QEvent::FocusOut:
		{
			osd::qtui::QtInputBus::instance().setFocused(false);
			osd::qtui::QtInputEvent e;
			e.type = osd::qtui::QtInputType::FocusLost;
			osd::qtui::QtInputBus::instance().pushKeyboard(e);
			break;
		}
		default:
			break;
		}
	}

	return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent *event)
{
	QMainWindow::changeEvent(event);
}

bool MainWindow::embedRunning() const
{
	return m_embedThread.joinable();
}

void MainWindow::stopEmbedded()
{
	// Asynchronous: posts Exit and converges on onEmbeddedFinished() when the
	// worker thread returns.
	if (m_embedSession)
		m_embedSession->post({ EmbedCommand::Exit, 0.0, 0, {} });
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

QString MainWindow::screenlessCachePath() const
{
	QString const dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir().mkpath(dir);
	return dir + QStringLiteral("/screenless.cache");
}

void MainWindow::loadScreenlessCache()
{
	QFile file(screenlessCachePath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	QTextStream in(&file);
	// First line: a stamp = system count when the cache was written.  If the
	// build's system set changed, ignore the (possibly stale) cache and re-scan.
	QString const stamp = in.readLine();
	if (stamp.toInt() != m_model->rowCount())
		return;

	std::vector<std::pair<std::string, bool>> results;
	while (!in.atEnd())
	{
		QString const line = in.readLine();
		int const sep = line.lastIndexOf(QLatin1Char(' '));
		if (sep <= 0)
			continue;
		results.emplace_back(line.left(sep).toStdString(), line.mid(sep + 1).toInt() != 0);
	}
	if (!results.empty())
		m_model->applyScreenlessBatch(results);   // marks hasScreenlessData()
}

void MainWindow::saveScreenlessCache(const std::vector<std::pair<std::string, bool>> &results) const
{
	QFile file(screenlessCachePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		return;

	QTextStream out(&file);
	out << m_model->rowCount() << QLatin1Char('\n');
	for (const auto &entry : results)
		out << QString::fromStdString(entry.first) << QLatin1Char(' ')
			<< (entry.second ? 1 : 0) << QLatin1Char('\n');
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

	// Restore the Qt-native play settings (placement / renderer / backend / audio).
	setNativePlacement(settings.value(QStringLiteral("play/nativePlacement"), int(PlaceCentral)).toInt());
	setNativeRenderer(settings.value(QStringLiteral("play/nativeRenderer"), int(RendererOpenGL)).toInt());
	setBgfxBackend(settings.value(QStringLiteral("play/bgfxBackend"), 0).toInt());
	setSoundProvider(settings.value(QStringLiteral("play/soundProvider"), 0).toInt());
	setJoystickProvider(settings.value(QStringLiteral("play/joystickProvider"), 0).toInt());

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
		m_actHideClones, m_actHideBootlegs, m_actHideHacks, m_actHidePrototypes,
		m_actHideMechanical, m_actHideScreenless };
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
	m_actHideMechanical->setChecked(settings.value(QStringLiteral("filters/hideMechanical"), false).toBool());
	m_actHideScreenless->setChecked(settings.value(QStringLiteral("filters/hideScreenless"), false).toBool());
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
	// Make sure no embedded run outlives the window: ask an in-process emulation
	// to exit and join its thread (a joinable std::thread would otherwise abort).
	if (m_embedThread.joinable())
	{
		if (m_embedSession)
			m_embedSession->post({ EmbedCommand::Exit, 0.0, 0, {} });
		m_embedThread.join();
	}

	// The screenless scan captures `this`; cancel + join it before we're gone.
	if (m_screenlessThread.joinable())
	{
		m_screenlessCancel.store(true, std::memory_order_relaxed);
		m_screenlessThread.join();
	}
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

	// Panes: collapse/show the main areas.  Each toggle hides its pane and
	// persists; the splitters redistribute the freed space.
	QMenu *panesMenu = viewMenu->addMenu(tr("&Panes"));
	QSettings paneSettings;
	struct { const char *label; QAction **act; const char *key; const char *tip; } const panes[] = {
		{ "&Categories",   &m_actShowFolders, "view/showFolders",
			"Show the folders/categories list on the left" },
		{ "&Machine List", &m_actShowSystems, "view/showSystems",
			"Show the machine list (hide it to give a software list more room)" },
		{ "&Details",      &m_actShowArtwork, "view/showArtwork",
			"Show the artwork/info panel on the right" },
	};
	for (const auto &p : panes)
	{
		QAction *act = panesMenu->addAction(tr(p.label));
		act->setCheckable(true);
		act->setChecked(paneSettings.value(QString::fromLatin1(p.key), true).toBool());
		act->setStatusTip(tr(p.tip));
		*p.act = act;
		QString const key = QString::fromLatin1(p.key);
		connect(act, &QAction::toggled, this, [this, key] (bool on) {
			QSettings().setValue(key, on);
			applyPaneVisibility();
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

	// Application style: choose the Qt widget style (e.g. Fusion).  On Linux the
	// native platform theme is usually fine; on Windows the default style looks
	// dated, so this lets the user switch (Fusion is a clean cross-platform pick).
	QMenu *styleMenu = viewMenu->addMenu(tr("&Style"));
	m_styleGroup = new QActionGroup(this);
	QString const savedStyle = QSettings().value(QStringLiteral("appearance/style")).toString();
	QString const activeStyle = QApplication::style() ? QApplication::style()->name() : QString();
	{
		QAction *act = styleMenu->addAction(tr("System default"));
		act->setCheckable(true);
		act->setChecked(savedStyle.isEmpty());
		m_styleGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this] { applyStyle(QString()); });
	}
	styleMenu->addSeparator();
	for (const QString &key : QStyleFactory::keys())
	{
		QAction *act = styleMenu->addAction(key);
		act->setCheckable(true);
		// Check the saved style, or (when none saved) the one actually in use.
		act->setChecked(savedStyle.isEmpty()
				? key.compare(activeStyle, Qt::CaseInsensitive) == 0
				: key.compare(savedStyle, Qt::CaseInsensitive) == 0);
		m_styleGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, key] { applyStyle(key); });
	}

	// Colour scheme: light / dark / follow-the-system.  On Qt 6.8+ this drives
	// the platform style-hint; on older Qt it applies a dark QPalette (best
	// paired with a palette-honouring style such as Fusion).
	QMenu *schemeMenu = viewMenu->addMenu(tr("&Color Scheme"));
	m_colorSchemeGroup = new QActionGroup(this);
	QString const savedScheme = QSettings().value(QStringLiteral("appearance/colorScheme")).toString();
	struct { const char *label; const char *key; } const schemes[] = {
		{ "System default", "" },
		{ "Light", "light" },
		{ "Dark", "dark" },
	};
	for (const auto &s : schemes)
	{
		QAction *act = schemeMenu->addAction(tr(s.label));
		act->setCheckable(true);
		QString const key = QString::fromLatin1(s.key);
		act->setChecked(savedScheme.compare(key, Qt::CaseInsensitive) == 0);
		m_colorSchemeGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, key] { applyColorScheme(key); });
	}

	// Where the Qt-native game surface is shown.
	QMenu *placeMenu = viewMenu->addMenu(tr("Qt-native &Placement"));
	m_nativePlacementGroup = new QActionGroup(this);
	struct { const char *label; int place; } const placements[] = {
		{ "Full window (replace list)", PlaceCentral },
		{ "Pane beside list", PlacePane },
	};
	for (const auto &choice : placements)
	{
		QAction *act = placeMenu->addAction(tr(choice.label));
		act->setCheckable(true);
		act->setData(choice.place);
		m_nativePlacementGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, place = choice.place] { setNativePlacement(place); });
	}

	// Renderer for the Qt-native window.
	QMenu *rendMenu = viewMenu->addMenu(tr("Qt-native &Renderer"));
	m_nativeRendererGroup = new QActionGroup(this);
	struct { const char *label; int rend; } const renderers[] = {
		{ "OpenGL", RendererOpenGL },
		{ "BGFX (shader chains)", RendererBgfx },
	};
	for (const auto &choice : renderers)
	{
		QAction *act = rendMenu->addAction(tr(choice.label));
		act->setCheckable(true);
		act->setData(choice.rend);
		m_nativeRendererGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, rend = choice.rend] { setNativeRenderer(rend); });
	}

	// BGFX backend (only relevant when the BGFX renderer is selected).  Different
	// backends render different shader chains correctly; Vulkan generally has the
	// best coverage on Linux, the GL backend the least.
	QMenu *backendMenu = rendMenu->addMenu(tr("BGFX &Backend"));
	m_bgfxBackendGroup = new QActionGroup(this);
	struct { const char *label; int backend; } const backends[] = {
		{ "Auto", 0 },
		{ "OpenGL", 1 },
		{ "Vulkan", 2 },
	};
	for (const auto &choice : backends)
	{
		QAction *act = backendMenu->addAction(tr(choice.label));
		act->setCheckable(true);
		act->setData(choice.backend);
		m_bgfxBackendGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, b = choice.backend] { setBgfxBackend(b); });
	}

	// Audio backend for the Qt-native OSD (non-SDL); the choices are the audio
	// providers available on this platform (kSoundProviders).
	QMenu *soundMenu = viewMenu->addMenu(tr("Qt-native &Audio"));
	m_soundProviderGroup = new QActionGroup(this);
	for (int i = 0; i < kSoundProviderCount; ++i)
	{
		QAction *act = soundMenu->addAction(tr(kSoundProviders[i].label));
		act->setCheckable(true);
		act->setData(i);
		m_soundProviderGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, i] { setSoundProvider(i); });
	}

	// Gamepad backend for the Qt-native OSD; the choices are the joystick
	// providers available on this platform (kJoystickProviders).
	QMenu *padMenu = viewMenu->addMenu(tr("Qt-native &Gamepad"));
	m_joystickProviderGroup = new QActionGroup(this);
	for (int i = 0; i < kJoystickProviderCount; ++i)
	{
		QAction *act = padMenu->addAction(tr(kJoystickProviders[i].label));
		act->setCheckable(true);
		act->setData(i);
		m_joystickProviderGroup->addAction(act);
		connect(act, &QAction::triggered, this, [this, i] { setJoystickProvider(i); });
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
	m_actHideMechanical = makeFilterAction(tr("Hide mechanical"), tr("Hide mechanical systems (pinball, redemption, slot machines, …)"));
	m_actHideScreenless = makeFilterAction(tr("Hide screenless"), tr("Hide systems with no screen (scans once in the background the first time)"));

	actionsExclusive(m_actWorking, m_actNotWorking);
	actionsExclusive(m_actAvailable, m_actUnavailable);
	for (QAction *act : { m_actWorking, m_actNotWorking, m_actAvailable, m_actUnavailable })
		connect(act, &QAction::toggled, this, &MainWindow::onStatusFilterChanged);
	for (QAction *act : { m_actHideClones, m_actHideBootlegs, m_actHideHacks, m_actHidePrototypes,
			m_actHideMechanical, m_actHideScreenless })
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
	filtersMenu->addSeparator();
	filtersMenu->addAction(m_actHideMechanical);
	filtersMenu->addAction(m_actHideScreenless);

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
			m_softwareAvailSavedAt = 0;
			m_softwareAudit->startAudit();
		}
	});

	buildMachineMenu();

	QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
	QAction *aboutAct = helpMenu->addAction(tr("&About"));
	connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::buildMachineMenu()
{
	// The in-game menus (Machine / Video / Input, plus Audio/Info/Cheat in later
	// steps) live on the main menu bar between Tools and Help.
	addInGameMenus(menuBar());

	// Poll the live machine's paused state to keep the Pause check in sync.
	m_embedStatusTimer = new QTimer(this);
	m_embedStatusTimer->setInterval(250);
	connect(m_embedStatusTimer, &QTimer::timeout, this, &MainWindow::updateEmbedStatus);

	setMachineControlsActive(false);
}

void MainWindow::addInGameMenus(QMenuBar *bar)
{
	// In-game controls mirroring MAME's in-game Tab menu, split into dedicated
	// top-level menus (Machine / Video / Input; Audio/Info/Cheat added later).
	// Active only while a game runs embedded in-process, where each action is
	// applied directly to the live running_machine on the emulation thread.
	// Called once for the main menu bar and again for the detached play window.
	auto track = [this] (QAction *a) {
		a->setEnabled(m_machineControlsActive);
		m_machineActions.append(a);
		return a;
	};
	// Add a top-level in-game menu, recorded so it is shown only while embedded.
	auto topMenu = [this, bar] (const QString &title) {
		QMenu *m = bar->addMenu(title);
		m_machineMenus.append(m);
		return m;
	};
	// Mark a menu/submenu action as relevant only when the machine has CapKey.
	auto relevant = [this] (QAction *a, int cap) {
		m_relevanceActions.emplace_back(a, cap);
		return a;
	};

	//---- Machine: system/hardware actions ----------------------------------
	QMenu *machine = topMenu(tr("&Machine"));

	QAction *pause = track(machine->addAction(tr("&Pause")));
	pause->setCheckable(true);
	m_pauseActions.append(pause);
	connect(pause, &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::TogglePause, 0.0, 0, {} }); });

	machine->addSeparator();
	connect(track(machine->addAction(tr("&Soft Reset"))), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::SoftReset, 0.0, 0, {} }); });
	connect(track(machine->addAction(tr("&Hard Reset"))), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::HardReset, 0.0, 0, {} }); });

	machine->addSeparator();
	connect(track(machine->addAction(tr("Save &State"))), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::SaveState, 0.0, 0, {} }); });
	connect(track(machine->addAction(tr("&Load State"))), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::LoadState, 0.0, 0, {} }); });
	connect(track(machine->addAction(tr("Save State &As…"))), &QAction::triggered, this, [this] {
		bool ok = false;
		QString const name = QInputDialog::getText(this, tr("Save State As"),
				tr("State slot/file name:"), QLineEdit::Normal, QStringLiteral("1"), &ok);
		if (ok && !name.isEmpty())
			postEmbed({ EmbedCommand::SaveState, 0.0, 0, name.toStdString() });
	});
	connect(track(machine->addAction(tr("Save Scree&nshot"))), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::SaveSnapshot, 0.0, 0, {} }); });

	// Devices: media images, configurable slots.  (BIOS / Tape / Network /
	// Barcode are added in a later step.)  Each submenu is rebuilt from the live
	// snapshot when it opens, and hidden entirely when the machine lacks it.
	machine->addSeparator();
	QMenu *media = machine->addMenu(tr("&Media"));
	track(media->menuAction());
	relevant(media->menuAction(), CapImages);
	connect(media, &QMenu::aboutToShow, this, [this, media] { rebuildMediaMenu(media); });

	QMenu *slotMenu = machine->addMenu(tr("S&lots"));
	track(slotMenu->menuAction());
	relevant(slotMenu->menuAction(), CapSlots);
	connect(slotMenu, &QMenu::aboutToShow, this, [this, slotMenu] { rebuildSlotsMenu(slotMenu); });

	QMenu *biosMenu = machine->addMenu(tr("&BIOS Selection"));
	track(biosMenu->menuAction());
	relevant(biosMenu->menuAction(), CapBios);
	connect(biosMenu, &QMenu::aboutToShow, this, [this, biosMenu] { rebuildBiosMenu(biosMenu); });

	QMenu *tapeMenu = machine->addMenu(tr("&Tape Control"));
	track(tapeMenu->menuAction());
	relevant(tapeMenu->menuAction(), CapTape);
	connect(tapeMenu, &QMenu::aboutToShow, this, [this, tapeMenu] { rebuildTapeMenu(tapeMenu); });

	QMenu *netMenu = machine->addMenu(tr("&Network Devices"));
	track(netMenu->menuAction());
	relevant(netMenu->menuAction(), CapNetwork);
	connect(netMenu, &QMenu::aboutToShow, this, [this, netMenu] { rebuildNetworkMenu(netMenu); });

	QMenu *barcodeMenu = machine->addMenu(tr("Bar&code Reader"));
	track(barcodeMenu->menuAction());
	relevant(barcodeMenu->menuAction(), CapBarcode);
	connect(barcodeMenu, &QMenu::aboutToShow, this, [this, barcodeMenu] { rebuildBarcodeMenu(barcodeMenu); });

	// DIP switches and machine configuration: live, per-machine settings.
	machine->addSeparator();
	QMenu *dipMenu = machine->addMenu(tr("&DIP Switches"));
	track(dipMenu->menuAction());
	relevant(dipMenu->menuAction(), CapDips);
	connect(dipMenu, &QMenu::aboutToShow, this, [this, dipMenu] { rebuildSettingsMenu(dipMenu, false); });

	QMenu *configMenu = machine->addMenu(tr("Machine &Configuration"));
	track(configMenu->menuAction());
	relevant(configMenu->menuAction(), CapConfigs);
	connect(configMenu, &QMenu::aboutToShow, this, [this, configMenu] { rebuildSettingsMenu(configMenu, true); });

	machine->addSeparator();
	connect(track(machine->addAction(tr("Stop &Game"))), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::Exit, 0.0, 0, {} }); });

	//---- Video: scaling, view, artwork, geometry, performance --------------
	// The whole menu is rebuilt from the live snapshot each time it opens, so
	// its children are never tracked individually (they vanish on the next
	// open); the top-level menu's visibility gates them instead.
	QMenu *video = topMenu(tr("&Video"));
	connect(video, &QMenu::aboutToShow, this, [this, video] { rebuildVideoMenu(video); });

	//---- Audio: volume sliders (machines with sound) -----------------------
	QMenu *audio = topMenu(tr("&Audio"));
	relevant(audio->menuAction(), CapSound);
	connect(audio, &QMenu::aboutToShow, this, [this, audio] { rebuildAudioMenu(audio); });

	//---- Input: keyboard mode + paste (natural-keyboard machines), crosshair --
	QMenu *input = topMenu(tr("&Input"));
	// Input remapping is universal (every machine has assignable inputs), so the
	// Input menu is always shown while a game runs — not gated on keyboard/crosshair.
	connect(input->addAction(tr("Input &Mapping…")), &QAction::triggered, this,
			[this] { showInputMapDialog(); });
	input->addSeparator();
	QMenu *keyboard = input->addMenu(tr("&Keyboard"));
	track(keyboard->menuAction());
	relevant(keyboard->menuAction(), CapNaturalKeyboard);
	QActionGroup *kbGroup = new QActionGroup(this);
	QAction *kbEmu = keyboard->addAction(tr("Emulated"));
	QAction *kbNat = keyboard->addAction(tr("Natural"));
	for (QAction *a : { kbEmu, kbNat }) { a->setCheckable(true); kbGroup->addAction(a); }
	kbEmu->setChecked(true);
	connect(kbEmu, &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::KeyboardEmulated, 0.0, 0, {} }); });
	connect(kbNat, &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::KeyboardNatural, 0.0, 0, {} }); });
	QAction *paste = relevant(track(input->addAction(tr("&Paste"))), CapNaturalKeyboard);
	connect(paste, &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::Paste, 0.0, 0, {} }); });

	QMenu *crosshair = input->addMenu(tr("&Crosshair"));
	track(crosshair->menuAction());
	relevant(crosshair->menuAction(), CapCrosshair);
	connect(crosshair, &QMenu::aboutToShow, this, [this, crosshair] { rebuildCrosshairMenu(crosshair); });

	//---- Cheat: global enable + per-entry state (only with -cheat) ----------
	QMenu *cheat = topMenu(tr("&Cheat"));
	relevant(cheat->menuAction(), CapCheat);
	connect(cheat, &QMenu::aboutToShow, this, [this, cheat] { rebuildCheatMenu(cheat); });

	//---- Info: read-only screens (system info / warnings / bookkeeping / history)
	QMenu *info = topMenu(tr("In&fo"));
	connect(info->addAction(tr("&System Information…")), &QAction::triggered, this, [this] {
		showInfoText(tr("System Information"), m_embedSession
				? QString::fromStdString(m_embedSession->infoSnapshot().sysInfo) : QString());
	});
	connect(info->addAction(tr("&Warning Information…")), &QAction::triggered, this, [this] {
		QString const w = m_embedSession
				? QString::fromStdString(m_embedSession->infoSnapshot().warnings) : QString();
		showInfoText(tr("Warning Information"),
				w.isEmpty() ? tr("This machine reports no emulation warnings.") : w);
	});
	connect(info->addAction(tr("&Bookkeeping…")), &QAction::triggered, this, [this] {
		showInfoText(tr("Bookkeeping"), m_embedSession
				? QString::fromStdString(m_embedSession->infoSnapshot().bookkeeping) : QString());
	});
	info->addSeparator();
	connect(info->addAction(tr("&History…")), &QAction::triggered, this,
			[this] { showRunningHistory(); });

	// Plugin Options: interactive Lua plugin menus (autofire, cheat finder, …),
	// shown only when a plugin registered a menu.
	QAction *plugins = relevant(info->addAction(tr("&Plugin Options…")), CapPlugins);
	connect(plugins, &QAction::triggered, this, [this] { showPluginMenuDialog(); });
}

void MainWindow::rebuildMediaMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}

	std::vector<EmbedImage> const images = m_embedSession->imagesSnapshot();
	if (images.empty())
	{
		menu->addAction(tr("(this machine has no media slots)"))->setEnabled(false);
		return;
	}

	for (const EmbedImage &img : images)
	{
		QString const label = QString::fromStdString(img.label);
		QString const file = img.filename.empty() ? tr("(empty)") : QString::fromStdString(img.filename);
		QMenu *dev = menu->addMenu(QStringLiteral("%1 — %2").arg(label, file));
		std::string const brief = img.brief;

		connect(dev->addAction(tr("Mount…")), &QAction::triggered, this, [this, brief] {
			QString const path = QFileDialog::getOpenFileName(this, tr("Mount Image"));
			if (!path.isEmpty())
			{
				postEmbed({ EmbedCommand::MountImage, 0.0, 0, brief, path.toStdString() });
				showReloadOverlay(tr("Changing media…"));
			}
		});
		QAction *unload = dev->addAction(tr("Unload"));
		unload->setEnabled(img.loaded);
		connect(unload, &QAction::triggered, this, [this, brief] {
			postEmbed({ EmbedCommand::UnloadImage, 0.0, 0, brief, {} });
			showReloadOverlay(tr("Changing media…"));
		});
	}
}

void MainWindow::rebuildSlotsMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}

	std::vector<EmbedSlot> const slotList = m_embedSession->slotsSnapshot();
	if (slotList.empty())
	{
		menu->addAction(tr("(this machine has no configurable slots)"))->setEnabled(false);
		return;
	}

	for (const EmbedSlot &slot : slotList)
	{
		QMenu *sm = menu->addMenu(QString::fromStdString(slot.name));
		QActionGroup *group = new QActionGroup(sm);
		std::string const name = slot.name;

		auto addOption = [&] (const QString &display, const std::string &value) {
			QAction *a = sm->addAction(display);
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(value == slot.current);
			connect(a, &QAction::triggered, this, [this, name, value] {
				postEmbed({ EmbedCommand::SetSlot, 0.0, 0, name, value });
				showReloadOverlay(tr("Changing slot (resetting)…"));
			});
		};

		addOption(tr("(none)"), std::string());
		for (const std::string &opt : slot.options)
		{
			QString label = QString::fromStdString(opt);
			if (opt == slot.defaultOption)
				label += tr("  (default)");
			addOption(label, opt);
		}
	}
}

void MainWindow::rebuildSettingsMenu(QMenu *menu, bool config)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}

	std::vector<EmbedSetting> const settings = m_embedSession->settingsSnapshot();
	bool any = false;
	for (const EmbedSetting &set : settings)
	{
		if (set.config != config)
			continue;
		any = true;
		QMenu *fieldMenu = menu->addMenu(QString::fromStdString(set.name));
		QActionGroup *group = new QActionGroup(fieldMenu);
		std::string const tag = set.portTag;
		quint32 const mask = set.mask;
		for (const auto &opt : set.options)
		{
			quint32 const value = opt.first;
			QAction *a = fieldMenu->addAction(QString::fromStdString(opt.second));
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(QString::fromStdString(opt.second) == QString::fromStdString(set.current));
			connect(a, &QAction::triggered, this, [this, tag, mask, value] {
				EmbedAction act;
				act.cmd = EmbedCommand::SetField;
				act.sval = tag;
				act.mask = mask;
				act.value = value;
				postEmbed(act);
			});
		}
	}
	if (!any)
		menu->addAction(config
				? tr("(this machine has no configuration settings)")
				: tr("(this machine has no DIP switches)"))->setEnabled(false);
}

void MainWindow::rebuildBiosMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}
	std::vector<EmbedBios> const list = m_embedSession->biosSnapshot();
	if (list.empty())
	{
		menu->addAction(tr("(this machine has no BIOS options)"))->setEnabled(false);
		return;
	}
	for (const EmbedBios &b : list)
	{
		// One device → list its BIOSes directly; several → a submenu each.
		QMenu *dev = (list.size() > 1) ? menu->addMenu(QString::fromStdString(b.label)) : menu;
		QActionGroup *group = new QActionGroup(dev);
		std::string const tag = b.tag;
		for (const auto &opt : b.options)
		{
			int const value = opt.first;
			QAction *a = dev->addAction(QString::fromStdString(opt.second));
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(value == b.current);
			connect(a, &QAction::triggered, this, [this, tag, value] {
				EmbedAction act;
				act.cmd = EmbedCommand::SetBios;
				act.sval = tag;
				act.ival = value;
				postEmbed(act);
				showReloadOverlay(tr("Changing BIOS (resetting)…"));
			});
		}
	}
}

void MainWindow::rebuildTapeMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}
	std::vector<EmbedTape> const tapes = m_embedSession->tapesSnapshot();
	if (tapes.empty())
	{
		menu->addAction(tr("(this machine has no cassette)"))->setEnabled(false);
		return;
	}
	auto mmss = [] (int s) { return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0')); };
	for (const EmbedTape &t : tapes)
	{
		QMenu *dev = (tapes.size() > 1) ? menu->addMenu(QString::fromStdString(t.label)) : menu;
		std::string const tag = t.tag;
		QString head = QString::fromStdString(t.status);
		if (t.loaded)
			head += QStringLiteral("  %1 / %2").arg(mmss(t.position), mmss(t.length));
		dev->addAction(head)->setEnabled(false);
		dev->addSeparator();
		struct { const char *label; int verb; } const verbs[] = {
			{ "Play", 1 }, { "Record", 2 }, { "Pause / Stop", 0 },
			{ "Rewind 30s", 3 }, { "Fast Forward 30s", 4 }, { "Rewind to Start", 5 },
		};
		for (const auto &v : verbs)
		{
			QAction *a = dev->addAction(tr(v.label));
			a->setEnabled(t.loaded);
			connect(a, &QAction::triggered, this, [this, tag, verb = v.verb] {
				EmbedAction act;
				act.cmd = EmbedCommand::TapeControl;
				act.sval = tag;
				act.ival = verb;
				postEmbed(act);
			});
		}
	}
}

void MainWindow::rebuildNetworkMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}
	std::vector<EmbedNetwork> const nets = m_embedSession->networkSnapshot();
	if (nets.empty())
	{
		menu->addAction(tr("(this machine has no network devices)"))->setEnabled(false);
		return;
	}
	for (const EmbedNetwork &n : nets)
	{
		QMenu *dev = (nets.size() > 1) ? menu->addMenu(QString::fromStdString(n.label)) : menu;
		QActionGroup *group = new QActionGroup(dev);
		std::string const tag = n.tag;
		auto addIface = [&] (const QString &label, int id) {
			QAction *a = dev->addAction(label);
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(id == n.current);
			connect(a, &QAction::triggered, this, [this, tag, id] {
				EmbedAction act;
				act.cmd = EmbedCommand::SetNetwork;
				act.sval = tag;
				act.ival = id;
				postEmbed(act);
			});
		};
		addIface(tr("(none)"), -1);
		for (const auto &iface : n.interfaces)
			addIface(QString::fromStdString(iface.second), iface.first);
	}
}

void MainWindow::rebuildBarcodeMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}
	std::vector<std::pair<std::string, std::string>> const readers = m_embedSession->barcodesSnapshot();
	if (readers.empty())
	{
		menu->addAction(tr("(this machine has no barcode reader)"))->setEnabled(false);
		return;
	}
	for (const auto &r : readers)
	{
		std::string const tag = r.first;
		QString const label = readers.size() > 1
				? tr("Enter barcode for %1…").arg(QString::fromStdString(r.second))
				: tr("Enter barcode…");
		connect(menu->addAction(label), &QAction::triggered, this, [this, tag] {
			bool ok = false;
			QString const code = QInputDialog::getText(this, tr("Barcode Reader"),
					tr("Barcode digits:"), QLineEdit::Normal, QString(), &ok);
			if (ok && !code.isEmpty())
			{
				EmbedAction act;
				act.cmd = EmbedCommand::BarcodeDecode;
				act.sval = tag;
				act.sval2 = code.toStdString();
				postEmbed(act);
			}
		});
	}
}

void MainWindow::rebuildCrosshairMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}
	std::vector<EmbedCrosshair> const list = m_embedSession->crosshairsSnapshot();
	if (list.empty())
	{
		menu->addAction(tr("(no crosshairs in use)"))->setEnabled(false);
		return;
	}
	for (const EmbedCrosshair &c : list)
	{
		QMenu *dev = (list.size() > 1) ? menu->addMenu(tr("Player %1").arg(c.player + 1)) : menu;
		QActionGroup *group = new QActionGroup(dev);
		int const player = c.player;
		struct { const char *label; int mode; } const modes[] = {
			{ "Off", 0 }, { "On", 1 }, { "Automatic", 2 },
		};
		for (const auto &m : modes)
		{
			QAction *a = dev->addAction(tr(m.label));
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(c.mode == m.mode);
			connect(a, &QAction::triggered, this, [this, player, mode = m.mode] {
				EmbedAction act;
				act.cmd = EmbedCommand::SetCrosshairMode;
				act.ival = player;
				act.value = unsigned(mode);
				postEmbed(act);
			});
		}
	}
}

void MainWindow::rebuildCheatMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}
	EmbedCheat const cheat = m_embedSession->cheatsSnapshot();

	QAction *enable = menu->addAction(tr("&Enable Cheats"));
	enable->setCheckable(true);
	enable->setChecked(cheat.enabled);
	connect(enable, &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::CheatToggleGlobal, 0.0, 0, {} }); });
	connect(menu->addAction(tr("&Reload All")), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::CheatReload, 0.0, 0, {} }); });

	menu->addSeparator();
	if (cheat.entries.empty())
	{
		menu->addAction(tr("(no cheats loaded)"))->setEnabled(false);
		return;
	}
	for (int i = 0; i < int(cheat.entries.size()); ++i)
	{
		const EmbedCheat::Entry &e = cheat.entries[i];
		QString title = QString::fromStdString(e.description);
		if (!e.state.empty())
			title += QStringLiteral(": ") + QString::fromStdString(e.state);
		if (e.textOnly)
		{
			// A comment/header line — not selectable.
			menu->addAction(title)->setEnabled(false);
			continue;
		}
		QMenu *sm = menu->addMenu(title);
		struct { const char *label; int dir; } const acts[] = {
			{ "Next state", 1 }, { "Previous state", 2 }, { "Reset to default", 0 },
		};
		for (const auto &act : acts)
			connect(sm->addAction(tr(act.label)), &QAction::triggered, this, [this, i, dir = act.dir] {
				EmbedAction a;
				a.cmd = EmbedCommand::CheatSelect;
				a.ival = i;
				a.value = unsigned(dir);
				postEmbed(a);
			});
	}
}

// Classify a slider by its (English, C-locale) description so the user-friendly
// ones land in the right menu: Image → Video, Volume → Audio, Speed → Video
// Performance, everything else → the Video "Other Adjustments" catch-all.
namespace {
enum SliderCategory { SliderImage, SliderVolume, SliderSpeed, SliderOther };
SliderCategory sliderCategory(const std::string &description)
{
	QString const d = QString::fromStdString(description).toLower();
	if (d.contains(QStringLiteral("volume")))
		return SliderVolume;
	if (d.contains(QStringLiteral("brightness")) || d.contains(QStringLiteral("contrast"))
			|| d.contains(QStringLiteral("gamma")))
		return SliderImage;
	if (d.contains(QStringLiteral("speed")))
		return SliderSpeed;
	return SliderOther;
}
} // namespace

void MainWindow::addSliderControl(QMenu *menu, const EmbedSlider &s, int index)
{
	// A submenu titled "<Name>: <value>" holding a live horizontal slider.  The
	// snapshot value seeds it; dragging posts SetSlider live; the title's
	// MAME-formatted value refreshes when the menu is next opened.
	QString const name = QString::fromStdString(s.description);
	QString const value = QString::fromStdString(s.text);
	QMenu *sm = menu->addMenu(value.isEmpty() ? name : tr("%1: %2").arg(name, value));

	QWidget *box = new QWidget(sm);
	QHBoxLayout *lay = new QHBoxLayout(box);
	lay->setContentsMargins(6, 2, 6, 2);
	QSlider *sl = new QSlider(Qt::Horizontal, box);
	sl->setMinimum(s.minval);
	sl->setMaximum(s.maxval);
	sl->setSingleStep(s.incval);
	sl->setPageStep(s.incval * 10);
	sl->setValue(s.current);
	sl->setMinimumWidth(180);
	QLabel *lbl = new QLabel(value.isEmpty() ? QString::number(s.current) : value, box);
	lbl->setMinimumWidth(52);
	lay->addWidget(sl);
	lay->addWidget(lbl);

	QWidgetAction *wa = new QWidgetAction(sm);
	wa->setDefaultWidget(box);
	sm->addAction(wa);

	connect(sl, &QSlider::valueChanged, this, [this, index, lbl, sm, name] (int v) {
		QString const text = QString::number(v);
		lbl->setText(text);
		sm->setTitle(tr("%1: %2").arg(name, text));   // live title feedback while dragging
		EmbedAction a;
		a.cmd = EmbedCommand::SetSlider;
		a.ival = index;
		a.dval = double(v);
		postEmbed(a);
	});

	sm->addSeparator();
	connect(sm->addAction(tr("Reset to default")), &QAction::triggered, this,
			[sl, def = s.defval] { sl->setValue(def); });   // valueChanged posts SetSlider
}

void MainWindow::rebuildVideoMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}

	EmbedVideo const video = m_embedSession->videoSnapshot();
	std::vector<EmbedSlider> const sliders = m_embedSession->slidersSnapshot();

	// Screen scaling: sharp (nearest) vs smooth (bilinear).  The most-wanted
	// control — the default bilinear filter looks blurry on pixel art.
	menu->addSection(tr("Pixels"));
	{
		QActionGroup *group = new QActionGroup(menu);
		struct { const char *label; int smooth; } const modes[] = {
			{ "Sharp (crisp pixels)", 0 },
			{ "Smooth (bilinear)", 1 },
		};
		for (const auto &m : modes)
		{
			QAction *a = menu->addAction(tr(m.label));
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(video.smooth == (m.smooth != 0));
			connect(a, &QAction::triggered, this, [this, smooth = m.smooth] {
				postEmbed({ EmbedCommand::SetFilter, 0.0, smooth, {} });
			});
		}
	}

	// Render views (radio): MAME's layout "views" — bezel on/off, cocktail,
	// cropped, individual screens, full artwork, …  Switching changes the whole
	// presentation.  Some games ship many, which overflow a flat list into
	// multiple columns, so present them in a scrollable "Layout" submenu (like
	// Shader Effect below).
	if (video.views.size() > 1)
	{
		QMenu *views = menu->addMenu(tr("&Layout"));
		views->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
		QActionGroup *group = new QActionGroup(views);
		for (int i = 0; i < int(video.views.size()); ++i)
		{
			QAction *a = views->addAction(QString::fromStdString(video.views[i]));
			a->setCheckable(true);
			group->addAction(a);
			a->setChecked(i == video.currentView);
			connect(a, &QAction::triggered, this, [this, i] {
				postEmbed({ EmbedCommand::SetView, 0.0, i, {} });
			});
		}
	}

	// Artwork-visibility toggles for the current view (bezel, overlay, ...).
	menu->addSection(tr("Artwork"));
	if (video.toggles.empty())
	{
		menu->addAction(tr("(this view has no artwork layers)"))->setEnabled(false);
	}
	else
	{
		for (int i = 0; i < int(video.toggles.size()); ++i)
		{
			QAction *a = menu->addAction(QString::fromStdString(video.toggles[i].name));
			a->setCheckable(true);
			a->setChecked(video.toggles[i].enabled);
			connect(a, &QAction::toggled, this, [this, i] (bool on) {
				EmbedAction act;
				act.cmd = EmbedCommand::SetVisibility;
				act.ival = i;
				act.value = on ? 1u : 0u;
				postEmbed(act);
			});
		}
	}

	// Shader effect (BGFX renderer only): a radio list of effect chains
	// (default / crt-geom / hlsl / lcd-grid / …) in its own submenu.  Absent on
	// the OpenGL renderer.
	{
		EmbedShaderChains const shaders = m_embedSession->shaderChainsSnapshot();
		if (shaders.available && !shaders.names.empty())
		{
			QMenu *effects = menu->addMenu(tr("Shader &Effect"));
			// There can be many chains; scroll the submenu rather than letting it
			// wrap into multiple columns.
			effects->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
			QActionGroup *group = new QActionGroup(effects);
			for (int i = 0; i < int(shaders.names.size()); ++i)
			{
				QAction *a = effects->addAction(QString::fromStdString(shaders.names[i]));
				a->setCheckable(true);
				group->addAction(a);
				a->setChecked(i == shaders.current);
				connect(a, &QAction::triggered, this, [this, i] {
					postEmbed({ EmbedCommand::SetShaderChain, 0.0, i, {} });
				});
			}
		}
	}

	// Geometry: rotate the view, scaling, aspect + fill the host with the game.
	menu->addSection(tr("Geometry"));
	QMenu *rotate = menu->addMenu(tr("&Rotate"));
	for (int deg : { 0, 90, 180, 270 })
		connect(rotate->addAction(tr("%1°").arg(deg)), &QAction::triggered, this,
				[this, deg] { postEmbed({ EmbedCommand::SetRotate, 0.0, deg, {} }); });

	QAction *aspect = menu->addAction(tr("Maintain &Aspect Ratio"));
	aspect->setCheckable(true);
	aspect->setChecked(video.keepaspect);
	connect(aspect, &QAction::toggled, this, [this] (bool on) {
		postEmbed({ EmbedCommand::SetKeepAspect, 0.0, on ? 1 : 0, {} });
	});

	// Scaling mode: integer pixels (crisp, no shimmer) vs fractional (fills more).
	QMenu *scaling = menu->addMenu(tr("&Scaling"));
	QActionGroup *scaleGroup = new QActionGroup(scaling);
	struct { const char *label; int mode; } const scaleModes[] = {
		{ "Fractional", 0 },          // SCALE_FRACTIONAL
		{ "Fractional (X only)", 1 }, // SCALE_FRACTIONAL_X
		{ "Fractional (Y only)", 2 }, // SCALE_FRACTIONAL_Y
		{ "Fractional (auto)", 3 },   // SCALE_FRACTIONAL_AUTO
		{ "Integer (sharp)", 4 },     // SCALE_INTEGER
	};
	for (const auto &sm : scaleModes)
	{
		QAction *a = scaling->addAction(tr(sm.label));
		a->setCheckable(true);
		scaleGroup->addAction(a);
		a->setChecked(video.scaleMode == sm.mode);
		connect(a, &QAction::triggered, this, [this, mode = sm.mode] {
			postEmbed({ EmbedCommand::SetScaleMode, 0.0, mode, {} });
		});
	}

	QAction *zoom = menu->addAction(tr("&Zoom to Screen Area"));
	zoom->setCheckable(true);
	zoom->setChecked(video.zoomToScreen);
	zoom->setEnabled(video.zoomAvailable);
	connect(zoom, &QAction::toggled, this, [this] (bool on) {
		postEmbed({ EmbedCommand::SetZoomToScreen, 0.0, on ? 1 : 0, {} });
	});

	QAction *fullscreen = menu->addAction(tr("F&ullscreen"));
	fullscreen->setCheckable(true);
	fullscreen->setChecked(m_embedFullscreen);
	connect(fullscreen, &QAction::triggered, this, [this] (bool on) { setEmbedFullscreen(on); });

	// Image: brightness / contrast / gamma live sliders.
	bool imageHeader = false;
	for (int i = 0; i < int(sliders.size()); ++i)
	{
		if (sliderCategory(sliders[i].description) != SliderImage)
			continue;
		if (!imageHeader) { menu->addSection(tr("Image")); imageHeader = true; }
		addSliderControl(menu, sliders[i], i);
	}

	// Performance: throttle rate, frameskip, FPS overlay, global speed.
	menu->addSection(tr("Performance"));
	QMenu *throttle = menu->addMenu(tr("&Throttle"));
	struct { const char *label; double rate; } const rates[] = {
		{ "50%", 0.5 }, { "100%", 1.0 }, { "200%", 2.0 }, { "500%", 5.0 }, { "1000%", 10.0 },
	};
	for (const auto &r : rates)
		connect(throttle->addAction(tr(r.label)), &QAction::triggered, this,
				[this, rate = r.rate] { postEmbed({ EmbedCommand::SetThrottleRate, rate, 0, {} }); });
	throttle->addSeparator();
	connect(throttle->addAction(tr("Toggle throttling")), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::ToggleThrottle, 0.0, 0, {} }); });

	QMenu *frameskip = menu->addMenu(tr("&Frameskip"));
	connect(frameskip->addAction(tr("Auto")), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::SetFrameskip, 0.0, -1, {} }); });
	frameskip->addSeparator();
	for (int i = 0; i <= 10; ++i)
		connect(frameskip->addAction(QString::number(i)), &QAction::triggered, this,
				[this, i] { postEmbed({ EmbedCommand::SetFrameskip, 0.0, i, {} }); });

	connect(menu->addAction(tr("Toggle &FPS display")), &QAction::triggered, this,
			[this] { postEmbed({ EmbedCommand::ToggleFps, 0.0, 0, {} }); });

	for (int i = 0; i < int(sliders.size()); ++i)
		if (sliderCategory(sliders[i].description) == SliderSpeed)
			addSliderControl(menu, sliders[i], i);

	// Other adjustments: every remaining slider (refresh, beam, mixer, overclock,
	// …) — kept reachable but out of the way of the user-friendly controls above.
	QMenu *other = nullptr;
	for (int i = 0; i < int(sliders.size()); ++i)
	{
		if (sliderCategory(sliders[i].description) != SliderOther)
			continue;
		if (!other) { menu->addSection(tr("More")); other = menu->addMenu(tr("Other Adjustments")); }
		addSliderControl(other, sliders[i], i);
	}
}

void MainWindow::rebuildAudioMenu(QMenu *menu)
{
	menu->clear();
	if (!m_embedSession)
	{
		menu->addAction(tr("(no machine running)"))->setEnabled(false);
		return;
	}

	std::vector<EmbedSlider> const sliders = m_embedSession->slidersSnapshot();
	bool any = false;
	for (int i = 0; i < int(sliders.size()); ++i)
	{
		if (sliderCategory(sliders[i].description) != SliderVolume)
			continue;
		addSliderControl(menu, sliders[i], i);
		any = true;
	}
	if (!any)
		menu->addAction(tr("(this machine has no volume controls)"))->setEnabled(false);

	// DSP effect chains (filter / compressor / EQ / reverb) — opens the editor.
	if (!m_embedSession->audioEffectsSnapshot().empty())
	{
		menu->addSeparator();
		connect(menu->addAction(tr("Audio &Effects…")), &QAction::triggered, this,
				[this] { showAudioEffectsDialog(); });
	}
}

void MainWindow::showAudioEffectsDialog()
{
	if (!m_embedSession)
		return;
	if (!m_audioEffectsDialog)
		m_audioEffectsDialog = new osd::qtui::AudioEffectsDialog(m_embedSession.get(), this);
	m_audioEffectsDialog->show();
	m_audioEffectsDialog->raise();
	m_audioEffectsDialog->activateWindow();
}

void MainWindow::showInputMapDialog()
{
	if (!m_embedSession)
		return;
	if (!m_inputMapDialog)
		m_inputMapDialog = new osd::qtui::InputMapDialog(m_embedSession.get(), this);
	m_inputMapDialog->show();
	m_inputMapDialog->raise();
	m_inputMapDialog->activateWindow();
}

void MainWindow::showPluginMenuDialog()
{
	if (!m_embedSession)
		return;
	if (!m_pluginMenuDialog)
		m_pluginMenuDialog = new osd::qtui::PluginMenuDialog(m_embedSession.get(), this);
	m_pluginMenuDialog->show();
	m_pluginMenuDialog->raise();
	m_pluginMenuDialog->activateWindow();
}

void MainWindow::showInfoText(const QString &title, const QString &text)
{
	QWidget *const owner = this;

	// One reusable modeless read-only dialog for all the Info screens.
	if (!m_infoDialog)
	{
		m_infoDialog = new QDialog(owner);
		m_infoDialog->resize(560, 460);
		QVBoxLayout *lay = new QVBoxLayout(m_infoDialog);
		m_infoTextView = new QPlainTextEdit(m_infoDialog);
		m_infoTextView->setReadOnly(true);
		m_infoTextView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
		lay->addWidget(m_infoTextView);
		QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, m_infoDialog);
		connect(buttons, &QDialogButtonBox::rejected, m_infoDialog, &QDialog::hide);
		lay->addWidget(buttons);
	}
	else if (m_infoDialog->parentWidget() != owner)
	{
		// Re-home the dialog to the window that owns the current run.
		m_infoDialog->setParent(owner, Qt::Dialog);
	}
	m_infoDialog->setWindowTitle(title);
	m_infoTextView->setPlainText(text.isEmpty() ? tr("No information available.") : text);
	m_infoDialog->show();
	m_infoDialog->raise();
	m_infoDialog->activateWindow();
}

void MainWindow::showRunningHistory()
{
	if (!m_embedSession)
		return;

	// History is keyed by the running software item (list/short) when one is
	// loaded, else by the host machine — reusing the browser's history loader.
	EmbedCaps const caps = m_embedSession->capsSnapshot();
	QString key;
	if (!caps.swShort.empty())
		key = QString::fromStdString(caps.swList) + QLatin1Char('/') + QString::fromStdString(caps.swShort);
	else
		key = m_runningSystem;

	if (frontendFolderPath(QStringLiteral("history")).isEmpty())
	{
		showInfoText(tr("History"), tr("History is not configured (set its path in Options)."));
		return;
	}
	if (key.isEmpty())
	{
		showInfoText(tr("History"), tr("No history available."));
		return;
	}

	if (!m_embedInfoLoader)
	{
		m_embedInfoLoader = new InfoLoader(this);
		connect(m_embedInfoLoader, &InfoLoader::loaded, this,
				[this] (quint64 epoch, int source, const QString &text) {
			if (epoch != m_embedInfoEpoch || source != InfoLoader::History)
				return;
			showInfoText(tr("History"), text.isEmpty() ? tr("No history available.") : text);
		});
	}
	showInfoText(tr("History"), tr("Loading…"));
	m_embedInfoLoader->request(++m_embedInfoEpoch, InfoLoader::History, key);
}

void MainWindow::setEmbedFullscreen(bool on)
{
	m_embedFullscreen = on;

	// Keep every copy of the Fullscreen action (main menu bar + detached play
	// window) in sync without re-triggering this slot.
	for (QAction *a : m_fullscreenActions)
	{
		QSignalBlocker block(a);
		a->setChecked(on);
	}

	// Qt-native game surface (central or pane): fill the main window with it by
	// hiding the surrounding panes and going fullscreen.  The menu bar stays
	// visible so the toggle remains reachable while the game holds keyboard focus.
	if (on)
	{
		m_folders->setVisible(false);
		m_systemPane->setVisible(false);
		m_softwarePane->setVisible(false);
		showFullScreen();
	}
	else
	{
		showNormal();
		applyPaneVisibility();
		setSoftwarePaneVisible(m_softwareModel->rowCount() > 0);
	}
}

void MainWindow::showReloadOverlay(const QString &message)
{
	// A media or slot change can trigger a machine reset; surface that in the
	// status bar (the Qt-native render surface has no overlay widget).
	statusBar()->showMessage(message, 1800);
}

void MainWindow::postEmbed(const EmbedAction &action)
{
	if (m_embedSession)
		m_embedSession->post(action);
}

void MainWindow::setMachineControlsActive(bool active)
{
	// The controls are greyed unless an in-process embed is running (the only
	// mode with live access to running_machine).  Toggling the actions (rather
	// than the menus) covers both the main bar and the detached window's bar.
	m_machineControlsActive = active;
	// The in-game top-level menus only appear while an in-process game runs.
	for (QMenu *m : m_machineMenus)
		m->menuAction()->setVisible(active);
	for (QAction *a : m_machineActions)
		a->setEnabled(active);
	if (active && m_embedSession)
	{
		// Hide menus the machine lacks straight away (don't wait for the timer).
		m_lastCapsGen = m_embedSession->capsGeneration();
		applyMenuRelevance(m_embedSession->capsSnapshot());
	}
	if (!active)
		for (QAction *p : m_pauseActions)
			p->setChecked(false);
	if (m_embedStatusTimer)
	{
		if (active)
			m_embedStatusTimer->start();
		else
			m_embedStatusTimer->stop();
	}
}

void MainWindow::applyMenuRelevance(const EmbedCaps &caps)
{
	// Show only the menus/submenus relevant to the running machine — a menu the
	// machine has no use for is hidden, not shown as a disabled placeholder.
	auto has = [&caps] (int cap) -> bool {
		switch (cap)
		{
		case CapDips:            return caps.hasDips;
		case CapConfigs:         return caps.hasConfigs;
		case CapBios:            return caps.hasBios;
		case CapSlots:           return caps.hasSlots;
		case CapImages:          return caps.hasImages;
		case CapTape:            return caps.hasTape;
		case CapNetwork:         return caps.hasNetwork;
		case CapBarcode:         return caps.hasBarcode;
		case CapCrosshair:       return caps.hasCrosshair;
		case CapSound:           return caps.hasSound;
		case CapNaturalKeyboard: return caps.hasNaturalKeyboard;
		case CapCheat:           return caps.cheatEnabled;
		case CapInput:           return caps.hasNaturalKeyboard || caps.hasCrosshair;
		case CapPlugins:         return caps.hasPlugins;
		}
		return true;
	};
	for (const auto &pr : m_relevanceActions)
		pr.first->setVisible(has(pr.second));
}

void MainWindow::updateEmbedStatus()
{
	if (m_embedSession && m_embedSession->running.load())
	{
		bool const paused = m_embedSession->paused.load();
		for (QAction *p : m_pauseActions)
			p->setChecked(paused);

		// Capabilities can change mid-run (a cart load adds image/slot devices),
		// so re-apply relevance whenever the published generation advances.
		unsigned const gen = m_embedSession->capsGeneration();
		if (gen != m_lastCapsGen)
		{
			m_lastCapsGen = gen;
			applyMenuRelevance(m_embedSession->capsSnapshot());
		}

		// Apply a CLI --shader request once the BGFX effect chains are published.
		if (!m_pendingShaderChain.isEmpty())
		{
			EmbedShaderChains const sh = m_embedSession->shaderChainsSnapshot();
			if (sh.available && !sh.names.empty())
			{
				for (int i = 0; i < int(sh.names.size()); ++i)
				{
					if (m_pendingShaderChain.compare(QString::fromStdString(sh.names[i]), Qt::CaseInsensitive) == 0)
					{
						postEmbed({ EmbedCommand::SetShaderChain, 0.0, i, {} });
						break;
					}
				}
				m_pendingShaderChain.clear();   // attempted once chains exist (found or not)
			}
		}
	}
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
	barFiltersMenu->addSeparator();
	barFiltersMenu->addAction(m_actHideMechanical);
	barFiltersMenu->addAction(m_actHideScreenless);
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
	m_softwareView->sortByColumn(SoftwareModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
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
	m_softwareTree->sortByColumn(SoftwareModel::COLUMN_DESCRIPTION, Qt::AscendingOrder);
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

	// Placeholder shown in place of the list while a slow software load is in
	// flight, so a delayed enumeration doesn't look like "this machine has no
	// software".  Hidden by default; revealed by m_softwareLoadingTimer.
	m_softwareLoadingLabel = new QLabel(tr("Loading software list…"));
	m_softwareLoadingLabel->setAlignment(Qt::AlignCenter);
	m_softwareLoadingLabel->setEnabled(false);   // muted, secondary-text look
	m_softwareLoadingLabel->setVisible(false);
	softwareLayout->addWidget(m_softwareLoadingLabel, 1);

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
	m_splitter = new QSplitter(Qt::Horizontal);
	applyMainLayout(m_mainLayout);

	// The browser splitter and the Qt-native game surface share a stack as the
	// central widget; the game's window container is added as a second page (and
	// shown) only while a game runs embedded in full-window placement.
	m_centralStack = new QStackedWidget(this);
	m_centralStack->addWidget(m_splitter);    // page 0: browser
	setCentralWidget(m_centralStack);

	// Debounce timer for software enumeration.
	m_softwareTimer = new QTimer(this);
	m_softwareTimer->setSingleShot(true);
	m_softwareTimer->setInterval(SOFTWARE_DEBOUNCE_MS);
	connect(m_softwareTimer, &QTimer::timeout, this, &MainWindow::refreshSoftware);

	// Reveal the "Loading…" placeholder only if a load is taking a while (e.g.
	// parked behind a running audit), so the common fast case never flickers.
	m_softwareLoadingTimer = new QTimer(this);
	m_softwareLoadingTimer->setSingleShot(true);
	m_softwareLoadingTimer->setInterval(SOFTWARE_LOADING_INDICATOR_MS);
	connect(m_softwareLoadingTimer, &QTimer::timeout, this, &MainWindow::showSoftwareLoadingIndicator);
}

void MainWindow::applyIconSize(int size)
{
	m_view->setIconSize(QSize(size, size));
	m_view->verticalHeader()->setDefaultSectionSize(size + 6);

	// The grouped (tree) view shows the same machine list, so it honours the same
	// icon/row size.  With uniform row heights the row height is derived from the
	// delegate's decoration size, so setIconSize() drives both the icon and the
	// row height — but the uniform height is cached, so toggle it to force a
	// relayout at the new size.
	if (m_tree)
	{
		m_tree->setIconSize(QSize(size, size));
		m_tree->setUniformRowHeights(false);
		m_tree->setUniformRowHeights(true);
	}

	// The software pane honours the same size: its rows now carry icons too
	// (per-software icon if available, else the host machine's).
	if (m_softwareView)
	{
		m_softwareView->setIconSize(QSize(size, size));
		m_softwareView->verticalHeader()->setDefaultSectionSize(size + 6);
	}
	if (m_softwareTree)
	{
		m_softwareTree->setIconSize(QSize(size, size));
		m_softwareTree->setUniformRowHeights(false);
		m_softwareTree->setUniformRowHeights(true);
	}
}

void MainWindow::applyStyle(const QString &name)
{
	// "" = restore the platform default captured at startup and drop the override.
	QString const target = name.isEmpty() ? defaultStyleName() : name;
	if (!target.isEmpty())
	{
		if (QStyle *const style = QStyleFactory::create(target))
			QApplication::setStyle(style);   // restyles all live widgets; takes ownership
	}
	if (name.isEmpty())
		QSettings().remove(QStringLiteral("appearance/style"));
	else
		QSettings().setValue(QStringLiteral("appearance/style"), name);

#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
	// Setting a style resets the palette to the new style's standard one, so
	// re-capture the light baseline and re-assert the dark override if active.
	// (On 6.8+ the style-hint persists across style changes automatically.)
	QString const scheme = QSettings().value(QStringLiteral("appearance/colorScheme")).toString();
	if (scheme.compare(QLatin1String("dark"), Qt::CaseInsensitive) != 0)
		g_defaultPaletteCaptured = false;   // force re-capture against the new style
	applyColorSchemeName(scheme);
#endif
}

void MainWindow::applyColorScheme(const QString &scheme)
{
	applyColorSchemeName(scheme);
	if (scheme.isEmpty())
		QSettings().remove(QStringLiteral("appearance/colorScheme"));
	else
		QSettings().setValue(QStringLiteral("appearance/colorScheme"), scheme);
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

QStringList MainWindow::gridFallbackLabels(bool *family) const
{
	QSettings settings;
	if (family)
		*family = settings.value(QStringLiteral("grid/artFallbackFamily"), true).toBool();
	QStringList order = settings.value(QStringLiteral("grid/artFallbackOrder")).toStringList();
	if (order.isEmpty())   // default: every art type, in table order
		for (std::size_t i = 0; i < THUMBNAIL_SOURCE_COUNT; ++i)
			order << QString::fromLatin1(THUMBNAIL_SOURCES[i].label);
	return order;
}

void MainWindow::applyMachineThumbSource()
{
	int const i = m_gridSource->currentIndex();
	if (i < 0 || i >= int(THUMBNAIL_SOURCE_COUNT))
		return;

	bool family = true;
	QStringList const order = gridFallbackLabels(&family);

	// Primary (selected) source first, then the enabled fallback types in order.
	QStringList keys;
	keys << QString::fromLatin1(THUMBNAIL_SOURCES[i].machineKey);
	for (const QString &label : order)
		for (std::size_t j = 0; j < THUMBNAIL_SOURCE_COUNT; ++j)
			if (int(j) != i && label == QLatin1String(THUMBNAIL_SOURCES[j].label))
				keys << QString::fromLatin1(THUMBNAIL_SOURCES[j].machineKey);

	m_model->setThumbnailSources(keys, family);
}

void MainWindow::applySoftwareThumbSource()
{
	int const i = m_softwareGridSource->currentIndex();
	if (i < 0 || i >= int(THUMBNAIL_SOURCE_COUNT))
		return;

	bool family = true;
	QStringList const order = gridFallbackLabels(&family);

	QVector<QPair<QString, QString>> keys;
	keys.append({ QString::fromLatin1(THUMBNAIL_SOURCES[i].softwareKey),
			QString::fromLatin1(THUMBNAIL_SOURCES[i].machineKey) });
	for (const QString &label : order)
		for (std::size_t j = 0; j < THUMBNAIL_SOURCE_COUNT; ++j)
			if (int(j) != i && label == QLatin1String(THUMBNAIL_SOURCES[j].label))
				keys.append({ QString::fromLatin1(THUMBNAIL_SOURCES[j].softwareKey),
						QString::fromLatin1(THUMBNAIL_SOURCES[j].machineKey) });

	m_softwareModel->setThumbnailSources(keys, family);
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

	// Then honour the user's collapse/show toggles for the main panes.
	applyPaneVisibility();
}

void MainWindow::applyPaneVisibility()
{
	// Guard: createMenus() builds the toggles before the panes exist on the very
	// first applyMainLayout(); they're set up together, but be defensive.
	if (m_actShowFolders && m_folders)
		m_folders->setVisible(m_actShowFolders->isChecked());
	if (m_actShowSystems && m_systemPane)
		m_systemPane->setVisible(m_actShowSystems->isChecked());
	if (m_actShowArtwork && m_artwork)
		m_artwork->setVisible(m_actShowArtwork->isChecked());
}

void MainWindow::setSoftwarePaneVisible(bool visible)
{
	// The software pane lives in the right (vertical) splitter beneath the
	// artwork; showing/hiding it lets the splitter redistribute the space.
	m_softwarePane->setVisible(visible);
}

void MainWindow::syncSoftwarePane()
{
	// A re-filter can hide the selected system without the active view emitting
	// a selection change (stale current index after invalidateFilter), leaving
	// the software pane stuck on a system no longer in the list.  Re-evaluate:
	// if the shown system isn't the current (visible) selection, refresh, which
	// hides the pane when nothing valid is selected.
	if (selectedSystem() != m_softwareLoadSystem)
		onSystemSelectionChanged();
}

void MainWindow::onFolderSelected(const FolderFilter &filter)
{
	m_proxy->setFolderFilter(filter);
	invalidateMachineViews();
	syncSoftwarePane();
	updateStatusCount();
}

void MainWindow::onSearchTextChanged(const QString &text)
{
	m_proxy->setSearchText(text);
	invalidateMachineViews();
	syncSoftwarePane();
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
	syncSoftwarePane();

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
	m_proxy->setHideMechanical(m_actHideMechanical->isChecked());
	m_proxy->setHideScreenless(m_actHideScreenless->isChecked());
	invalidateMachineViews();
	syncSoftwarePane();

	QSettings settings;
	settings.setValue(QStringLiteral("filters/hideClones"), m_actHideClones->isChecked());
	settings.setValue(QStringLiteral("filters/hideBootlegs"), m_actHideBootlegs->isChecked());
	settings.setValue(QStringLiteral("filters/hideHacks"), m_actHideHacks->isChecked());
	settings.setValue(QStringLiteral("filters/hidePrototypes"), m_actHidePrototypes->isChecked());
	settings.setValue(QStringLiteral("filters/hideMechanical"), m_actHideMechanical->isChecked());
	settings.setValue(QStringLiteral("filters/hideScreenless"), m_actHideScreenless->isChecked());

	// Screenless verdicts need a one-time background scan (build a machine_config
	// per driver); kick it off the first time the filter is actually enabled.
	if (m_actHideScreenless->isChecked() && !m_model->hasScreenlessData())
		startScreenlessScan();

	updateStatusCount();
}

void MainWindow::startScreenlessScan()
{
	if (m_screenlessScanning || m_screenlessThread.joinable() || m_model->hasScreenlessData())
		return;
	m_screenlessScanning = true;
	statusBar()->showMessage(
			tr("Scanning for screenless systems… (one-time, runs in the background)"));

	m_screenlessCancel.store(false, std::memory_order_relaxed);
	m_screenlessThread = std::thread([this] {
		osd::qtui::lower_current_thread_priority();
		auto results = std::make_shared<std::vector<std::pair<std::string, bool>>>();
		qtui_scan_screenless(
				[&results](const std::string &name, bool screenless) {
					results->emplace_back(name, screenless);
				},
				m_screenlessCancel);
		// Hand the results back to the GUI thread to apply to the model.
		QMetaObject::invokeMethod(this, [this, results] {
			applyScreenlessResults(*results);
		}, Qt::QueuedConnection);
	});
}

void MainWindow::applyScreenlessResults(const std::vector<std::pair<std::string, bool>> &results)
{
	if (m_screenlessThread.joinable())
		m_screenlessThread.join();
	m_screenlessScanning = false;

	if (m_screenlessCancel.load(std::memory_order_relaxed))
		return;   // cancelled (app shutting down)

	m_model->applyScreenlessBatch(results);
	saveScreenlessCache(results);   // persist so the scan never repeats on this build
	// Re-filter now that the verdicts exist, in case the filter is on.
	if (m_actHideScreenless && m_actHideScreenless->isChecked())
		invalidateMachineViews();
	updateStatusCount();
	statusBar()->showMessage(tr("Screenless scan complete."), 4000);
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
		QString const parent = m_softwareModel->parentForRow(sourceRow);
		if (!list.isEmpty() && !software.isEmpty())
		{
			m_artwork->setSoftware(list, software, parent);
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
		m_softwareLoadingTimer->stop();
		m_softwareLoading = false;
		m_softwareLoadingLabel->setVisible(false);
		m_softwareStack->setVisible(true);
		m_softwareModel->setEntries({});
		setSoftwarePaneVisible(false);
		m_softwareLoadSystem.clear();
		return;
	}

	// Enumerate + audit on a worker thread; results arrive in onSoftwareLoaded.
	// Arm the loading indicator: it reveals itself only if this load is slow.
	m_softwareLoadSystem = system;
	m_softwareLoading = true;
	m_softwareLoadingTimer->start();
	m_softwareLoader->load(system);
}

void MainWindow::showSoftwareLoadingIndicator()
{
	if (!m_softwareLoading)
		return;

	// Reveal the software pane with the placeholder in place of the list, so a
	// slow load (e.g. parked behind a running audit) reads as "loading", not
	// "no software list".
	m_softwareGridBar->setVisible(false);
	m_softwareStack->setVisible(false);
	m_softwareLoadingLabel->setVisible(true);
	setSoftwarePaneVisible(true);
}

void MainWindow::onSoftwareLoaded(const std::vector<qtui_software_entry> &entries)
{
	// The load resolved: dismiss the loading placeholder and restore the views.
	m_softwareLoadingTimer->stop();
	m_softwareLoading = false;
	m_softwareLoadingLabel->setVisible(false);
	m_softwareStack->setVisible(true);
	bool const gridMode = (m_softwareStack->currentIndex() == ViewGrid
			|| m_softwareStack->currentIndex() == ViewGridGrouped);
	m_softwareGridBar->setVisible(gridMode);

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

void MainWindow::launchSelectedSystem()
{
	QString const system = selectedSystem();
	if (system.isEmpty())
		return;
	launchSystem(system, QString());
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

	launchSystem(system, software);
}


void MainWindow::setNativePlacement(int placement)
{
	if (placement != PlacePane)
		placement = PlaceCentral;
	m_nativePlacement = placement;
	QSettings().setValue(QStringLiteral("play/nativePlacement"), placement);
	if (m_nativePlacementGroup)
		for (QAction *act : m_nativePlacementGroup->actions())
			if (act->data().toInt() == placement)
				act->setChecked(true);
}

void MainWindow::setNativeRenderer(int renderer)
{
	if (renderer != RendererBgfx)
		renderer = RendererOpenGL;
	m_nativeRenderer = renderer;
	QSettings().setValue(QStringLiteral("play/nativeRenderer"), renderer);
	if (m_nativeRendererGroup)
		for (QAction *act : m_nativeRendererGroup->actions())
			if (act->data().toInt() == renderer)
				act->setChecked(true);
}

// 0=auto, 1=opengl, 2=vulkan
static QString bgfxBackendName(int backend)
{
	switch (backend)
	{
	case 1:  return QStringLiteral("opengl");
	case 2:  return QStringLiteral("vulkan");
	default: return QStringLiteral("auto");
	}
}

void MainWindow::setBgfxBackend(int backend)
{
	if (backend < 0 || backend > 2)
		backend = 0;
	m_bgfxBackend = backend;
	QSettings().setValue(QStringLiteral("play/bgfxBackend"), backend);
	if (m_bgfxBackendGroup)
		for (QAction *act : m_bgfxBackendGroup->actions())
			if (act->data().toInt() == backend)
				act->setChecked(true);
}

// Index into the platform's kSoundProviders table → the MAME -sound value.
static QString soundProviderName(int provider)
{
	if (provider < 0 || provider >= kSoundProviderCount)
		provider = 0;
	return QString::fromLatin1(kSoundProviders[provider].prov);
}

void MainWindow::setSoundProvider(int provider)
{
	if (provider < 0 || provider >= kSoundProviderCount)
		provider = 0;
	m_soundProvider = provider;
	QSettings().setValue(QStringLiteral("play/soundProvider"), provider);
	if (m_soundProviderGroup)
		for (QAction *act : m_soundProviderGroup->actions())
			if (act->data().toInt() == provider)
				act->setChecked(true);
}

// Index into the platform's kJoystickProviders table → the MAME -joystickprovider value.
static QString joystickProviderName(int provider)
{
	if (provider < 0 || provider >= kJoystickProviderCount)
		provider = 0;
	return QString::fromLatin1(kJoystickProviders[provider].prov);
}

void MainWindow::setJoystickProvider(int provider)
{
	if (provider < 0 || provider >= kJoystickProviderCount)
		provider = 0;
	m_joystickProvider = provider;
	QSettings().setValue(QStringLiteral("play/joystickProvider"), provider);
	if (m_joystickProviderGroup)
		for (QAction *act : m_joystickProviderGroup->actions())
			if (act->data().toInt() == provider)
				act->setChecked(true);
}

void MainWindow::returnFromEmbed()
{
	// Restore the browser after a Qt-native run: leave fullscreen first (restores
	// window + panes), take the game container out of the artwork pane, and show
	// the browser splitter again.
	if (m_embedFullscreen)
		setEmbedFullscreen(false);
	if (isHidden())
		show();
	m_artwork->detachGame();
	applyPaneVisibility();
	m_centralStack->setCurrentWidget(m_splitter);
}

void MainWindow::startStandaloneEmbedded(const QString &system, const QString &software,
		const QString &renderer, const QString &bgfxBackend, const QString &shader)
{
	m_standaloneEmbed = true;
	// Qt-native OSD: the game renders in this window's central area with the
	// in-game menu bar; the browser list isn't shown.  No SDL, no X11 attach —
	// works on any platform.  Set members directly so we don't persist over the
	// user's normal preferences.  Closing the window quits the app
	// (onEmbeddedFinished honours m_standaloneEmbed).
	m_nativePlacement = PlaceCentral;

	// CLI overrides (don't persist): renderer, BGFX backend, and a shader chain
	// to apply once the game's effects publish.
	if (renderer == QLatin1String("bgfx"))
		m_nativeRenderer = RendererBgfx;
	else if (renderer == QLatin1String("opengl"))
		m_nativeRenderer = RendererOpenGL;
	if (!bgfxBackend.isEmpty())
	{
		m_nativeRenderer = RendererBgfx;   // a backend choice implies BGFX
		if (bgfxBackend == QLatin1String("opengl"))      m_bgfxBackend = 1;
		else if (bgfxBackend == QLatin1String("vulkan")) m_bgfxBackend = 2;
		else                                             m_bgfxBackend = 0;   // auto
	}
	if (!shader.isEmpty())
	{
		m_nativeRenderer = RendererBgfx;   // shader chains require BGFX
		m_pendingShaderChain = shader;
	}

	show();
	// Launch once the event loop is running so the render surface is realised.
	QTimer::singleShot(0, this, [this, system, software] { launchSystem(system, software); });
}

void MainWindow::runCliPassthrough(const std::vector<std::string> &args)
{
	// The browser is never shown; exiting the run quits the app (honoured by
	// onEmbeddedFinished via m_standaloneEmbed).
	m_standaloneEmbed = true;

	// First bare (non-option) token after the program name is the system, used
	// for the window title when the run opens a window.
	for (std::size_t i = 1; i < args.size(); ++i)
	{
		if (!args[i].empty() && args[i][0] != '-')
		{
			m_runningSystem = QString::fromStdString(args[i]);
			break;
		}
	}

	m_embedSession = std::make_unique<EmbedSession>();
	m_nativeGlTarget = std::make_unique<osd::qtui::QtEmbedTarget>();
	osd::qtui::QtEmbedTarget *const target = m_nativeGlTarget.get();

	// Lazy surface factory: the OSD calls this from the worker thread inside
	// video_init() only if the run needs video.  Hop to the GUI thread (blocking)
	// to create + show the window, so headless commands open nothing.
	target->create_window = [this, target]() -> bool {
		bool ok = false;
		QMetaObject::invokeMethod(this, [this, target, &ok] { ok = createCliRenderWindow(target); },
				Qt::BlockingQueuedConnection);
		return ok;
	};

	std::string const sound = soundProviderName(m_soundProvider).toStdString();
	std::string const joystick = joystickProviderName(m_joystickProvider).toStdString();
	std::vector<std::string> argv = args;
	EmbedSession *const session = m_embedSession.get();
	m_embedThread = std::thread([this, argv, target, session, sound, joystick]() mutable {
		int const code = qtui_run_args_native(argv, target, *session, sound, joystick);
		QMetaObject::invokeMethod(this, "onEmbeddedFinished", Qt::QueuedConnection, Q_ARG(int, code));
	});
}

bool MainWindow::createCliRenderWindow(osd::qtui::QtEmbedTarget *target)
{
	if (!target)
		return false;

	// Capture desktop monitor geometry on the GUI thread for the Qt-native
	// monitor module (which initialises on the worker thread and can't touch
	// QScreen there).
	{
		std::vector<osd::qtui::QtMonitorRect> mons;
		const QScreen *const primary = QGuiApplication::primaryScreen();
		for (QScreen *const sc : QGuiApplication::screens())
		{
			QRect const g = sc->geometry();
			mons.push_back({ g.x(), g.y(), g.width(), g.height(), sc == primary });
		}
		osd::qtui::qtui_set_monitors(std::move(mons));
	}

	QSurfaceFormat fmt;
	fmt.setRenderableType(QSurfaceFormat::OpenGL);
	fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
	fmt.setVersion(2, 1);
	fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	fmt.setSwapInterval(1);

	// A bare top-level QWindow (no container): the browser isn't shown, so the
	// game renders in its own window.  onEmbeddedFinished's top-level-fallback
	// branch tears it down (m_nativeGlContainer stays null).
	m_nativeGlWindow = new QWindow();
	m_nativeGlWindow->setSurfaceType(QSurface::OpenGLSurface);
	m_nativeGlWindow->setFormat(fmt);
	m_nativeGlWindow->setTitle(m_runningSystem.isEmpty()
			? QStringLiteral("GooeyMAME")
			: QStringLiteral("GooeyMAME — %1").arg(m_runningSystem));
	m_nativeGlWindow->resize(640, 480);
	m_nativeGlWindow->installEventFilter(this);   // input / resize / close
	m_nativeGlWindow->setCursor(Qt::BlankCursor);

	m_nativeGlTarget->window = m_nativeGlWindow;

	m_nativeGlWindow->show();
	m_nativeGlWindow->requestActivate();

	// Prime the input bus (assume focus; the window is being shown + activated).
	osd::qtui::QtInputBus::instance().clear();
	osd::qtui::QtInputBus::instance().setFocused(true);

	updateNativeGlSize();
	// Return immediately: this runs in a blocking-queued call from the worker, so
	// we must NOT spin the event loop here (the surface exposes only once the main
	// exec() loop resumes).  The worker waits on target->exposed, which the
	// eventFilter sets when the QWindow's Expose event arrives.
	return true;
}

void MainWindow::launchSystem(const QString &system, const QString &software)
{
	if (embedRunning())
	{
		// Quit the running game and start this one once it has stopped.
		m_pendingLaunchSystem = system;
		m_pendingLaunchSoftware = software;
		m_hasPendingLaunch = true;
		statusBar()->showMessage(tr("Stopping current game…"));
		stopEmbedded();
		return;
	}

	// Pause any artwork-panel media (snap/advert video, soundtrack) so it doesn't
	// keep playing — and competing for audio — once the game starts.
	m_artwork->pauseMedia();

	m_runningSystem = system;   // for the Info ▸ History lookup during the run

	QString const label = software.isEmpty()
			? system
			: QStringLiteral("%1 %2").arg(system, software);

	// All play now goes through the Qt-native OSD (renders into a QWindow, no SDL).
	launchEmbeddedNativeGl(label, system, software);
}

void MainWindow::updateNativeGlSize()
{
	if (!m_nativeGlWindow || !m_nativeGlTarget)
		return;
	// device pixels: the GL framebuffer is sized in physical pixels
	qreal const dpr = m_nativeGlWindow->devicePixelRatio();
	int const w = int(m_nativeGlWindow->width() * dpr);
	int const h = int(m_nativeGlWindow->height() * dpr);
	m_nativeGlTarget->width.store((w > 0) ? w : 640, std::memory_order_relaxed);
	m_nativeGlTarget->height.store((h > 0) ? h : 480, std::memory_order_relaxed);
}

void MainWindow::launchEmbeddedNativeGl(const QString &label, const QString &system, const QString &software)
{
	if (m_embedSession || m_embedThread.joinable())
		return;   // a run is already in progress

	// Renderer for the Qt-native window: OpenGL (default) or BGFX, from the
	// View ▸ Qt-native Renderer setting (GOOEY_QT_BGFX=1 forces BGFX too).
	bool const useBgfx = (m_nativeRenderer == RendererBgfx)
			|| qEnvironmentVariableIsSet("GOOEY_QT_BGFX");
	std::string const bgfxBackend = bgfxBackendName(m_bgfxBackend).toStdString();
	std::string const soundProvider = soundProviderName(m_soundProvider).toStdString();
	std::string const joystickProvider = joystickProviderName(m_joystickProvider).toStdString();

	// Capture desktop monitor geometry on the GUI thread for the Qt-native
	// monitor module (which initialises on the worker thread and can't touch
	// QScreen there).
	{
		std::vector<osd::qtui::QtMonitorRect> mons;
		const QScreen *const primary = QGuiApplication::primaryScreen();
		for (QScreen *const sc : QGuiApplication::screens())
		{
			QRect const g = sc->geometry();
			mons.push_back({ g.x(), g.y(), g.width(), g.height(), sc == primary });
		}
		osd::qtui::qtui_set_monitors(std::move(mons));
	}

	statusBar()->showMessage(tr("Running %1 (Qt-native OSD, %2)…")
			.arg(label, useBgfx ? QStringLiteral("BGFX") : QStringLiteral("OpenGL")));
	m_playAct->setEnabled(false);

	// Create the render surface on the GUI thread.  A GL-capable QWindow works
	// for both the OpenGL renderer (QOpenGLContext) and BGFX's OpenGL backend
	// (which renders to the window's native handle).
	QSurfaceFormat fmt;
	fmt.setRenderableType(QSurfaceFormat::OpenGL);
	fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
	fmt.setVersion(2, 1);
	fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
	fmt.setSwapInterval(1);

	m_nativeGlWindow = new QWindow();
	m_nativeGlWindow->setSurfaceType(QSurface::OpenGLSurface);
	m_nativeGlWindow->setFormat(fmt);
	m_nativeGlWindow->installEventFilter(this);   // resize/close

	m_nativeGlContainer = QWidget::createWindowContainer(m_nativeGlWindow, this);
	m_nativeGlContainer->setMinimumSize(320, 240);
	m_nativeGlContainer->setFocusPolicy(Qt::StrongFocus);
	// Hide the OS pointer over the game surface (matches SDL): the in-game
	// crosshair is the visible aiming reference, so the bare pointer racing
	// ahead of the pipeline-delayed reticule no longer reads as lag.
	m_nativeGlContainer->setCursor(Qt::BlankCursor);
	m_nativeGlWindow->setCursor(Qt::BlankCursor);

	// Place the surface: full central area (replacing the browser list) or in a
	// pane beside the list (hosted in the artwork pane, list stays visible).
	m_nativeGlPlacedInPane = (m_nativePlacement == PlacePane);
	if (m_nativeGlPlacedInPane)
	{
		m_artwork->setVisible(true);
		m_artwork->attachGame(m_nativeGlContainer);
	}
	else
	{
		if (m_centralStack->indexOf(m_nativeGlContainer) < 0)
			m_centralStack->addWidget(m_nativeGlContainer);
		m_centralStack->setCurrentWidget(m_nativeGlContainer);
	}

	m_nativeGlTarget = std::make_unique<osd::qtui::QtEmbedTarget>();
	m_nativeGlTarget->window = m_nativeGlWindow;
	updateNativeGlSize();

	// Prime the Qt-native input bus: drop stale events and assume focus (the
	// surface is about to be shown and given keyboard focus).
	osd::qtui::QtInputBus::instance().clear();
	osd::qtui::QtInputBus::instance().setFocused(true);
	m_nativeGlContainer->setFocus(Qt::OtherFocusReason);
	m_nativeGlWindow->requestActivate();

	m_embedSession = std::make_unique<EmbedSession>();
	setMachineControlsActive(true);

	std::string const sys = system.toStdString();
	std::string const sw = software.toStdString();

	// The QWindow must be exposed (native surface created) before the worker's
	// GL context calls makeCurrent() against it.  Poll isExposed() before
	// spawning the emulation thread.
	auto *const attempts = new int(0);
	auto spawnPtr = std::make_shared<std::function<void()>>();
	*spawnPtr = [this, sys, sw, attempts, spawnPtr, useBgfx, bgfxBackend, soundProvider, joystickProvider]() {
		if (!m_nativeGlWindow || !m_nativeGlTarget)
		{
			delete attempts;
			return;   // torn down before we got going
		}
		if (!m_nativeGlWindow->isExposed() && (*attempts)++ < 100)
		{
			QTimer::singleShot(10, this, *spawnPtr);
			return;
		}
		delete attempts;

		// capture the now-laid-out, exposed surface's real size before launch
		updateNativeGlSize();

		EmbedSession *const session = m_embedSession.get();
		osd::qtui::QtEmbedTarget *const target = m_nativeGlTarget.get();
		if (!session || !target)
			return;
		m_embedThread = std::thread([this, sys, sw, target, session, useBgfx, bgfxBackend, soundProvider, joystickProvider] {
			int const code = qtui_run_embedded_native(sys, sw, target, *session, useBgfx, bgfxBackend, soundProvider, joystickProvider);
			QMetaObject::invokeMethod(this, "onEmbeddedFinished", Qt::QueuedConnection, Q_ARG(int, code));
		});
	};
	QTimer::singleShot(10, this, *spawnPtr);
}

void MainWindow::onEmbeddedFinished(int exitCode)
{
	// Close the input-mapping dialog before the session it references is freed.
	if (m_inputMapDialog)
	{
		m_inputMapDialog->close();
		m_inputMapDialog->deleteLater();
		m_inputMapDialog = nullptr;
	}
	if (m_audioEffectsDialog)
	{
		m_audioEffectsDialog->close();
		m_audioEffectsDialog->deleteLater();
		m_audioEffectsDialog = nullptr;
	}
	if (m_pluginMenuDialog)
	{
		m_pluginMenuDialog->close();
		m_pluginMenuDialog->deleteLater();
		m_pluginMenuDialog = nullptr;
	}

	// Join the in-process emulation thread and tear down its bridge (no-op for
	// the child-process embed path, which uses neither).
	if (m_embedThread.joinable())
		m_embedThread.join();
	m_embedSession.reset();
	setMachineControlsActive(false);

	// Phase 13: the worker (and its GL context) are gone now, so it is safe to
	// destroy the GUI-owned render surface on this (GUI) thread.  The container
	// owns the QWindow (createWindowContainer reparents it), so deleting the
	// container deletes the window.
	if (m_nativeGlContainer)
	{
		if (m_nativeGlPlacedInPane)
		{
			m_artwork->detachGame();   // reparents the container out of the pane
		}
		else
		{
			m_centralStack->setCurrentWidget(m_splitter);   // back to the browser first
			m_centralStack->removeWidget(m_nativeGlContainer);
		}
		m_nativeGlContainer->deleteLater();
		m_nativeGlContainer = nullptr;
		m_nativeGlWindow = nullptr;
	}
	else if (m_nativeGlWindow)   // top-level fallback (unused once embedded)
	{
		m_nativeGlWindow->destroy();
		m_nativeGlWindow->deleteLater();
		m_nativeGlWindow = nullptr;
	}
	m_nativeGlTarget.reset();

	if (m_standaloneEmbed)
	{
		// Launched via --gooey with no browser: exiting the game quits the application.
		QApplication::quit();
		return;
	}

	if (m_quitAfterStop)
	{
		// The user closed the (pane-mode) main window: now that the game has
		// stopped and its surface is gone, finish quitting the application.
		m_quitAfterStop = false;
		saveSettings();
		saveSoftwareCache();
		QApplication::quit();
		return;
	}

	returnFromEmbed();
	m_playAct->setEnabled(!selectedSystem().isEmpty());
	activateWindow();
	raise();

	if (exitCode != 0)
		statusBar()->showMessage(tr("Embedded run exited with code %1").arg(exitCode));
	else
		updateStatusCount();

	// If a new game was requested while this one was running, start it now that
	// the teardown is complete (deferred a turn so all state has settled).
	if (m_hasPendingLaunch)
	{
		m_hasPendingLaunch = false;
		QString const sys = m_pendingLaunchSystem;
		QString const sw = m_pendingLaunchSoftware;
		m_pendingLaunchSystem.clear();
		m_pendingLaunchSoftware.clear();
		QTimer::singleShot(0, this, [this, sys, sw] { launchSystem(sys, sw); });
	}
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
			tr("About GooeyMAME"),
			tr("GooeyMAME – a cross-platform Qt front-end for MAME."));
}

} // namespace osd::qtui
