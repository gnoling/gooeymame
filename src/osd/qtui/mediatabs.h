// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mediatabs.h - video and soundtrack tabs for the artwork panel
//
//  VideoTab plays a single video snap (videosnaps / videosnaps_SL) looped.
//  SoundtrackTab lists the audio tracks in a machine's soundtrack folder and
//  plays them with auto-advance.  Both use Qt Multimedia and play only from
//  loose files on disk (the EXTRAs distribute these unzipped).
//
//  These are plain QWidget subclasses (no Q_OBJECT): all wiring is done with
//  lambda connections, so no moc step is required.
//
//============================================================
#ifndef MAME_OSD_QTUI_MEDIATABS_H
#define MAME_OSD_QTUI_MEDIATABS_H

#pragma once

#include <QtWidgets/QWidget>

#include <QtCore/QString>
#include <QtCore/QStringList>

class QLabel;
class QListWidget;
class QPushButton;

QT_BEGIN_NAMESPACE
class QAudioOutput;
class QMediaPlayer;
class QVideoWidget;
QT_END_NAMESPACE

namespace osd::qtui {

//============================================================
//  A looping video player for a single video snap.
//============================================================
class VideoTab : public QWidget
{
public:
	explicit VideoTab(QWidget *parent = nullptr);

	// Set the file to play ("" clears and shows a placeholder message).  A
	// non-empty path plays looped when auto-play is enabled and the user has
	// not paused; otherwise it loads paused.  `message` overrides placeholder.
	void setFile(const QString &path, const QString &message = QString());
	void resume();  // tab became current: play iff auto-play is wanted
	void stop();    // pause automatically (does not change the user's intent)

private:
	void updatePlayButton();
	// True when playback should start on its own: the auto-play option is on
	// and the user has not paused.  Pausing is "sticky" across selections.
	bool wantPlay() const;

	QMediaPlayer *m_player = nullptr;
	QAudioOutput *m_audio = nullptr;
	QVideoWidget *m_video = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_playPause = nullptr;
	QPushButton *m_mute = nullptr;
	QString m_path;
	bool m_playWanted = true;   // cleared by a manual pause, set by manual play
};

//============================================================
//  A track list + audio player for a machine's soundtrack folder.
//============================================================
class SoundtrackTab : public QWidget
{
public:
	explicit SoundtrackTab(QWidget *parent = nullptr);

	// Populate the track list with the given file paths ("" message overrides
	// the empty-list placeholder).  Does not autoplay.
	void setTracks(const QStringList &files, const QString &message = QString());
	void play();    // resume the current track
	void stop();    // pause

private:
	void playRow(int row);
	void updatePlayButton();

	QMediaPlayer *m_player = nullptr;
	QAudioOutput *m_audio = nullptr;
	QListWidget *m_list = nullptr;
	QLabel *m_status = nullptr;
	QPushButton *m_playPause = nullptr;
	QPushButton *m_stopBtn = nullptr;
	QStringList m_files;
	int m_current = -1;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_MEDIATABS_H
