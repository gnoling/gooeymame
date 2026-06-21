// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  artworkpanel.cpp - artwork/screenshot viewer for the selected item
//
//============================================================

#include "artworkpanel.h"

#include "artloader.h"
#include "emulator.h"
#include "frontendpaths.h"
#include "infoloader.h"
#include "mediatabs.h"
#ifndef QTUI_NO_PDF
#include "manualtab.h"
#endif

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#include <QtGui/QPixmap>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>


namespace osd::qtui {

namespace {

// Image tabs and the frontendpaths key used in each mode ("" = unavailable in
// that mode; software mode then falls back to the host machine's art).
struct ImageDef { const char *label; const char *sysKey; const char *swKey; };
const ImageDef kImageTabs[] =
{
	{ "Snapshot",      "snap",      "snap_sl"   },
	{ "Title",         "titles",    "titles_sl" },
	{ "Flyer",         "flyers",    ""          },
	{ "Cabinet",       "cabinets",  ""          },
	{ "Marquee",       "marquees",  "marquees_sl" },
	{ "Control Panel", "cpanel",    ""          },
	{ "PCB",           "pcb",       ""          },
	{ "Cover",         "",          "covers"    },
	{ "Boss",          "bosses",    ""          },
	{ "Logo",          "logos",     "logos_sl"  },
	{ "Artwork",       "artpreview","artpreview"},
	// Software-list art, resolved from the secondary media root when present.
	{ "Box",           "",          "box_sl"       },
	{ "Box 3D",        "",          "box3d_sl"     },
	{ "Box Back",      "",          "boxback_sl"   },
	{ "Box Full",      "",          "boxfull_sl"   },
	{ "Cart",          "",          "cart_sl"      },
	{ "Cart 3D",       "",          "cart3d_sl"    },
	{ "Cart Top",      "",          "carttop_sl"   },
	{ "Background",    "",          "background_sl"},
	{ "Banner",        "",          "banner_sl"    },
	{ "Advert Art",    "",          "advertimg_sl" },
	{ "Select",        "select",    ""          },
	{ "Versus",        "versus",    ""          },
	{ "Score",         "scores",    ""          },
	{ "Game Over",     "gameover",  ""          },
	{ "How To",        "howto",     ""          },
	{ "End",           "ends",      ""          },
	{ "Warning",       "warning",   ""          },
	{ "Devices",       "devices",   ""          },
};

// Text-database tabs and the InfoLoader source they read.
struct InfoDef { const char *label; int source; const char *key; };
const InfoDef kInfoTabs[] =
{
	{ "History",   InfoLoader::History,  "history"  },
	{ "MAME Info", InfoLoader::MameInfo, "mameinfo" },
	{ "Command",   InfoLoader::Command,  "command"  },
	{ "MESS Info", InfoLoader::MessInfo, "messinfo" },
	{ "Init",      InfoLoader::GameInit, "gameinit" },
	{ "System",    InfoLoader::SysInfo,  "sysinfo"  },
	{ "Story",     InfoLoader::Story,    "story"    },
	{ "Top Scores",InfoLoader::TopScores,"topscores"},
};

// QLabel and QTextBrowser both have setText(), but as a QWidget* the view must
// be cast first.  Media tabs are not text and are ignored here.
void setViewText(QWidget *view, const QString &text)
{
	if (auto *label = qobject_cast<QLabel *>(view))
		label->setText(text);
	else if (auto *browser = qobject_cast<QTextBrowser *>(view))
		browser->setText(text);
}

// Video snaps may be distributed in any of a few container formats.
const char *const kVideoExtensions[] = { ".mp4", ".mkv", ".avi", ".webm" };

// Audio formats found in soundtrack folders.
const char *const kAudioFilters[] = { "*.mp3", "*.flac", "*.ogg", "*.opus", "*.m4a", "*.wav" };

// Single-track audio formats for the per-game Music tab.
const char *const kMusicExtensions[] = { ".mp3", ".flac", ".ogg", ".opus", ".m4a", ".wav" };

// Return base/stem.<ext> for the first file that exists, trying `extensions`.
QString resolveByExt(const QString &base, const QString &stem,
		const char *const *extensions, std::size_t count)
{
	if (base.isEmpty() || stem.isEmpty())
		return QString();
	for (std::size_t i = 0; i < count; ++i)
	{
		QString const path = base + QLatin1Char('/') + stem + QString::fromLatin1(extensions[i]);
		if (QFileInfo::exists(path))
			return path;
	}
	return QString();
}

// Return base/stem.<ext> for the first container that exists on disk, else "".
QString resolveVideo(const QString &base, const QString &stem)
{
	return resolveByExt(base, stem, kVideoExtensions,
			sizeof(kVideoExtensions) / sizeof(kVideoExtensions[0]));
}

QString resolveAudio(const QString &base, const QString &stem)
{
	return resolveByExt(base, stem, kMusicExtensions,
			sizeof(kMusicExtensions) / sizeof(kMusicExtensions[0]));
}

// List the audio tracks in base/folder (sorted), or empty if the dir is absent.
QStringList listTracks(const QString &base, const QString &folder)
{
	if (base.isEmpty() || folder.isEmpty())
		return QStringList();
	QDir dir(base + QLatin1Char('/') + folder);
	if (!dir.exists())
		return QStringList();
	QStringList filters;
	for (const char *f : kAudioFilters)
		filters << QString::fromLatin1(f);
	QStringList out;
	for (const QString &name : dir.entryList(filters, QDir::Files, QDir::Name))
		out << dir.absoluteFilePath(name);
	return out;
}

} // anonymous namespace

// Settings key for an image type: its system key, or software key if it has no
// system art (e.g. "covers").
static QString imageTypeKey(const ImageDef &def)
{
	return (def.sysKey && def.sysKey[0]) ? QString::fromLatin1(def.sysKey)
			: QString::fromLatin1(def.swKey);
}

QVector<QPair<QString, QString>> artScaleTypes()
{
	QVector<QPair<QString, QString>> out;
	for (const ImageDef &def : kImageTabs)
		out.append({ QString::fromLatin1(def.label), imageTypeKey(def) });
	return out;
}

int artScaleMode(const QString &key)
{
	if (key.isEmpty())
		return ArtScaleSmooth;
	return QSettings().value(QStringLiteral("artscale/") + key, int(ArtScaleSmooth)).toInt();
}

void setArtScaleMode(const QString &key, int mode)
{
	if (!key.isEmpty())
		QSettings().setValue(QStringLiteral("artscale/") + key, mode);
}

bool artScaleInteger(const QString &key)
{
	if (key.isEmpty())
		return false;
	return QSettings().value(QStringLiteral("artscaleint/") + key, false).toBool();
}

void setArtScaleInteger(const QString &key, bool on)
{
	if (!key.isEmpty())
		QSettings().setValue(QStringLiteral("artscaleint/") + key, on);
}

ArtworkPanel::ArtworkPanel(QWidget *parent) :
	QWidget(parent)
{
	m_artTabs = new QTabWidget(this);
	for (const ImageDef &def : kImageTabs)
	{
		QLabel *label = new QLabel(m_artTabs);
		label->setAlignment(Qt::AlignCenter);
		label->setMinimumSize(160, 120);
		m_artTabs->addTab(label, tr(def.label));
		int const viewIndex = int(m_views.size());
		m_views.push_back({ KindImage, QString::fromLatin1(def.sysKey),
				QString::fromLatin1(def.swKey), 0, label, false, QPixmap() });

		// Right-click an image to choose its scaling (smooth / nearest).
		label->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(label, &QWidget::customContextMenuRequested, this,
				[this, viewIndex, label] (const QPoint &pos) {
					showImageScaleMenu(viewIndex, label->mapToGlobal(pos));
				});
	}

	// Multimedia tabs sit alongside the images, just after the Snapshot tab.
	// Video keys are carried on the Tab (like images) so each video tab resolves
	// its own source: the main snap (videosnaps/_sl) and the advert clip (advert_sl,
	// from the secondary media root only).
	m_videoTab = new VideoTab(m_artTabs);
	m_artTabs->insertTab(1, m_videoTab, tr("Video"));
	m_views.push_back({ KindVideo, QStringLiteral("videosnaps"), QStringLiteral("videosnaps_sl"),
			0, m_videoTab, false, QPixmap() });

	m_advertTab = new VideoTab(m_artTabs);
	m_artTabs->insertTab(2, m_advertTab, tr("Advert"));
	m_views.push_back({ KindVideo, QString(), QStringLiteral("advert_sl"),
			0, m_advertTab, false, QPixmap() });

	m_soundtrackTab = new SoundtrackTab(m_artTabs);
	m_artTabs->insertTab(3, m_soundtrackTab, tr("Soundtrack"));
	m_views.push_back({ KindSoundtrack, QString(), QString(), 0, m_soundtrackTab, false, QPixmap() });

	// Per-game music: a single audio file (music_sl, secondary root) in the same
	// player UI as the soundtrack list.
	m_musicTab = new SoundtrackTab(m_artTabs);
	m_artTabs->insertTab(4, m_musicTab, tr("Music"));
	m_views.push_back({ KindMusic, QString(), QStringLiteral("music_sl"), 0, m_musicTab, false, QPixmap() });

	m_infoTabs = new QTabWidget(this);
	for (const InfoDef &def : kInfoTabs)
	{
		QTextBrowser *browser = new QTextBrowser(m_infoTabs);
		browser->setOpenExternalLinks(true);
		m_infoTabs->addTab(browser, tr(def.label));
		m_views.push_back({ KindText, QString(), QString(), def.source, browser, false, QPixmap() });
	}

#ifndef QTUI_NO_PDF
	// PDF tabs: a manual (manuals / manuals_SL) and a game map (maps_sl, secondary
	// root).  Omitted on platforms without Qt PDF (e.g. MSYS2 MinGW64).  Keys are
	// carried on the Tab so each viewer resolves its own document.
	m_manualTab = new ManualTab(m_infoTabs);
	m_infoTabs->addTab(m_manualTab, tr("Manual"));
	m_views.push_back({ KindManual, QStringLiteral("manuals"), QStringLiteral("manuals_sl"),
			0, m_manualTab, false, QPixmap() });

	m_mapTab = new ManualTab(m_infoTabs);
	m_infoTabs->addTab(m_mapTab, tr("Map"));
	m_views.push_back({ KindManual, QString(), QStringLiteral("maps_sl"), 0, m_mapTab, false, QPixmap() });
#endif

	connect(m_artTabs, &QTabWidget::currentChanged, this, &ArtworkPanel::loadCurrent);
	connect(m_infoTabs, &QTabWidget::currentChanged, this, &ArtworkPanel::loadCurrent);
	// Remember the selected tab in each group across sessions.
	connect(m_artTabs, &QTabWidget::currentChanged, this, [] (int index) {
		QSettings().setValue(QStringLiteral("artwork/artTab"), index);
	});
	connect(m_infoTabs, &QTabWidget::currentChanged, this, [] (int index) {
		QSettings().setValue(QStringLiteral("artwork/infoTab"), index);
	});

	m_splitter = new QSplitter(Qt::Vertical, this);
	m_splitter->addWidget(m_artTabs);
	m_splitter->addWidget(m_infoTabs);
	m_splitter->setStretchFactor(0, 3);
	m_splitter->setStretchFactor(1, 1);
	// Dragging the handle resizes the tab widgets but not this panel, so
	// resizeEvent never fires; rescale the visible image explicitly.
	connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
		if (m_layout == Split || m_layout == ArtOnly)
			rescale(indexOfView(m_artTabs->currentWidget()));
		QSettings().setValue(QStringLiteral("artwork/splitter"), m_splitter->saveState());
	});

	m_loader = new ArtLoader(this);
	connect(m_loader, &ArtLoader::loaded, this, &ArtworkPanel::onLoaded);

	m_info = new InfoLoader(this);
	connect(m_info, &InfoLoader::loaded, this, &ArtworkPanel::onInfoLoaded);

	m_layoutCombo = new QComboBox(this);
	m_layoutCombo->addItem(tr("Art + Info"), int(Split));
	m_layoutCombo->addItem(tr("Art only"), int(ArtOnly));
	m_layoutCombo->addItem(tr("Info only"), int(InfoOnly));

	QHBoxLayout *bar = new QHBoxLayout;
	bar->setContentsMargins(4, 2, 4, 2);
	bar->addWidget(new QLabel(tr("View:"), this));
	bar->addWidget(m_layoutCombo);
	bar->addStretch();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addLayout(bar);
	layout->addWidget(m_splitter);

	// Restore the saved layout choice and the art/info split sizes.
	QSettings settings;
	int const saved = settings.value(QStringLiteral("artwork/layout"), int(Split)).toInt();
	m_layout = (saved >= Split && saved <= InfoOnly) ? saved : Split;
	m_layoutCombo->setCurrentIndex(m_layout);
	QByteArray const splitState = settings.value(QStringLiteral("artwork/splitter")).toByteArray();
	if (!splitState.isEmpty())
		m_splitter->restoreState(splitState);
	m_artTabs->setCurrentIndex(settings.value(QStringLiteral("artwork/artTab"), 0).toInt());
	m_infoTabs->setCurrentIndex(settings.value(QStringLiteral("artwork/infoTab"), 0).toInt());
	applyLayout(m_layout);
	connect(m_layoutCombo, qOverload<int>(&QComboBox::currentIndexChanged),
			this, &ArtworkPanel::onLayoutChanged);
}

