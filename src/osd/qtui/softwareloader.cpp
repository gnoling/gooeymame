// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareloader.cpp - background software-list enumeration
//
//============================================================

#include "softwareloader.h"

#include <QtCore/QMetaObject>

#include <memory>
#include <utility>


namespace osd::qtui {

SoftwareLoader::SoftwareLoader(QObject *parent) :
	QObject(parent)
{
}

SoftwareLoader::~SoftwareLoader()
{
	cancel();
	stopWorker();
}

void SoftwareLoader::cancel()
{
	m_cancel.store(true);
}

void SoftwareLoader::stopWorker()
{
	if (m_thread.joinable())
		m_thread.join();
}

void SoftwareLoader::load(const QString &system)
{
	// Stop the previous load before starting a new one.  Joining guarantees
	// the old worker has exited before we clear the cancel flag, so there is
	// no chance of the reset reviving it.
	m_cancel.store(true);
	stopWorker();
	m_cancel.store(false);

	unsigned const epoch = ++m_epoch;
	std::string const sys = system.toStdString();

	m_thread = std::thread([this, sys, epoch] {
		// Phase 2 results accumulate here, index-aligned with phase 1.
		auto availability = std::make_shared<QVector<int>>();

		qtui_load_software(
				sys, &m_cancel,
				// Phase 1: entries parsed - deliver immediately.
				[this, epoch, availability] (const std::vector<qtui_software_entry> &entries) {
					availability->fill(0, int(entries.size()));
					QMetaObject::invokeMethod(
							this,
							[this, epoch, entries] () {
								if (epoch == m_epoch.load())
									emit loaded(entries);
							},
							Qt::QueuedConnection);
				},
				// Phase 2: record each audited entry's availability.
				[availability] (int idx, int avail) {
					if (idx >= 0 && idx < availability->size())
						(*availability)[idx] = avail;
				});

		if (m_cancel.load())
			return;

		// Deliver the completed availability for the current load only.
		QMetaObject::invokeMethod(
				this,
				[this, epoch, availability] () {
					if (epoch == m_epoch.load())
						emit availabilityReady(*availability);
				},
				Qt::QueuedConnection);
	});
}

} // namespace osd::qtui
