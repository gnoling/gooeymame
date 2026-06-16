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
#include "manualtab.h"
#include "mediatabs.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtGui/QPixmap>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
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
	{ "Marquee",       "marquees",  ""          },
	{ "Control Panel", "cpanel",    ""          },
	{ "PCB",           "pcb",       ""          },
	{ "Cover",         "",          "covers"    },
	{ "Boss",          "bosses",    ""          },
	{ "Logo",          "logos",     ""          },
	{ "Artwork",       "artpreview","artpreview"},
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

// Return base/stem.<ext> for the first container that exists on disk, else "".
QString resolveVideo(const QString &base, const QString &stem)
{
	if (base.isEmpty() || stem.isEmpty())
		return QString();
	for (const char *ext : kVideoExtensions)
	{
		QString const path = base + QLatin1Char('/') + stem + QString::fromLatin1(ext);
		if (QFileInfo::exists(path))
			return path;
	}
	return QString();
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
		m_views.push_back({ KindImage, QString::fromLatin1(def.sysKey),
				QString::fromLatin1(def.swKey), 0, label, false, QPixmap() });
	}

	// Multimedia tabs sit alongside the images, just after the Snapshot tab.
	m_videoTab = new VideoTab(m_artTabs);
	m_artTabs->insertTab(1, m_videoTab, tr("Video"));
	m_views.push_back({ KindVideo, QString(), QString(), 0, m_videoTab, false, QPixmap() });

	m_soundtrackTab = new SoundtrackTab(m_artTabs);
	m_artTabs->insertTab(2, m_soundtrackTab, tr("Soundtrack"));
	m_views.push_back({ KindSoundtrack, QString(), QString(), 0, m_soundtrackTab, false, QPixmap() });

	m_infoTabs = new QTabWidget(this);
	for (const InfoDef &def : kInfoTabs)
	{
		QTextBrowser *browser = new QTextBrowser(m_infoTabs);
		browser->setOpenExternalLinks(true);
		m_infoTabs->addTab(browser, tr(def.label));
		m_views.push_back({ KindText, QString(), QString(), def.source, browser, false, QPixmap() });
	}

	// Manual tab: a PDF viewer (manuals / manuals_SL).
	m_manualTab = new ManualTab(m_infoTabs);
	m_infoTabs->addTab(m_manualTab, tr("Manual"));
	m_views.push_back({ KindManual, QString(), QString(), 0, m_manualTab, false, QPixmap() });

	connect(m_artTabs, &QTabWidget::currentChanged, this, &ArtworkPanel::loadCurrent);
	connect(m_infoTabs, &QTabWidget::currentChanged, this, &ArtworkPanel::loadCurrent);

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

	// Restore the saved layout choice.
	QSettings settings;
	int const saved = settings.value(QStringLiteral("artwork/layout"), int(Split)).toInt();
	m_layout = (saved >= Split && saved <= InfoOnly) ? saved : Split;
	m_layoutCombo->setCurrentIndex(m_layout);
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

void ArtworkPanel::setSoftware(const QString &list, const QString &software)
{
	if (m_mode == Mode::Software && list == m_swList && software == m_swName)
		return;
	m_mode = Mode::Software;
	m_swList = list;
	m_swName = software;
	refresh();
}

void ArtworkPanel::onLayoutChanged(int index)
{
	int const layout = m_layoutCombo->itemData(index).toInt();
	QSettings settings;
	settings.setValue(QStringLiteral("artwork/layout"), layout);
	applyLayout(layout);
}

void ArtworkPanel::applyLayout(int layout)
{
	m_layout = layout;
	bool const artShown = (layout == Split || layout == ArtOnly);
	m_artTabs->setVisible(artShown);
	m_infoTabs->setVisible(layout == Split || layout == InfoOnly);
	if (!artShown)
		stopAllMedia();   // no point playing what cannot be seen
	loadCurrent();        // load anything newly shown
}

void ArtworkPanel::stopAllMedia()
{
	if (m_videoTab)
		m_videoTab->stop();
	if (m_soundtrackTab)
		m_soundtrackTab->stop();
}

void ArtworkPanel::stopOtherMedia(int keepIndex)
{
	for (int i = 0; i < int(m_views.size()); ++i)
	{
		if (i == keepIndex)
			continue;
		if (m_views[i].kind == KindVideo)
			m_videoTab->stop();
		else if (m_views[i].kind == KindSoundtrack)
			m_soundtrackTab->stop();
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
			m_videoTab->resume();
		else if (tab.kind == KindSoundtrack)
			m_soundtrackTab->play();
		return;
	}
	tab.loaded = true;

	// Video snap tab: resolve a loose file (software first, machine fallback).
	if (tab.kind == KindVideo)
	{
		QString const base = frontendFolderPath(QStringLiteral("videosnaps"));
		QString const slBase = frontendFolderPath(QStringLiteral("videosnaps_sl"));
		if (base.isEmpty() && slBase.isEmpty())
		{
			m_videoTab->setFile(QString(), tr("Video folder not configured."));
			return;
		}
		QString path;
		if (m_mode == Mode::Software && !slBase.isEmpty() && !m_swList.isEmpty() && !m_swName.isEmpty())
			path = resolveVideo(slBase, m_swList + QLatin1Char('/') + m_swName);
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
		m_videoTab->setFile(path, tr("No video for this item."));
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

	// Manual tab: resolve a PDF (software first, then machine + clone parent).
	if (tab.kind == KindManual)
	{
		QString const base = frontendFolderPath(QStringLiteral("manuals"));
		QString const slBase = frontendFolderPath(QStringLiteral("manuals_sl"));
		if (base.isEmpty() && slBase.isEmpty())
		{
			m_manualTab->setMessage(tr("Manuals folder not configured."));
			return;
		}

		ArtCandidates candidates;
		if (m_mode == Mode::Software && !slBase.isEmpty() && !m_swList.isEmpty() && !m_swName.isEmpty())
			candidates.append({ slBase, m_swList + QLatin1Char('/') + m_swName + QStringLiteral(".pdf") });
		if (!base.isEmpty() && !m_system.isEmpty())
		{
			candidates.append({ base, m_system + QStringLiteral(".pdf") });
			std::string const parent = qtui_parent_of(m_system.toStdString());
			if (!parent.empty())
				candidates.append({ base, QString::fromStdString(parent) + QStringLiteral(".pdf") });
		}

		if (candidates.isEmpty())
		{
			m_manualTab->setMessage(tr("No manual"));
			return;
		}
		m_manualTab->setMessage(tr("Loading…"));
		m_loader->request(m_epoch, index, candidates);
		return;
	}

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
				candidates.append({ path, m_swList + QLatin1Char('/') + m_swName + QStringLiteral(".png") });
		}
		addSystemArt();   // machine fallback
	}
	else
	{
		addSystemArt();
	}

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

	// Manuals are PDFs, not images: hand the bytes to the viewer.
	if (t.kind == KindManual)
	{
		m_manualTab->setPdf(bytes);
		return;
	}

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

void ArtworkPanel::rescale(int index)
{
	if (index < 0 || index >= int(m_views.size()))
		return;
	Tab &tab = m_views[index];
	if (tab.kind != KindImage || tab.original.isNull())
		return;
	if (auto *label = qobject_cast<QLabel *>(tab.view))
		label->setPixmap(tab.original.scaled(
				label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