void ArtworkPanel::setSystem(const QString &shortName)
{
	if (m_mode == Mode::System && shortName == m_system)
		return;
	m_mode = Mode::System;
	m_system = shortName;
	refresh();
}

void ArtworkPanel::setSoftware(const QString &list, const QString &software, const QString &parent)
{
	if (m_mode == Mode::Software && list == m_swList && software == m_swName)
		return;
	m_mode = Mode::Software;
	m_swList = list;
	m_swName = software;
	m_swParent = parent;
	refresh();
}

void ArtworkPanel::onLayoutChanged(int index)
{
	int const layout = m_layoutCombo->itemData(index).toInt();
	// Persist only the normal (non-game) views; game views are transient.
	if (layout >= Split && layout <= InfoOnly)
		QSettings().setValue(QStringLiteral("artwork/layout"), layout);
	applyLayout(layout);
}

void ArtworkPanel::applyLayout(int layout)
{
	m_layout = layout;
	bool const gameShown = (layout == GameOnly || layout == GameArt || layout == GameInfo);
	bool const artShown = (layout == Split || layout == ArtOnly || layout == GameArt);
	bool const infoShown = (layout == Split || layout == InfoOnly || layout == GameInfo);
	if (m_gameWidget)
		m_gameWidget->setVisible(gameShown);
	m_artTabs->setVisible(artShown);
	m_infoTabs->setVisible(infoShown);
	if (!artShown)
		stopAllMedia();   // no point playing what cannot be seen
	loadCurrent();        // load anything newly shown
}

