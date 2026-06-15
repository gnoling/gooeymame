// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  artloader.h - background image asset loader
//
//  Loads artwork bytes from EXTRAs folders/zips on a worker thread so that
//  opening a large archive never blocks the UI.  Only the most recent
//  request matters; results are tagged with an epoch + tab so the caller can
//  discard stale ones.
//
//============================================================
#ifndef MAME_OSD_QTUI_ARTLOADER_H
#define MAME_OSD_QTUI_ARTLOADER_H

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace osd::qtui { using ArtCandidates = QVector<QPair<QString, QString>>; }

namespace osd::qtui {

class ArtLoader : public QObject
{
	Q_OBJECT

public:
	explicit ArtLoader(QObject *parent = nullptr);
	~ArtLoader() override;

	// Queue a load: try each (path, entry) candidate in order; the first hit
	// wins.  Replaces any pending request.
	void request(quint64 epoch, int tab, const ArtCandidates &candidates);

signals:
	// Emitted on the loader's thread affinity (the UI thread); bytes is empty
	// if nothing was found.
	void loaded(quint64 epoch, int tab, const QByteArray &bytes);

private:
	void run();

	std::thread m_thread;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	bool m_stop = false;
	bool m_hasRequest = false;

	quint64 m_epoch = 0;
	int m_tab = 0;
	ArtCandidates m_candidates;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_ARTLOADER_H
