// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  infoloader.cpp - background lookups into history.xml
//
//============================================================

#include "infoloader.h"

#include "frontendpaths.h"

#include <QtCore/QFile>


namespace osd::qtui {

namespace {

// Unescape the XML entities that appear in history.xml <text> bodies.
QString unescape(const QString &in)
{
	QString out = in;
	out.replace(QLatin1String("&lt;"), QLatin1String("<"));
	out.replace(QLatin1String("&gt;"), QLatin1String(">"));
	out.replace(QLatin1String("&quot;"), QLatin1String("\""));
	out.replace(QLatin1String("&apos;"), QLatin1String("'"));
	out.replace(QLatin1String("&amp;"), QLatin1String("&"));   // last
	return out;
}

// Read a double-quoted attribute value following `attr` within [from, to).
QByteArray readAttr(const QByteArray &data, const char *attr, int from, int to)
{
	int const key = data.indexOf(attr, from);
	if (key < 0 || key >= to)
		return QByteArray();
	int const open = data.indexOf('"', key);
	if (open < 0 || open >= to)
		return QByteArray();
	int const close = data.indexOf('"', open + 1);
	if (close < 0 || close >= to)
		return QByteArray();
	return data.mid(open + 1, close - open - 1);
}

} // anonymous namespace

InfoLoader::InfoLoader(QObject *parent) :
	QObject(parent)
{
	m_thread = std::thread(&InfoLoader::run, this);
}

InfoLoader::~InfoLoader()
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_thread.joinable())
		m_thread.join();
}

void InfoLoader::request(quint64 epoch, const QString &key)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_epoch = epoch;
		m_key = key;
		m_hasRequest = true;
	}
	m_cv.notify_one();
}

void InfoLoader::buildIndex()
{
	m_indexed = true;   // attempt once regardless of outcome

	QString const path = frontendFolderPath(QStringLiteral("history"));
	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;
	m_data = file.readAll();
	file.close();

	const QByteArray &data = m_data;
	int pos = 0;
	for (;;)
	{
		int const entry = data.indexOf("<entry>", pos);
		if (entry < 0)
			break;
		int const entryEnd = data.indexOf("</entry>", entry);
		if (entryEnd < 0)
			break;

		int const textOpen = data.indexOf("<text>", entry);
		int textStart = -1, textLen = 0;
		if (textOpen >= 0 && textOpen < entryEnd)
		{
			int const textClose = data.indexOf("</text>", textOpen);
			if (textClose >= 0 && textClose <= entryEnd)
			{
				textStart = textOpen + 6;   // len("<text>")
				textLen = textClose - textStart;
			}
		}

		if (textStart >= 0)
		{
			QPair<int, int> const range(textStart, textLen);
			int const idLimit = textOpen;   // identifiers precede the text

			// System entries: <system name="...">
			int sp = entry;
			for (;;)
			{
				int const sys = data.indexOf("<system ", sp);
				if (sys < 0 || sys >= idLimit)
					break;
				QByteArray const name = readAttr(data, "name=", sys, idLimit);
				if (!name.isEmpty())
					m_index.insert(QString::fromLatin1(name), range);
				sp = sys + 8;
			}

			// Software entries: <item list="..." name="...">
			int ip = entry;
			for (;;)
			{
				int const item = data.indexOf("<item ", ip);
				if (item < 0 || item >= idLimit)
					break;
				int const itemEnd = data.indexOf('>', item);
				QByteArray const list = readAttr(data, "list=", item, itemEnd);
				QByteArray const name = readAttr(data, "name=", item, itemEnd);
				if (!list.isEmpty() && !name.isEmpty())
					m_index.insert(QString::fromLatin1(list) + QLatin1Char('/') + QString::fromLatin1(name), range);
				ip = itemEnd + 1;
			}
		}

		pos = entryEnd + 8;   // len("</entry>")
	}
}

void InfoLoader::run()
{
	for (;;)
	{
		quint64 epoch;
		QString key;
		{
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cv.wait(lk, [this] { return m_hasRequest || m_stop; });
			if (m_stop)
				return;
			epoch = m_epoch;
			key = m_key;
			m_hasRequest = false;
		}

		if (!m_indexed)
			buildIndex();

		QString text;
		auto it = m_index.constFind(key);
		if (it != m_index.constEnd())
		{
			QByteArray const raw = m_data.mid(it.value().first, it.value().second);
			text = unescape(QString::fromUtf8(raw)).trimmed();
		}

		emit loaded(epoch, text);
	}
}

} // namespace osd::qtui