void ArtworkPanel::attachGame(QWidget *game)
{
	if (!game)
		return;
	m_gameWidget = game;
	m_splitter->insertWidget(0, game);
	m_splitter->setStretchFactor(0, 4);
	game->show();

	// Remember the non-game layout and offer the game views.
	m_savedLayout = (m_layout >= GameOnly) ? Split : m_layout;
	QSignalBlocker block(m_layoutCombo);
	if (m_layoutCombo->findData(int(GameArt)) < 0)
	{
		m_layoutCombo->addItem(tr("Game only"), int(GameOnly));
		m_layoutCombo->addItem(tr("Game + Art"), int(GameArt));
		m_layoutCombo->addItem(tr("Game + Info"), int(GameInfo));
	}
	int const idx = m_layoutCombo->findData(int(GameOnly));
	m_layoutCombo->setCurrentIndex(idx);
	applyLayout(GameOnly);
}

void ArtworkPanel::detachGame()
{
	if (!m_gameWidget)
		return;
	// Remove the game from the splitter without destroying it (MainWindow owns
	// it and reuses it for the next launch).
	m_gameWidget->hide();
	m_gameWidget->setParent(nullptr);
	m_gameWidget = nullptr;

	QSignalBlocker block(m_layoutCombo);
	for (int v : { int(GameOnly), int(GameArt), int(GameInfo) })
	{
		int const i = m_layoutCombo->findData(v);
		if (i >= 0)
			m_layoutCombo->removeItem(i);
	}
	int const idx = m_layoutCombo->findData(m_savedLayout);
	m_layoutCombo->setCurrentIndex(idx >= 0 ? idx : 0);
	applyLayout(m_savedLayout);
}

