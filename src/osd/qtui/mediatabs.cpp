// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  mediatabs.cpp - video and soundtrack tabs for the artwork panel
//
//============================================================

#include "mediatabs.h"

#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QUrl>
#include <QtMultimedia/QAudioOutput>
#include <QtMultimedia/QMediaPlayer>
#include <QtMultimediaWidgets/QVideoWidget>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QStackedLayout>
#include <QtWidgets/QVBoxLayout>


namespace osd::qtui {

//============================================================
//  VideoTab
//============================================================

VideoTab::VideoTab(QWidget *parent) :
	QWidget(parent)
{
	m_player = new QMediaPlayer(this);
	m_audio = new QAudioOutput(this);
	m_audio->setVolume(0.8f);
	m_player->setAudioOutput(m_audio);
	m_player->setLoops(QMediaPlayer::Infinite);

	m_video = new QVideoWidget(this);
	m_player->setVideoOutput(m_video);

	m_status = new QLabel(tr("No video"), this);
	m_status->setAlignment(Qt::AlignCenter);

	// The video surface and the placeholder share the same space.
	QWidget *stack = new QWidget(this);
	QStackedLayout *stackLayout = new QStackedLayout(stack);
	stackLayout->setContentsMargins(0, 0, 0, 0);
	stackLayout->setStackingMode(QStackedLayout::StackAll);
	stackLayout->addWidget(m_status);
	stackLayout->addWidget(m_video);

	m_playPause = new QPushButton(tr("Play"), this);
	m_playPause->setEnabled(false);
	connect(m_playPause, &QPushButton::clicked, this, [this] {
		// A manual pause/play sets the sticky intent that survives selection
		// changes, so pausing one video keeps all videos paused until resumed.
		if (m_player->playbackState() == QMediaPlayer::PlayingState)
		{
			m_player->pause();
			m_playWanted = false;
		}
		else
		{
			m_player->play();
			m_playWanted = true;
		}
	});

	m_mute = new QPushButton(tr("Mute"), this);
	m_mute->setCheckable(true);
	connect(m_mute, &QPushButton::toggled, this, [this] (bool on) {
		m_audio->setMuted(on);
	});

	connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] { updatePlayButton(); });

	QHBoxLayout *controls = new QHBoxLayout;
	controls->setContentsMargins(4, 2, 4, 2);
	controls->addWidget(m_playPause);
	controls->addWidget(m_mute);
	controls->addStretch();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(stack, 1);
	layout->addLayout(controls);

	m_video->hide();
}

void VideoTab::setFile(const QString &path, const QString &message)
{
	m_path = path;
	if (path.isEmpty())
	{
		m_player->stop();
		m_player->setSource(QUrl());
		m_video->hide();
		m_status->setText(message.isEmpty() ? tr("No video") : message);
		m_status->show();
		m_playPause->setEnabled(false);
		updatePlayButton();
		return;
	}

	m_status->hide();
	m_video->show();
	m_player->setSource(QUrl::fromLocalFile(path));
	m_playPause->setEnabled(true);
	if (wantPlay())
		m_player->play();
	else
		m_player->pause();
	updatePlayButton();
}

void VideoTab::resume()
{
	if (!m_path.isEmpty() && wantPlay())
		m_player->play();
}

void VideoTab::stop()
{
	m_player->pause();
}

bool VideoTab::wantPlay() const
{
	return QSettings().value(QStringLiteral("artwork/videoAutoplay"), true).toBool() && m_playWanted;
}

void VideoTab::updatePlayButton()
{
	bool const playing = m_player->playbackState() == QMediaPlayer::PlayingState;
	m_playPause->setText(playing ? tr("Pause") : tr("Play"));
}

//============================================================
//  SoundtrackTab
//============================================================

SoundtrackTab::SoundtrackTab(QWidget *parent) :
	QWidget(parent)
{
	m_player = new QMediaPlayer(this);
	m_audio = new QAudioOutput(this);
	m_audio->setVolume(0.8f);
	m_player->setAudioOutput(m_audio);

	m_list = new QListWidget(this);
	connect(m_list, &QListWidget::itemActivated, this, [this] (QListWidgetItem *) {
		playRow(m_list->currentRow());
	});
	connect(m_list, &QListWidget::itemDoubleClicked, this, [this] (QListWidgetItem *) {
		playRow(m_list->currentRow());
	});

	m_status = new QLabel(tr("No soundtrack"), this);
	m_status->setAlignment(Qt::AlignCenter);
	m_status->hide();

	m_playPause = new QPushButton(tr("Play"), this);
	connect(m_playPause, &QPushButton::clicked, this, [this] {
		if (m_player->playbackState() == QMediaPlayer::PlayingState)
		{
			m_player->pause();
		}
		else if (m_current >= 0)
		{
			m_player->play();
		}
		else if (!m_files.isEmpty())
		{
			playRow(m_list->currentRow() >= 0 ? m_list->currentRow() : 0);
		}
	});

	m_stopBtn = new QPushButton(tr("Stop"), this);
	connect(m_stopBtn, &QPushButton::clicked, this, [this] { m_player->stop(); });

	connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] { updatePlayButton(); });

	// Auto-advance to the next track when one finishes.
	connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this] (QMediaPlayer::MediaStatus status) {
		if (status == QMediaPlayer::EndOfMedia && m_current + 1 < m_files.size())
			playRow(m_current + 1);
	});

	QHBoxLayout *controls = new QHBoxLayout;
	controls->setContentsMargins(4, 2, 4, 2);
	controls->addWidget(m_playPause);
	controls->addWidget(m_stopBtn);
	controls->addStretch();

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(m_status);
	layout->addWidget(m_list, 1);
	layout->addLayout(controls);
}

void SoundtrackTab::setTracks(const QStringList &files, const QString &message)
{
	m_player->stop();
	m_player->setSource(QUrl());
	m_current = -1;
	m_files = files;
	m_list->clear();

	if (files.isEmpty())
	{
		m_status->setText(message.isEmpty() ? tr("No soundtrack") : message);
		m_status->show();
		m_list->hide();
		m_playPause->setEnabled(false);
		m_stopBtn->setEnabled(false);
		return;
	}

	m_status->hide();
	m_list->show();
	m_playPause->setEnabled(true);
	m_stopBtn->setEnabled(true);
	for (const QString &file : files)
		m_list->addItem(QFileInfo(file).completeBaseName());
	m_list->setCurrentRow(0);
	updatePlayButton();
}

void SoundtrackTab::play()
{
	if (m_current >= 0)
		m_player->play();
}

void SoundtrackTab::stop()
{
	m_player->pause();
}

void SoundtrackTab::playRow(int row)
{
	if (row < 0 || row >= m_files.size())
		return;
	m_current = row;
	m_list->setCurrentRow(row);
	m_player->setSource(QUrl::fromLocalFile(m_files[row]));
	m_player->play();
	updatePlayButton();
}

void SoundtrackTab::updatePlayButton()
{
	bool const playing = m_player->playbackState() == QMediaPlayer::PlayingState;
	m_playPause->setText(playing ? tr("Pause") : tr("Play"));
}

} // namespace osd::qtui
