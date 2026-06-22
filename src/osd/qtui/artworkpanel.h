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
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

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

// Per-art-type scaling for the art view: how an image is resampled when scaled.
enum ArtScaleMode { ArtScaleSmooth = 0, ArtScaleNearest = 1 };

// The art-view image types eligible for a per-type scaling mode, as
// (display label, settings key) — shared by the panel and the Options dialog.
QVector<QPair<QString, QString>> artScaleTypes();
int artScaleMode(const QString &key);                 // QSettings, default smooth
void setArtScaleMode(const QString &key, int mode);
bool artScaleInteger(const QString &key);             // integer (pixel-perfect) scaling, default off
void setArtScaleInteger(const QString &key, bool on);

class ArtworkPanel : public QWidget
{
	Q_OBJECT

public:
	explicit ArtworkPanel(QWidget *parent = nullptr);

	// Show artwork for a system (by short name).
	void setSystem(const QString &shortName);

	// Show artwork for a software item from a system's software list.  `parent`
	// is the clone parent (cloneof) short name, "" for a parent item.
	void setSoftware(const QString &list, const QString &software, const QString &parent = QString());

	// Re-apply art scaling settings to the visible image (after Options changes).
	void reloadScaling();

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
	enum TabKind { KindImage, KindText, KindVideo, KindSoundtrack, KindManual, KindMusic };

	void refresh();          // invalidate all tabs and (re)load the visible ones
	void applyLayout(int layout);
	void loadTab(int index); // load one tab by index into m_views
	// Append fallback candidates from the optional secondary media root
	// (folders/secondaryRoot, laid out <root>/<key>/<list>/<sw>.<ext>) for the
	// current item.  No-op when the root is unset.  `key` is the art-type key.
	void addSecondaryCandidates(QVector<QPair<QString, QString>> &candidates,
			const QString &key, const QString &ext) const;
	void rescale(int index);
	int indexOfView(QWidget *view) const;
	void loadVisible(QTabWidget *group);
	void updateTabVisibility(QTabWidget *group);     // hide empty tabs, re-home selection
	QStringList soundtrackTracks() const;
	void stopAllMedia();     // pause every media tab
	void stopOtherMedia(int keepIndex);
	void showImageScaleMenu(int index, const QPoint &globalPos);   // right-click on an art image
	QString scaleKey(int index) const;                             // settings key for an image tab

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

	// Candidate / path resolution for a tab (shared by load + content probe).
	QVector<QPair<QString, QString>> imageCandidates(const Tab &tab) const;
	QVector<QPair<QString, QString>> manualCandidates(const Tab &tab) const;
	QString videoPathFor(const Tab &tab) const;
	QString musicPathFor(const Tab &tab) const;
	bool tabHasContent(const Tab &tab) const;       // would this tab show anything?

	QComboBox *m_layoutCombo = nullptr;
	QSplitter *m_splitter = nullptr;
	QTabWidget *m_artTabs = nullptr;
	QTabWidget *m_infoTabs = nullptr;
	ArtLoader *m_loader = nullptr;
	InfoLoader *m_info = nullptr;
	VideoTab *m_videoTab = nullptr;
	VideoTab *m_advertTab = nullptr;
	SoundtrackTab *m_soundtrackTab = nullptr;
	SoundtrackTab *m_musicTab = nullptr;
	ManualTab *m_manualTab = nullptr;
	ManualTab *m_mapTab = nullptr;
	std::vector<Tab> m_views;

	// Canonical tab order per group (view + label), captured once after the tabs
	// are built.  updateTabVisibility() rebuilds each group from this so the tab
	// order is stable as empty tabs come and go.
	struct TabSlot { QWidget *view; QString label; };
	std::vector<TabSlot> m_artOrder;
	std::vector<TabSlot> m_infoOrder;
	int canonicalTab(const std::vector<TabSlot> &order, QWidget *view) const;

	int m_layout = Split;
	QWidget *m_gameWidget = nullptr;   // embedded game surface (when playing in-pane)
	int m_savedLayout = Split;         // layout to restore when the game detaches

	Mode m_mode = Mode::System;
	QString m_system;
	QString m_swList;
	QString m_swName;
	QString m_swParent;   // clone parent short name ("" if a parent)
	quint64 m_epoch = 0;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_ARTWORKPANEL_H