void ArtworkPanel::stopAllMedia()
{
	for (const Tab &tab : m_views)
	{
		if (tab.kind == KindVideo)
			static_cast<VideoTab *>(tab.view)->stop();
		else if (tab.kind == KindSoundtrack || tab.kind == KindMusic)
			static_cast<SoundtrackTab *>(tab.view)->stop();
	}
}

void ArtworkPanel::stopOtherMedia(int keepIndex)
{
	for (int i = 0; i < int(m_views.size()); ++i)
	{
		if (i == keepIndex)
			continue;
		if (m_views[i].kind == KindVideo)
			static_cast<VideoTab *>(m_views[i].view)->stop();
		else if (m_views[i].kind == KindSoundtrack || m_views[i].kind == KindMusic)
			static_cast<SoundtrackTab *>(m_views[i].view)->stop();
	}
}

void ArtworkPanel::refresh()
{
	++m_epoch;   // invalidate any in-flight loads
	stopAllMedia();
	for (Tab &tab : m_views)
	{
		tab.loaded = false;
		tab.original = QPixmap();
		setViewText(tab.view, QString());
	}
	loadCurrent();
}

int ArtworkPanel::indexOfView(QWidget *view) const
{
	for (int i = 0; i < int(m_views.size()); ++i)
		if (m_views[i].view == view)
			return i;
	return -1;
}

