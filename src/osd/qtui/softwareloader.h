// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareloader.h - background software-list enumeration
//
//  Runs qtui_enumerate_software() (which now also audits each entry's ROMs)
//  on a worker thread so selecting a system never freezes the UI, and so a
//  new selection can cancel an in-flight load.
//
//============================================================
#ifndef MAME_OSD_QTUI_SOFTWARELOADER_H
#define MAME_OSD_QTUI_SOFTWARELOADER_H

#pragma once

#include "emulator.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <atomic>
#include <thread>
#include <vector>

namespace osd::qtui {

class SoftwareLoader : public QObject
{
	Q_OBJECT

public:
	explicit SoftwareLoader(QObject *parent = nullptr);
	~SoftwareLoader() override;

	// Cancel any in-flight load and start enumerating the given system.
	void load(const QString &system);

	// Cancel any in-flight load without starting a new one.
	void cancel();

signals:
	// Phase 1: the parsed entries (availability UNKNOWN), delivered quickly.
	void loaded(const std::vector<qtui_software_entry> &entries);
	// Phase 2: ROM availability for those entries (index-aligned), delivered
	// after the background audit completes.
	void availabilityReady(const QVector<int> &availability);

private:
	void stopWorker();

	std::thread m_thread;
	std::atomic<bool> m_cancel{ false };
	std::atomic<unsigned> m_epoch{ 0 };
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_SOFTWARELOADER_H
