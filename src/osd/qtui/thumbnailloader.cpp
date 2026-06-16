// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  thumbnailloader.cpp - background loader for grid-view thumbnails
//
//============================================================

#include "thumbnailloader.h"

#include "emulator.h"

#include <vector>


namespace osd::qtui {

ThumbnailLoader::ThumbnailLoader(QObject *parent) :
	QObject(parent)
{
	m_thread = std::thread(&ThumbnailLoader::run, this);
}

ThumbnailLoader::~ThumbnailLoader()
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

void ThumbnailLoader::request(int row, quint64 generation, const ArtCandidates &candidates)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_queue.push_back({ row, generation, candidates });
		// Bound the backlog: during fast scrolling, drop the oldest (now
		// off-screen) requests so the worker stays focused on recent rows.
		while (m_queue.size() > 256)
			m_queue.pop_front();
	}
	m_cv.notify_one();
}

void ThumbnailLoader::run()
{
	for (;;)
	{
		Request req;
		{
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cv.wait(lk, [this] { return !m_queue.empty() || m_stop; });
			if (m_stop)
				return;
			// Process the most recent request first: rows just scrolled into
			// view get their thumbnails before older, off-screen ones.
			req = m_queue.back();
			m_queue.pop_back();
		}

		QByteArray result;
		for (const QPair<QString, QString> &candidate : req.candidates)
		{
			std::vector<std::uint8_t> const bytes =
					qtui_load_asset(candidate.first.toStdString(), candidate.second.toStdString());
			if (!bytes.empty())
			{
				result = QByteArray(reinterpret_cast<const char *>(bytes.data()), int(bytes.size()));
				break;
			}
		}

		emit loaded(req.row, req.generation, result);   // auto-queued to the UI thread
	}
}

} // namespace osd::qtui