void ArtworkPanel::loadVisible(QTabWidget *group)
{
	int const idx = indexOfView(group->currentWidget());
	if (idx < 0)
		return;
	// Switching away from a video/soundtrack tab should pause its playback.
	if (group == m_artTabs)
		stopOtherMedia(idx);
	loadTab(idx);
}

void ArtworkPanel::loadCurrent()
{
	// Key loading off the chosen layout rather than on-screen visibility: at
	// startup the tab widgets are not yet shown when the restored selection
	// arrives, so isVisible() would wrongly skip the initial load.
	if (m_layout == Split || m_layout == ArtOnly)
		loadVisible(m_artTabs);
	if (m_layout == Split || m_layout == InfoOnly)
		loadVisible(m_infoTabs);
}

void ArtworkPanel::addSecondaryCandidates(
		QVector<QPair<QString, QString>> &candidates, const QString &key, const QString &ext) const
{
	if (key.isEmpty())
		return;
	QString const root = frontendFolderPath(QStringLiteral("secondaryRoot"));
	if (root.isEmpty())
		return;
	QString const base = root + QLatin1Char('/') + key;
	if (m_mode == Mode::Software)
	{
		if (m_swList.isEmpty() || m_swName.isEmpty())
			return;
		candidates.append({ base, m_swList + QLatin1Char('/') + m_swName + ext });
		if (!m_swParent.isEmpty())
			candidates.append({ base, m_swList + QLatin1Char('/') + m_swParent + ext });
	}
	else if (!m_system.isEmpty())
	{
		candidates.append({ base, m_system + ext });
		std::string const parent = qtui_parent_of(m_system.toStdString());
		if (!parent.empty())
			candidates.append({ base, QString::fromStdString(parent) + ext });
	}
}

