// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  infoloader.cpp - background lookups into the EXTRAs text databases
//
//============================================================

#include "infoloader.h"

#include "frontendpaths.h"

#include <QtCore/QFile>


namespace osd::qtui {

namespace {

// Parse format of each text database.
enum Fmt { FmtXml, FmtDat, FmtScores };

// Maps each Source to its frontendpaths key and parse format.
struct SourceDef { const char *key; int fmt; };
const SourceDef kSources[InfoLoader::SourceCount] =
{
	{ "history",   FmtXml    },   // History (history.xml)
	{ "mameinfo",  FmtDat    },   // MameInfo
	{ "messinfo",  FmtDat    },   // MessInfo
	{ "command",   FmtDat    },   // Command
	{ "gameinit",  FmtDat    },   // GameInit
	{ "sysinfo",   FmtDat    },   // SysInfo
	{ "story",     FmtDat    },   // Story
	{ "topscores", FmtScores },   // TopScores (scores3.htm, MARP)
};

// Escape text for display inside an HTML <pre> block.
QString htmlEscape(const QString &in)
{
	QString out = in;
	out.replace(QLatin1Char('&'), QLatin1String("&amp;"));   // first
	out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
	out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
	return out;
}

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

// Parse history.xml: map each <system name> / <item list/name> to its <text>.
void buildXmlIndex(const QByteArray &data, QHash<QString, QPair<int, int>> &index)
{
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
					index.insert(QString::fromLatin1(name), range);
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
					index.insert(QString::fromLatin1(list) + QLatin1Char('/') + QString::fromLatin1(name), range);
				ip = itemEnd + 1;
			}
		}

		pos = entryEnd + 8;   // len("</entry>")
	}
}

// Parse a "$info=names\n$tag\n...body...\n$end" dat file, mapping each
// comma-separated short name to the body range.
void buildDatIndex(const QByteArray &data, QHash<QString, QPair<int, int>> &index)
{
	int pos = 0;
	for (;;)
	{
		int const info = data.indexOf("$info=", pos);
		if (info < 0)
			break;
		int const nameStart = info + 6;   // len("$info=")
		int const nameEnd = data.indexOf('\n', nameStart);
		if (nameEnd < 0)
			break;

		// The next line beginning with '$' is the section tag ($mame/$cmd/...);
		// the body starts after it and runs to the "$end" line.
		int const tag = data.indexOf('$', nameEnd);
		if (tag < 0)
			break;
		int const bodyStart = data.indexOf('\n', tag);
		if (bodyStart < 0)
			break;
		int const end = data.indexOf("$end", bodyStart);
		if (end < 0)
			break;

		QByteArray const names = data.mid(nameStart, nameEnd - nameStart);
		QPair<int, int> const range(bodyStart + 1, end - bodyStart - 1);
		for (const QByteArray &raw : names.split(','))
		{
			QByteArray const name = raw.trimmed();
			if (!name.isEmpty())
				index.insert(QString::fromLatin1(name), range);
		}

		pos = end + 4;   // len("$end")
	}
}

// Parse the MARP scores3.htm leaderboard.  Each game's block begins with a
// line whose pre-colon field is a non-empty, space-free short name; following
// lines for the same game have a blank pre-colon field.  Maps short name ->
// the block's byte range (header line through the line before the next game).
void buildScoresIndex(const QByteArray &data, QHash<QString, QPair<int, int>> &index)
{
	int pos = 0;
	int blockStart = -1;
	QByteArray key;
	while (pos < data.size())
	{
		int lineEnd = data.indexOf('\n', pos);
		if (lineEnd < 0)
			lineEnd = data.size();

		int const colon = data.indexOf(':', pos);
		if (colon >= 0 && colon < lineEnd)
		{
			QByteArray const left = data.mid(pos, colon - pos).trimmed();
			if (!left.isEmpty() && !left.contains(' '))
			{
				// New game header: close the previous block.
				if (blockStart >= 0 && !key.isEmpty())
					index.insert(QString::fromLatin1(key), { blockStart, pos - blockStart });
				key = left;
				blockStart = pos;
			}
		}

		pos = lineEnd + 1;
	}
	if (blockStart >= 0 && !key.isEmpty())
		index.insert(QString::fromLatin1(key), { blockStart, data.size() - blockStart });
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

void InfoLoader::request(quint64 epoch, int source, const QString &key)
{
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		m_epoch = epoch;
		m_source = source;
		m_key = key;
		m_hasRequest = true;
	}
	m_cv.notify_one();
}

void InfoLoader::buildIndex(int source)
{
	Db &db = m_db[source];
	db.indexed = true;   // attempt once regardless of outcome

	QString const path = frontendFolderPath(QString::fromLatin1(kSources[source].key));
	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;
	db.data = file.readAll();
	file.close();

	switch (kSources[source].fmt)
	{
	case FmtXml:    buildXmlIndex(db.data, db.index);    break;
	case FmtScores: buildScoresIndex(db.data, db.index); break;
	default:        buildDatIndex(db.data, db.index);    break;
	}
}

void InfoLoader::run()
{
	for (;;)
	{
		quint64 epoch;
		int source;
		QString key;
		{
			std::unique_lock<std::mutex> lk(m_mutex);
			m_cv.wait(lk, [this] { return m_hasRequest || m_stop; });
			if (m_stop)
				return;
			epoch = m_epoch;
			source = m_source;
			key = m_key;
			m_hasRequest = false;
		}

		if (source < 0 || source >= SourceCount)
			continue;

		Db &db = m_db[source];
		if (!db.indexed)
			buildIndex(source);

		QString text;
		auto it = db.index.constFind(key);
		if (it != db.index.constEnd())
		{
			QByteArray const raw = db.data.mid(it.value().first, it.value().second);
			QString const decoded = QString::fromUtf8(raw);
			switch (kSources[source].fmt)
			{
			case FmtXml:
				text = unescape(decoded).trimmed();
				break;
			case FmtScores:
				// Preserve the column alignment with a monospaced <pre> block.
				text = QStringLiteral("<pre>") + htmlEscape(decoded.trimmed()) + QStringLiteral("</pre>");
				break;
			default:
				text = decoded.trimmed();
				break;
			}
		}

		emit loaded(epoch, source, text);
	}
}

} // namespace osd::qtui
