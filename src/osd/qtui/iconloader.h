// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  iconloader.h - background loader for game-list row icons
//
//  Loads per-system icons (icons.zip / folder) on a worker thread, queueing
//  requests so scrolling the list never blocks the UI.
//
//============================================================
#ifndef MAME_OSD_QTUI_ICONLOADER_H
#define MAME_OSD_QTUI_ICONLOADER_H

#pragma once

#include "artloader.h"   // ArtCandidates

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class QIcon;

namespace osd::qtui {

// Decode raw image bytes into a row icon scaled to `displaySize` px square.
// When enlarging, `smoothUpscale` chooses smooth vs nearest-neighbour (the
// latter keeps pixel art crisp); shrinking always uses smooth scaling.  A
// displaySize <= 0 leaves the icon at its native size.  Returns a null QIcon
// for empty/undecodable bytes.
QIcon makeRowIcon(const QByteArray &bytes, int displaySize, bool smoothUpscale);

class IconLoader : public QObject
{
	Q_OBJECT

public:
	explicit IconLoader(QObject *parent = nullptr);
	~IconLoader() override;

	// Queue an icon load for a model row: try each (folder, entry) candidate in
	// order, first hit wins.  Candidates may mix the icons set (.ico) with any
	// art folder (.png) so the row icon can fall back across art types.
	void request(int row, const ArtCandidates &candidates);

signals:
	void loaded(int row, const QByteArray &bytes);

private:
	void run();

	struct Request
	{
		int row;
		ArtCandidates candidates;
	};

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	std::deque<Request> m_queue;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_ICONLOADER_H