void ArtworkPanel::loadTab(int index)
{
	if (index < 0 || index >= int(m_views.size()))
		return;

	Tab &tab = m_views[index];
	if (tab.loaded)
	{
		// Already loaded: rescale images, resume playback for media.
		if (tab.kind == KindImage)
			rescale(index);
		else if (tab.kind == KindVideo)
		{
			if (VideoTab *const video = static_cast<VideoTab *>(tab.view))
				video->resume();
		}
		else if (tab.kind == KindSoundtrack || tab.kind == KindMusic)
			static_cast<SoundtrackTab *>(tab.view)->play();
		return;
	}
	tab.loaded = true;

	// Video tab: resolve a loose file (software first, machine fallback, then the
	// secondary media root).  Keys come from the Tab so each video tab (snap,
	// advert) resolves independently.
	if (tab.kind == KindVideo)
	{
		VideoTab *const video = static_cast<VideoTab *>(tab.view);
		QString const base = tab.sysKey.isEmpty() ? QString() : frontendFolderPath(tab.sysKey);
		QString const slBase = tab.swKey.isEmpty() ? QString() : frontendFolderPath(tab.swKey);
		QString const secBase = (tab.swKey.isEmpty() || frontendFolderPath(QStringLiteral("secondaryRoot")).isEmpty())
				? QString()
				: frontendFolderPath(QStringLiteral("secondaryRoot")) + QLatin1Char('/') + tab.swKey;

		QString path;
		auto trySoftware = [&] (const QString &dir) {
			if (path.isEmpty() && !dir.isEmpty() && m_mode == Mode::Software && !m_swList.isEmpty() && !m_swName.isEmpty())
			{
				path = resolveVideo(dir, m_swList + QLatin1Char('/') + m_swName);
				if (path.isEmpty() && !m_swParent.isEmpty())
					path = resolveVideo(dir, m_swList + QLatin1Char('/') + m_swParent);
			}
		};
		trySoftware(slBase);
		if (path.isEmpty() && !base.isEmpty() && !m_system.isEmpty())
		{
			path = resolveVideo(base, m_system);
			if (path.isEmpty())
			{
				std::string const parent = qtui_parent_of(m_system.toStdString());
				if (!parent.empty())
					path = resolveVideo(base, QString::fromStdString(parent));
			}
		}
		trySoftware(secBase);   // secondary media root last
		video->setFile(path, tr("No video for this item."));
		return;
	}

	// Soundtrack tab: list the audio files in the machine's folder.
	if (tab.kind == KindSoundtrack)
	{
		QString const base = frontendFolderPath(QStringLiteral("soundtrack"));
		if (base.isEmpty())
		{
			m_soundtrackTab->setTracks(QStringList(), tr("Soundtrack folder not configured."));
			return;
		}
		QStringList tracks = listTracks(base, m_system);
		if (tracks.isEmpty() && !m_system.isEmpty())
		{
			std::string const parent = qtui_parent_of(m_system.toStdString());
			if (!parent.empty())
				tracks = listTracks(base, QString::fromStdString(parent));
		}
		m_soundtrackTab->setTracks(tracks, tr("No soundtrack for this item."));
		return;
	}

	// Music tab: a single per-game audio file (software + clone parent + secondary).
	if (tab.kind == KindMusic)
	{
		SoundtrackTab *const music = static_cast<SoundtrackTab *>(tab.view);
		QString const slBase = tab.swKey.isEmpty() ? QString() : frontendFolderPath(tab.swKey);
		QString const secBase = (tab.swKey.isEmpty() || frontendFolderPath(QStringLiteral("secondaryRoot")).isEmpty())
				? QString()
				: frontendFolderPath(QStringLiteral("secondaryRoot")) + QLatin1Char('/') + tab.swKey;

		QString path;
		auto trySoftware = [&] (const QString &dir) {
			if (path.isEmpty() && !dir.isEmpty() && m_mode == Mode::Software && !m_swList.isEmpty() && !m_swName.isEmpty())
			{
				path = resolveAudio(dir, m_swList + QLatin1Char('/') + m_swName);
				if (path.isEmpty() && !m_swParent.isEmpty())
					path = resolveAudio(dir, m_swList + QLatin1Char('/') + m_swParent);
			}
		};
		trySoftware(slBase);
		trySoftware(secBase);
		music->setTracks(path.isEmpty() ? QStringList() : QStringList{ path },
				tr("No music for this item."));
		return;
	}

	// PDF tab (Manual / Map): resolve a PDF (software first, then machine + clone
	// parent, then the secondary root).  Keys come from the Tab.
#ifndef QTUI_NO_PDF
	if (tab.kind == KindManual)
	{
		ManualTab *const pdf = static_cast<ManualTab *>(tab.view);
		QString const base = tab.sysKey.isEmpty() ? QString() : frontendFolderPath(tab.sysKey);
		QString const slBase = tab.swKey.isEmpty() ? QString() : frontendFolderPath(tab.swKey);
		bool const haveSecondary = !frontendFolderPath(QStringLiteral("secondaryRoot")).isEmpty();
		if (base.isEmpty() && slBase.isEmpty() && !haveSecondary)
		{
			pdf->setMessage(tr("Folder not configured."));
			return;
		}

		ArtCandidates candidates;
		if (m_mode == Mode::Software && !slBase.isEmpty() && !m_swList.isEmpty() && !m_swName.isEmpty())
		{
			candidates.append({ slBase, m_swList + QLatin1Char('/') + m_swName + QStringLiteral(".pdf") });
			if (!m_swParent.isEmpty())
				candidates.append({ slBase, m_swList + QLatin1Char('/') + m_swParent + QStringLiteral(".pdf") });
		}
		if (!base.isEmpty() && !m_system.isEmpty())
		{
			candidates.append({ base, m_system + QStringLiteral(".pdf") });
			std::string const parent = qtui_parent_of(m_system.toStdString());
			if (!parent.empty())
				candidates.append({ base, QString::fromStdString(parent) + QStringLiteral(".pdf") });
		}

		addSecondaryCandidates(candidates,
				m_mode == Mode::Software ? tab.swKey : tab.sysKey, QStringLiteral(".pdf"));

		if (candidates.isEmpty())
		{
			pdf->setMessage(tr("Not available"));
			return;
		}
		pdf->setMessage(tr("Loading…"));
		m_loader->request(m_epoch, index, candidates);
		return;
	}
#endif

	// Text database tab: look up the configured source for the current item.
	if (tab.kind == KindText)
	{
		const char *fileKey = "history";
		for (const InfoDef &def : kInfoTabs)
			if (def.source == tab.source)
				fileKey = def.key;

		// History follows the selection (system or software); the dat files are
		// keyed by the host machine short name.
		QString key;
		if (tab.source == InfoLoader::History)
		{
			if (m_mode == Mode::System)
				key = m_system;
			else if (!m_swList.isEmpty() && !m_swName.isEmpty())
				key = m_swList + QLatin1Char('/') + m_swName;
		}
		else
		{
			key = m_system;
		}

		if (frontendFolderPath(QString::fromLatin1(fileKey)).isEmpty())
		{
			setViewText(tab.view, tr("Not configured."));
			return;
		}
		if (key.isEmpty())
		{
			setViewText(tab.view, tr("No information available."));
			return;
		}
		setViewText(tab.view, tr("Loading…"));
		m_info->request(m_epoch, tab.source, key);
		return;
	}

	// Image tab.  Build the candidate chain, most-specific first.  In software
	// mode we try the software's own art, then fall back to the host machine's
	// art (so e.g. an NES cartridge still shows the NES cabinet).
	ArtCandidates candidates;

	auto addSystemArt = [&] {
		if (tab.sysKey.isEmpty() || m_system.isEmpty())
			return;
		QString const path = frontendFolderPath(tab.sysKey);
		if (path.isEmpty())
			return;
		candidates.append({ path, m_system + QStringLiteral(".png") });
		std::string const parent = qtui_parent_of(m_system.toStdString());
		if (!parent.empty())
			candidates.append({ path, QString::fromStdString(parent) + QStringLiteral(".png") });
	};

	if (m_mode == Mode::Software)
	{
		if (!tab.swKey.isEmpty() && !m_swList.isEmpty() && !m_swName.isEmpty())
		{
			QString const path = frontendFolderPath(tab.swKey);
			if (!path.isEmpty())
			{
				candidates.append({ path, m_swList + QLatin1Char('/') + m_swName + QStringLiteral(".png") });
				// Fall back to the software clone parent: variants often share art,
				// and the media optimizer prunes a clone's copy when it matches.
				if (!m_swParent.isEmpty())
					candidates.append({ path, m_swList + QLatin1Char('/') + m_swParent + QStringLiteral(".png") });
			}
		}
		addSystemArt();   // machine fallback
	}
	else
	{
		addSystemArt();
	}

	// Secondary media root fills gaps the primary sources don't cover.
	addSecondaryCandidates(candidates,
			m_mode == Mode::Software ? tab.swKey : tab.sysKey, QStringLiteral(".png"));

	if (candidates.isEmpty())
	{
		setViewText(tab.view, tr("Not available"));
		return;
	}

	setViewText(tab.view, tr("Loading…"));
	m_loader->request(m_epoch, index, candidates);
}

