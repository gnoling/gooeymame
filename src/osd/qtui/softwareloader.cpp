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
	if (m_cancel)
		m_cancel->store(true);
}

void SoftwareLoader::stopWorker()
{
	// Shutdown only: blocking joins are acceptable here.
	if (m_thread.joinable())
		m_thread.join();
	for (auto &worker : m_retiring)
	{
		if (worker.first.joinable())
			worker.first.join();
	}
	m_retiring.clear();
}

void SoftwareLoader::reapFinished()
{
	for (auto it = m_retiring.begin(); it != m_retiring.end(); )
	{
		if (it->second->load())   // the worker has finished, so the join is instant
		{
			if (it->first.joinable())
				it->first.join();
			it = m_retiring.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void SoftwareLoader::load(const QString &system)
{
	// Retire the previous load WITHOUT blocking the (GUI) caller.  A load in
	// flight may be parked waiting for the core mutex that a running audit
	// holds across its slow per-driver file IO; joining it here would freeze
	// the UI until the audit released that lock.  Instead signal the old worker
	// to cancel and set it aside — it exits on its own once it can take the
	// lock (qtui_load_software checks the flag right after) — then collect any
	// already-finished retirees.  The epoch guard discards any late result.
	if (m_thread.joinable())
	{
		if (m_cancel)
			m_cancel->store(true);
		m_retiring.emplace_back(std::move(m_thread), m_done);
	}
	reapFinished();

	auto cancel = std::make_shared<std::atomic<bool>>(false);
	auto done = std::make_shared<std::atomic<bool>>(false);
	m_cancel = cancel;
	m_done = done;

	unsigned const epoch = ++m_epoch;
	std::string const sys = system.toStdString();

	m_thread = std::thread([this, sys, epoch, cancel, done] {
		// Mark this worker finished on every exit path so reapFinished() can
		// collect it; capturing the shared flags keeps them alive for the run.
		struct DoneGuard {
			std::shared_ptr<std::atomic<bool>> flag;
			~DoneGuard() { flag->store(true); }
		} const doneGuard{ done };

		// Phase 2 results accumulate here, index-aligned with phase 1.
		auto availability = std::make_shared<QVector<int>>();

		qtui_load_software(
				sys, cancel.get(),
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

		if (cancel->load())
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
