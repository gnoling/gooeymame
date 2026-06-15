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

#include <QtGui/QPixmap>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>


namespace osd::qtui {

namespace {

// Tabs and the frontendpaths key used in each mode ("" = unavailable here).
struct TabDef { const char *label; const char *sysKey; const char *swKey; bool text; };
const TabDef kTabs[] =
{
	{ "Snapshot",      "snap",     "snap_sl",   false },
	{ "History",       "",         "",          true  },
	{ "Title",         "titles",   "titles_sl", false },
	{ "Cover",         "",         "covers",    false },
	{ "Cabinet",       "cabinets", "",          false },
	{ "Control Panel", "cpanel",   "",          false },
	{ "Marquee",       "marquees", "",          false },
	{ "Flyer",         "flyers",   "",          false },
	{ "PCB",           "pcb",      "",          false },
};

} // anonymous namespace

ArtworkPanel::ArtworkPanel(QWidget *parent) :
	QWidget(parent)
{
	m_tabs = new QTabWidget(this);
	for (const TabDef &def : kTabs)
	{
		QWidget *view;
		if (def.text)
		{
			QTextBrowser *browser = new QTextBrowser(m_tabs);
			browser->setOpenExternalLinks(true);
			view = browser;
			m_historyTab = int(m_views.size());
		}
		else
		{
			QLabel *label = new QLabel(m_tabs);
			label->setAlignment(Qt::AlignCenter);
			label->setMinimumSize(160, 120);
			view = label;
		}
		m_tabs->addTab(view, tr(def.label));
		m_views.push_back({ def.text, QString::fromLatin1(def.sysKey),
				QString::fromLatin1(def.swKey), view, false, QPixmap() });
	}
	connect(m_tabs, &QTabWidget::currentChanged, this, &ArtworkPanel::loadCurrent);

	m_loader = new ArtLoader(this);
	connect(m_loader, &ArtLoader::loaded, this, &ArtworkPanel::onLoaded);

	m_info = new InfoLoader(this);
	connect(m_info, &InfoLoader::loaded, this, &ArtworkPanel::onInfoLoaded);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_tabs);
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

namespace {

// QLabel and QTextBrowser both have setText()/clear(), but as a QWidget* the
// view must be cast first.
void setViewText(QWidget *view, const QString &text)
{
	if (auto *label = qobject_cast<QLabel *>(view))
		label->setText(text);
	else if (auto *browser = qobject_cast<QTextBrowser *>(view))
		browser->setText(text);
}

} // anonymous namespace

void ArtworkPanel::refresh()
{
	++m_epoch;   // invalidate any in-flight loads
	for (Tab &tab : m_views)
	{
		tab.loaded = false;
		tab.original = QPixmap();
		setViewText(tab.view, QString());
	}
	loadCurrent();
}

void ArtworkPanel::loadCurrent()
{
	int const idx = m_tabs->currentIndex();
	if (idx < 0 || idx >= int(m_views.size()))
		return;

	Tab &tab = m_views[idx];
	if (tab.loaded)
	{
		if (!tab.isText)
			rescale(idx);
		return;
	}
	tab.loaded = true;

	// History (text) tab: look up history.xml for the current item.
	if (tab.isText)
	{
		QString key;
		if (m_mode == Mode::System)
			key = m_system;
		else if (!m_swList.isEmpty() && !m_swName.isEmpty())
			key = m_swList + QLatin1Char('/') + m_swName;

		if (key.isEmpty() || frontendFolderPath(QStringLiteral("history")).isEmpty())
		{
			setViewText(tab.view, frontendFolderPath(QStringLiteral("history")).isEmpty()
					? tr("History file not configured.") : tr("No history available."));
			return;
		}
		setViewText(tab.view, tr("Loading…"));
		m_info->request(m_epoch, key);
		return;
	}

	// Build the candidate chain, most-specific first.  In software mode we
	// try the software's own art, then fall back to the host machine's art
	// (so e.g. an NES cartridge still shows the NES cabinet).
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
	m_loader->request(m_epoch, idx, candidates);
}

void ArtworkPanel::onLoaded(quint64 epoch, int tab, const QByteArray &bytes)
{
	if (epoch != m_epoch || tab < 0 || tab >= int(m_views.size()))
		return;   // stale

	Tab &t = m_views[tab];
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
	if (tab == m_tabs->currentIndex())
		rescale(tab);
}

void ArtworkPanel::onInfoLoaded(quint64 epoch, const QString &text)
{
	if (epoch != m_epoch || m_historyTab < 0)
		return;   // stale
	setViewText(m_views[m_historyTab].view, text.isEmpty() ? tr("No history available.") : text);
}

void ArtworkPanel::rescale(int index)
{
	if (index < 0 || index >= int(m_views.size()))
		return;
	Tab &tab = m_views[index];
	if (tab.isText || tab.original.isNull())
		return;
	if (auto *label = qobject_cast<QLabel *>(tab.view))
		label->setPixmap(tab.original.scaled(
				label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ArtworkPanel::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	rescale(m_tabs->currentIndex());
}

} // namespace osd::qtui