void ArtworkPanel::onLoaded(quint64 epoch, int tab, const QByteArray &bytes)
{
	if (epoch != m_epoch || tab < 0 || tab >= int(m_views.size()))
		return;   // stale

	Tab &t = m_views[tab];

	// Manuals/maps are PDFs, not images: hand the bytes to that tab's viewer.
#ifndef QTUI_NO_PDF
	if (t.kind == KindManual)
	{
		static_cast<ManualTab *>(t.view)->setPdf(bytes);
		return;
	}
#endif

	if (bytes.isEmpty())
	{
		t.original = QPixmap();
		setViewText(t.view, tr("No image"));
		return;
	}

	QPixmap pixmap;
	if (!pixmap.loadFromData(bytes))
	{
		setViewText(t.view, tr("No image"));
		return;
	}
	t.original = pixmap;
	if (t.view == m_artTabs->currentWidget())
		rescale(tab);
}

void ArtworkPanel::onInfoLoaded(quint64 epoch, int source, const QString &text)
{
	if (epoch != m_epoch)
		return;   // stale
	for (Tab &tab : m_views)
	{
		if (tab.kind == KindText && tab.source == source)
		{
			setViewText(tab.view, text.isEmpty() ? tr("No information available.") : text);
			return;
		}
	}
}

