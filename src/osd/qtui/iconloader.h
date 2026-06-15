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

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace osd::qtui {

class IconLoader : public QObject
{
	Q_OBJECT

public:
	explicit IconLoader(QObject *parent = nullptr);
	~IconLoader() override;

	// Queue an icon load for a model row: try each entry in `entries` within
	// `path`, first hit wins.
	void request(int row, const QString &path, const QStringList &entries);

signals:
	void loaded(int row, const QByteArray &bytes);

private:
	void run();

	struct Request
	{
		int row;
		QString path;
		QStringList entries;
	};

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	std::deque<Request> m_queue;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_ICONLOADER_H
