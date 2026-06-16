// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  thumbnailloader.h - background loader for grid-view thumbnails
//
//  Like IconLoader but takes an ordered list of (path, entry) candidates (so
//  software thumbnails can fall back to the host machine's image) and tags
//  each result with a generation, so results from a previous thumbnail source
//  can be discarded.  Newest requests are served first (LIFO) and the backlog
//  is bounded, keeping fast grid scrolling responsive.
//
//============================================================
#ifndef MAME_OSD_QTUI_THUMBNAILLOADER_H
#define MAME_OSD_QTUI_THUMBNAILLOADER_H

#pragma once

#include "artloader.h"   // ArtCandidates

#include <QtCore/QByteArray>
#include <QtCore/QObject>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace osd::qtui {

// Item-data role for a row's grid thumbnail (a QPixmap), shared by both list
// models and the grid delegate.  Kept well clear of each model's own roles.
constexpr int kThumbnailRole = Qt::UserRole + 50;

class ThumbnailLoader : public QObject
{
	Q_OBJECT

public:
	explicit ThumbnailLoader(QObject *parent = nullptr);
	~ThumbnailLoader() override;

	// Queue a thumbnail load for a model row: try each (path, entry) candidate
	// in order, first hit wins.  `generation` is echoed back so stale results
	// (from a previous source) can be dropped.
	void request(int row, quint64 generation, const ArtCandidates &candidates);

signals:
	void loaded(int row, quint64 generation, const QByteArray &bytes);

private:
	void run();

	struct Request
	{
		int row;
		quint64 generation;
		ArtCandidates candidates;
	};

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	std::deque<Request> m_queue;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_THUMBNAILLOADER_H