QString ArtworkPanel::scaleKey(int index) const
{
	if (index < 0 || index >= int(m_views.size()))
		return QString();
	const Tab &tab = m_views[index];
	return tab.sysKey.isEmpty() ? tab.swKey : tab.sysKey;
}

void ArtworkPanel::rescale(int index)
{
	if (index < 0 || index >= int(m_views.size()))
		return;
	Tab &tab = m_views[index];
	if (tab.kind != KindImage || tab.original.isNull())
		return;
	auto *label = qobject_cast<QLabel *>(tab.view);
	if (!label)
		return;

	QString const key = scaleKey(index);
	Qt::TransformationMode const mode =
			(artScaleMode(key) == ArtScaleNearest) ? Qt::FastTransformation : Qt::SmoothTransformation;
	QSize const avail = label->size();
	QSize const src = tab.original.size();

	QSize target = avail;
	if (artScaleInteger(key) && src.width() > 0 && src.height() > 0)
	{
		// Largest integer multiple of the source that still fits; the centred
		// QLabel pillarboxes the result.  If the source is larger than the area,
		// integer upscaling is impossible, so fall back to fitting it.
		int const factor = qMin(avail.width() / src.width(), avail.height() / src.height());
		if (factor >= 1)
			target = src * factor;
	}
	label->setPixmap(tab.original.scaled(target, Qt::KeepAspectRatio, mode));
}

void ArtworkPanel::showImageScaleMenu(int index, const QPoint &globalPos)
{
	QString const key = scaleKey(index);
	if (key.isEmpty())
		return;

	QMenu menu;
	menu.addSection(tr("Image scaling"));
	QActionGroup *group = new QActionGroup(&menu);
	int const current = artScaleMode(key);
	struct { const char *label; int mode; } const modes[] = {
		{ "Smooth (blurry when enlarged)", ArtScaleSmooth },
		{ "Nearest neighbour (sharp pixels)", ArtScaleNearest },
	};
	for (const auto &m : modes)
	{
		QAction *act = menu.addAction(tr(m.label));
		act->setCheckable(true);
		act->setChecked(current == m.mode);
		group->addAction(act);
		connect(act, &QAction::triggered, this, [this, key, mode = m.mode, index] {
			setArtScaleMode(key, mode);
			rescale(index);
		});
	}

	menu.addSeparator();
	QAction *integer = menu.addAction(tr("Integer scaling (whole-pixel multiples)"));
	integer->setCheckable(true);
	integer->setChecked(artScaleInteger(key));
	connect(integer, &QAction::toggled, this, [this, key, index] (bool on) {
		setArtScaleInteger(key, on);
		rescale(index);
	});

	menu.exec(globalPos);
}

void ArtworkPanel::reloadScaling()
{
	// Re-apply scaling to the visible art image (after an Options change).
	rescale(indexOfView(m_artTabs->currentWidget()));
}

void ArtworkPanel::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (m_layout == Split || m_layout == ArtOnly)
		rescale(indexOfView(m_artTabs->currentWidget()));
}

void ArtworkPanel::hideEvent(QHideEvent *event)
{
	QWidget::hideEvent(event);
	stopAllMedia();   // don't keep playing audio/video while hidden
}

} // namespace osd::qtui
