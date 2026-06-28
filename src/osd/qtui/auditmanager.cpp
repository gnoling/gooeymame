// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  auditmanager.cpp - background ROM availability auditing
//
//============================================================

#include "auditmanager.h"

#include "emulator.h"
#include "gamelistmodel.h"
#include "threadutil.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>


namespace osd::qtui {

namespace {

// How often the UI thread drains worker results into the model.
constexpr int FLUSH_INTERVAL_MS = 800;

} // anonymous namespace

AuditManager::AuditManager(GameListModel *model, QObject *parent) :
	QObject(parent),
	m_model(model)
{
	m_flushTimer = new QTimer(this);
	m_flushTimer->setInterval(FLUSH_INTERVAL_MS);
	connect(m_flushTimer, &QTimer::timeout, this, &AuditManager::flush);
}

AuditManager::~AuditManager()
{
	m_cancel.store(true);
	joinWorker();
}

void AuditManager::joinWorker()
{
	if (m_thread.joinable())
		m_thread.join();
}

QString AuditManager::cacheFilePath() const
{
	QString const dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	QDir().mkpath(dir);
	return dir + QStringLiteral("/availability.cache");
}

bool AuditManager::loadCache()
{
	QFile file(cacheFilePath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return false;

	QVector<QPair<QString, int>> batch;
	QTextStream in(&file);
	while (!in.atEnd())
	{
		QString const line = in.readLine();
		int const sep = line.lastIndexOf(QLatin1Char(' '));
		if (sep <= 0)
			continue;
		QString const name = line.left(sep);
		int const status = line.mid(sep + 1).toInt();
		m_results.insert(name, status);
		batch.append({ name, status });
	}

	if (!batch.isEmpty())
		m_model->applyAvailabilityBatch(batch);
	return !batch.isEmpty();
}

void AuditManager::saveCache() const
{
	QFile file(cacheFilePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
		return;

	QTextStream out(&file);
	for (auto it = m_results.constBegin(); it != m_results.constEnd(); ++it)
		out << it.key() << QLatin1Char(' ') << it.value() << QLatin1Char('\n');
}

void AuditManager::cancelAudit()
{
	m_cancel.store(true);
}

void AuditManager::startAudit()
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
		qtui_audit_all(
				[this] (const std::string &name, int status) {
					std::lock_guard<std::mutex> lk(m_mutex);
					m_pending.emplace_back(name, status);
				},
				m_cancel);
		m_running.store(false);
	});
}

void AuditManager::flush()
{
	std::vector<std::pair<std::string, int>> batch;
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		batch.swap(m_pending);
	}

	if (!batch.empty())
	{
		QVector<QPair<QString, int>> applied;
		applied.reserve(int(batch.size()));
		for (const auto &entry : batch)
		{
			// Every system counts toward progress, but only definite results
			// (available/unavailable) are cached and displayed.
			if (entry.second != 0 /* QTUI_AVAIL_UNKNOWN */)
			{
				QString const name = QString::fromStdString(entry.first);
				m_results.insert(name, entry.second);
				applied.append({ name, entry.second });
			}
		}
		m_audited += int(batch.size());
		if (!applied.isEmpty())
			m_model->applyAvailabilityBatch(applied);
		emit progress(m_audited, m_total);
	}

	// Worker finished: drain is complete once running is false and we have
	// applied the final batch above.
	if (!m_running.load())
	{
		joinWorker();
		m_flushTimer->stop();
		saveCache();
		emit finished();
	}
}

} // namespace osd::qtui
