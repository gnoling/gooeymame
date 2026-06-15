// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  artloader.cpp - background image asset loader
//
//============================================================

#include "artloader.h"

#include "emulator.h"

#include <vector>


namespace osd::qtui {

ArtLoader::ArtLoader(QObject *parent) :
	QObject(parent)
{
	m_thread = std::thread(&ArtLoader::run, this);
}

ArtLoader::~ArtLoader()
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

void ArtLoader::request(quint64 epoch, int tab, const ArtCandidates &candidates)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_epoch = epoch;
		m_tab = tab;
		m_candidates = candidates;
		m_hasRequest = true;
	}
	m_cv.notify_one();
}

void ArtLoader::run()
{
	for (;;)
	{
		quint64 epoch;
		int tab;
		ArtCandidates candidates;

		{
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cv.wait(lk, [this] { return m_hasRequest || m_stop; });
			if (m_stop)
				return;
			epoch = m_epoch;
			tab = m_tab;
			candidates = m_candidates;
			m_hasRequest = false;
		}

		QByteArray result;
		for (const auto &candidate : candidates)
		{
			std::vector<std::uint8_t> const bytes =
					qtui_load_asset(candidate.first.toStdString(), candidate.second.toStdString());
			if (!bytes.empty())
			{
				result = QByteArray(reinterpret_cast<const char *>(bytes.data()), int(bytes.size()));
				break;
			}
		}

		// Auto-queued to the UI thread (ArtLoader lives there).
		emit loaded(epoch, tab, result);
	}
}

} // namespace osd::qtui
