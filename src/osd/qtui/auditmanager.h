// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  auditmanager.h - background ROM availability auditing
//
//  Runs qtui_audit_all() on a worker thread, streams results into the
//  GameListModel, and persists them to a cache so subsequent launches are
//  instant.
//
//============================================================
#ifndef MAME_OSD_QTUI_AUDITMANAGER_H
#define MAME_OSD_QTUI_AUDITMANAGER_H

#pragma once

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

class QTimer;

namespace osd::qtui {

class GameListModel;

class AuditManager : public QObject
{
	Q_OBJECT

public:
	explicit AuditManager(GameListModel *model, QObject *parent = nullptr);
	~AuditManager() override;

	// Load cached results (if any) and push them to the model.  Returns true
	// if a cache existed and was applied.
	bool loadCache();

	// Begin (or restart) a background full audit.
	void startAudit();

	// Request that an in-progress audit stop as soon as possible.
	void cancelAudit();

	bool isRunning() const { return m_running.load(); }

signals:
	void progress(int audited, int total);
	void finished();

private slots:
	void flush();

private:
	void joinWorker();
	QString cacheFilePath() const;
	void saveCache() const;

	GameListModel *m_model;
	QHash<QString, int> m_results;

	std::mutex m_mutex;
	std::vector<std::pair<std::string, int>> m_pending;
	std::atomic<bool> m_cancel{ false };
	std::atomic<bool> m_running{ false };
	std::thread m_thread;
	QTimer *m_flushTimer = nullptr;
	int m_audited = 0;
	int m_total = 0;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_AUDITMANAGER_H
