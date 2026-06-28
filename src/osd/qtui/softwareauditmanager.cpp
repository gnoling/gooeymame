// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareauditmanager.cpp - background bulk software-availability audit
//
//============================================================

#include "softwareauditmanager.h"

#include "emulator.h"
#include "threadutil.h"

#include <QtCore/QTimer>


namespace osd::qtui {

namespace {

// How often the UI thread drains worker results.
constexpr int FLUSH_INTERVAL_MS = 500;

} // anonymous namespace

SoftwareAuditManager::SoftwareAuditManager(QObject *parent) :
	QObject(parent)
{
	m_flushTimer = new QTimer(this);
	m_flushTimer->setInterval(FLUSH_INTERVAL_MS);
	connect(m_flushTimer, &QTimer::timeout, this, &SoftwareAuditManager::flush);
}

SoftwareAuditManager::~SoftwareAuditManager()
{
	m_cancel.store(true);
	joinWorker();
}

void SoftwareAuditManager::joinWorker()
{
	if (m_thread.joinable())
		m_thread.join();
}

void SoftwareAuditManager::cancelAudit()
{
	m_cancel.store(true);
}

void SoftwareAuditManager::startAudit()
{
	if (m_running.load())
		return;

	m_cancel.store(false);
	m_running.store(true);
	m_audited = 0;
	m_total = qtui_system_count();
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_pending.clear();
	}

	m_flushTimer->start();

	m_thread = std::thread([this] {
		osd::qtui::lower_current_thread_priority();
		qtui_audit_all_software(
				[this] (const std::string &system, const std::vector<int> &availability, bool hasSoftware) {
					QVector<int> avail;
					if (hasSoftware)
					{
						avail.reserve(int(availability.size()));
						for (int a : availability)
							avail.append(a);
					}
					std::lock_guard<std::mutex> lk(m_mutex);
					m_pending.emplace_back(QString::fromStdString(system), std::move(avail));
				},
				m_cancel);
		m_running.store(false);
	});
}

void SoftwareAuditManager::flush()
{
	std::vector<std::pair<QString, QVector<int>>> batch;
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		batch.swap(m_pending);
	}

	if (!batch.empty())
	{
		for (auto &entry : batch)
		{
			if (!entry.second.isEmpty())
				emit systemAudited(entry.first, entry.second);
		}
		m_audited += int(batch.size());
		emit progress(m_audited, m_total);
	}

	if (!m_running.load())
	{
		joinWorker();
		m_flushTimer->stop();
		emit finished();
	}
}

} // namespace osd::qtui
