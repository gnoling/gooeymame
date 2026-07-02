// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  iconloader.cpp - background loader for game-list row icons
//
//============================================================

#include "iconloader.h"

#include "emulator.h"

#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>

#include <vector>


namespace osd::qtui {

QIcon makeRowIcon(const QByteArray &bytes, int displaySize, bool smoothUpscale)
{
	if (bytes.isEmpty())
		return QIcon();

	QPixmap pixmap;
	if (!pixmap.loadFromData(bytes))
		return QIcon();

	if (displaySize <= 0)
		return QIcon(pixmap);   // native size (no uniform slot)

	// Scale to fit within a displaySize square, keeping the art's aspect ratio.
	QPixmap scaled;
	if (pixmap.width() == displaySize && pixmap.height() == displaySize)
	{
		scaled = pixmap;
	}
	else
	{
		// Enlarging: honour the user's choice (nearest-neighbour keeps pixel art
		// crisp; smooth blends it).  Shrinking always uses smooth scaling to avoid
		// dropped pixels.
		bool const enlarging = (displaySize > pixmap.width() || displaySize > pixmap.height());
		Qt::TransformationMode const mode =
				(enlarging && !smoothUpscale) ? Qt::FastTransformation : Qt::SmoothTransformation;
		scaled = pixmap.scaled(displaySize, displaySize, Qt::KeepAspectRatio, mode);
	}

	// A square icon already fills its slot; return it directly.
	if (scaled.width() == displaySize && scaled.height() == displaySize)
		return QIcon(scaled);

	// Otherwise centre it on a transparent displaySize square so every row icon
	// occupies the same footprint regardless of the art's aspect ratio — this
	// keeps the row text left-aligned (portrait/landscape box art would otherwise
	// reserve a variable-width decoration and shift the text).
	QPixmap canvas(displaySize, displaySize);
	canvas.fill(Qt::transparent);
	QPainter painter(&canvas);
	painter.drawPixmap((displaySize - scaled.width()) / 2, (displaySize - scaled.height()) / 2, scaled);
	painter.end();
	return QIcon(canvas);
}

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

void IconLoader::request(int row, const ArtCandidates &candidates)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_queue.push_back({ row, candidates });
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
		for (const auto &cand : req.candidates)
		{
			if (cand.first.isEmpty() || cand.second.isEmpty())
				continue;
			std::vector<std::uint8_t> const bytes =
					qtui_load_asset(cand.first.toStdString(), cand.second.toStdString());
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
