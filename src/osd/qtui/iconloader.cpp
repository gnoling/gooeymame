// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  iconloader.cpp - background loader for game-list row icons
//
//============================================================

#include "iconloader.h"

#include "emulator.h"

#include <vector>


namespace osd::qtui {

IconLoader::IconLoader(QObject *parent) :
	QObject(parent)
{
	m_thread = std::thread(&IconLoader::run, this);
}

IconLoader::~IconLoader()
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

void IconLoader::request(int row, const QString &path, const QStringList &entries)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_queue.push_back({ row, path, entries });
		// Bound the backlog: during fast scrolling, drop the oldest (now
		// off-screen) requests so the worker stays focused on recent rows.
		while (m_queue.size() > 256)
			m_queue.pop_front();
	}
	m_cv.notify_one();
}

void IconLoader::run()
{
	for (;;)
	{
		Request req;
		{
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cv.wait(lk, [this] { return !m_queue.empty() || m_stop; });
			if (m_stop)
				return;
			// Process the most recent request first: the rows just scrolled
			// into view get their icons before older, off-screen ones.
			req = m_queue.back();
			m_queue.pop_back();
		}

		QByteArray result;
		std::string const pathStr = req.path.toStdString();
		for (const QString &entry : req.entries)
		{
			std::vector<std::uint8_t> const bytes = qtui_load_asset(pathStr, entry.toStdString());
			if (!bytes.empty())
			{
				result = QByteArray(reinterpret_cast<const char *>(bytes.data()), int(bytes.size()));
				break;
			}
		}

		emit loaded(req.row, result);   // auto-queued to the UI thread
	}
}

} // namespace osd::qtui
