// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  infoloader.h - background lookups into the EXTRAs text databases
//
//  Indexes the (large) text databases once each, on a worker thread:
//   - history.xml  (XML; system + software keyed)
//   - mameinfo.dat / messinfo.dat / command.dat  ($info= ... $end; machine
//     short-name keyed)
//  then answers lookups without blocking the UI.
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
	// Text databases the loader knows about (index into the source table).
	enum Source { History = 0, MameInfo, MessInfo, Command, GameInit, SysInfo, Story, TopScores, SourceCount };

	explicit InfoLoader(QObject *parent = nullptr);
	~InfoLoader() override;

	// Look up text for a key in the given source.  For History the key is a
	// system short name ("pacman") or a software key ("nes/smb"); for the dat
	// sources it is a machine short name.  Replaces any pending request.
	void request(quint64 epoch, int source, const QString &key);

signals:
	void loaded(quint64 epoch, int source, const QString &text);

private:
	void run();
	void buildIndex(int source);   // worker thread

	struct Db
	{
		bool indexed = false;
		QByteArray data;                          // raw file contents
		QHash<QString, QPair<int, int>> index;    // key -> (offset, length)
	};

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	bool m_hasRequest = false;
	quint64 m_epoch = 0;
	int m_source = History;
	QString m_key;

	// Worker-thread-only state, one entry per Source.
	Db m_db[SourceCount];
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_INFOLOADER_H
