// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  artworkpanel.h - artwork/screenshot viewer for the selected item
//
//  Shows tabs of MAME EXTRAs images for the current system, or for a
//  selected software-list item (which uses the "_SL" archives keyed by
//  "<list>/<software>.png").  Images load on a worker thread.
//
//============================================================
#ifndef MAME_OSD_QTUI_ARTWORKPANEL_H
#define MAME_OSD_QTUI_ARTWORKPANEL_H

#pragma once

#include <QtWidgets/QWidget>

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <QtGui/QPixmap>

#include <vector>

class QLabel;
class QResizeEvent;
class QTabWidget;

namespace osd::qtui {

class ArtLoader;
class InfoLoader;

class ArtworkPanel : public QWidget
{
	Q_OBJECT

public:
	explicit ArtworkPanel(QWidget *parent = nullptr);

	// Show artwork for a system (by short name).
	void setSystem(const QString &shortName);

	// Show artwork for a software item from a system's software list.
	void setSoftware(const QString &list, const QString &software);

protected:
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void loadCurrent();
	void onLoaded(quint64 epoch, int tab, const QByteArray &bytes);
	void onInfoLoaded(quint64 epoch, const QString &text);

private:
	enum class Mode { System, Software };

	void refresh();   // invalidate all tabs and (re)load the visible one
	void rescale(int index);

	struct Tab
	{
		bool isText;      // true = history text tab; false = image tab
		QString sysKey;   // frontendpaths key in system mode ("" = none)
		QString swKey;    // frontendpaths key in software mode ("" = none)
		QWidget *view;    // QLabel (image) or QTextBrowser (text)
		bool loaded;
		QPixmap original; // image tabs only
	};

	QTabWidget *m_tabs = nullptr;
	ArtLoader *m_loader = nullptr;
	InfoLoader *m_info = nullptr;
	int m_historyTab = -1;
	std::vector<Tab> m_views;

	Mode m_mode = Mode::System;
	QString m_system;
	QString m_swList;
	QString m_swName;
	quint64 m_epoch = 0;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_ARTWORKPANEL_H
