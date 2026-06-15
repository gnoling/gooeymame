// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  infoloader.h - background lookups into history.xml
//
//  Indexes the (large) history.xml once on a worker thread, mapping each
//  system short name and "<list>/<software>" key to the byte range of its
//  <text> entry, then answers lookups without blocking the UI.
//
//============================================================
#ifndef MAME_OSD_QTUI_INFOLOADER_H
#define MAME_OSD_QTUI_INFOLOADER_H

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QString>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace osd::qtui {

class InfoLoader : public QObject
{
	Q_OBJECT

public:
	explicit InfoLoader(QObject *parent = nullptr);
	~InfoLoader() override;

	// Look up history text for a key: a system short name ("pacman") or a
	// software key ("nes/smb").  Replaces any pending request.
	void request(quint64 epoch, const QString &key);

signals:
	void loaded(quint64 epoch, const QString &text);

private:
	void run();
	void buildIndex();   // worker thread

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	bool m_hasRequest = false;
	quint64 m_epoch = 0;
	QString m_key;

	// Worker-thread-only state.
	bool m_indexed = false;
	QByteArray m_data;                                  // raw history.xml
	QHash<QString, QPair<int, int>> m_index;            // key -> (offset, length)
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_INFOLOADER_H
