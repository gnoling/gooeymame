// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  artworkpanel.h - artwork/screenshot viewer for the selected item
//
//  Shows tabs of MAME EXTRAs images for the current system, or for a
//  selected software-list item (which uses the "_SL" archives keyed by
//  "<list>/<software>.png", with a fall back to the host machine's art).
//  A second group of tabs shows the text databases (history/mameinfo/...).
//  The two groups can be arranged as a split (art over info), or either one
//  on its own, via a layout selector.  Images and text load on worker
//  threads.
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

class QComboBox;
class QHideEvent;
class QLabel;
class QResizeEvent;
class QSplitter;
class QTabWidget;

namespace osd::qtui {

class ArtLoader;
class InfoLoader;
class VideoTab;
class SoundtrackTab;
class ManualTab;

class ArtworkPanel : public QWidget
{
	Q_OBJECT

public:
	explicit ArtworkPanel(QWidget *parent = nullptr);

	// Show artwork for a system (by short name).
	void setSystem(const QString &shortName);

	// Show artwork for a software item from a system's software list.
	void setSoftware(const QString &list, const QString &software);

	// Host an embedded game's video widget in the top of the panel and switch to
	// a game view (adds "Game only / Game + Art / Game + Info" choices).  Pass
	// nullptr / call detachGame() to remove it and restore the normal view.
	void attachGame(QWidget *game);
	void detachGame();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void hideEvent(QHideEvent *event) override;

private slots:
	void loadCurrent();
	void onLayoutChanged(int index);
	void onLoaded(quint64 epoch, int tab, const QByteArray &bytes);
	void onInfoLoaded(quint64 epoch, int source, const QString &text);

private:
	enum class Mode { System, Software };
	enum Layout { Split = 0, ArtOnly, InfoOnly, GameOnly, GameArt, GameInfo };
	enum TabKind { KindImage, KindText, KindVideo, KindSoundtrack, KindManual };

	void refresh();          // invalidate all tabs and (re)load the visible ones
	void applyLayout(int layout);
	void loadTab(int index); // load one tab by index into m_views
	void rescale(int index);
	int indexOfView(QWidget *view) const;
	void loadVisible(QTabWidget *group);
	void stopAllMedia();     // pause every media tab
	void stopOtherMedia(int keepIndex);

	struct Tab
	{
		int kind;         // TabKind
		QString sysKey;   // image: frontendpaths key in system mode ("" = none)
		QString swKey;    // image: frontendpaths key in software mode ("" = none)
		int source;       // text: InfoLoader::Source
		QWidget *view;    // QLabel / QTextBrowser / VideoTab / SoundtrackTab
		bool loaded;
		QPixmap original; // image tabs only
	};

	QComboBox *m_layoutCombo = nullptr;
	QSplitter *m_splitter = nullptr;
	QTabWidget *m_artTabs = nullptr;
	QTabWidget *m_infoTabs = nullptr;
	ArtLoader *m_loader = nullptr;
	InfoLoader *m_info = nullptr;
	VideoTab *m_videoTab = nullptr;
	SoundtrackTab *m_soundtrackTab = nullptr;
	ManualTab *m_manualTab = nullptr;
	std::vector<Tab> m_views;
	int m_layout = Split;
	QWidget *m_gameWidget = nullptr;   // embedded game surface (when playing in-pane)
	int m_savedLayout = Split;         // layout to restore when the game detaches

	Mode m_mode = Mode::System;
	QString m_system;
	QString m_swList;
	QString m_swName;
	quint64 m_epoch = 0;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_ARTWORKPANEL_H
