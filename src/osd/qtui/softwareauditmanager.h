// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareauditmanager.h - background bulk software-availability audit
//
//  Runs qtui_audit_all_software() on a worker thread to populate the
//  per-system software availability cache all at once (the lazy per-selection
//  caching still applies otherwise).  Mirrors AuditManager's structure:
//  results stream in via a flush timer, with progress + cancel.
//
//============================================================
#ifndef MAME_OSD_QTUI_SOFTWAREAUDITMANAGER_H
#define MAME_OSD_QTUI_SOFTWAREAUDITMANAGER_H

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

class QTimer;

namespace osd::qtui {

class SoftwareAuditManager : public QObject
{
	Q_OBJECT

public:
	explicit SoftwareAuditManager(QObject *parent = nullptr);
	~SoftwareAuditManager() override;

	void startAudit();
	void cancelAudit();
	bool isRunning() const { return m_running.load(); }

signals:
	void progress(int audited, int total);
	// One system's software availability (empty vector = no software).
	void systemAudited(const QString &system, const QVector<int> &availability);
	void finished();

private slots:
	void flush();

private:
	void joinWorker();

	std::thread m_thread;
	std::atomic<bool> m_cancel{ false };
	std::atomic<bool> m_running{ false };
	QTimer *m_flushTimer = nullptr;

	std::mutex m_mutex;
	std::vector<std::pair<QString, QVector<int>>> m_pending;   // system -> availability
	int m_audited = 0;
	int m_total = 0;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_SOFTWAREAUDITMANAGER_H
