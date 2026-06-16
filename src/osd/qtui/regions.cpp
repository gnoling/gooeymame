// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  regions.cpp - region detection for clone "version" preference
//
//============================================================

#include "regions.h"

#include <QtCore/QHash>
#include <QtCore/QLocale>


namespace osd::qtui {

namespace {

// Canonical regions, US-first by default.
const char *const kRegions[] = {
	"USA", "World", "Europe", "Japan", "Asia", "Korea", "China", "Taiwan",
	"Hong Kong", "Brazil", "UK", "Germany", "France", "Italy", "Spain",
	"Netherlands", "Sweden", "Australia", "Argentina"
};

// Lower-cased description token -> canonical region.  Built once.  Multi-word
// regions are keyed by their first word (descriptions are tokenised on
// non-letters, so "Hong Kong" arrives as "hong").
const QHash<QString, QString> &keywordMap()
{
	static const QHash<QString, QString> map = {
		{ "world", "World" },
		{ "usa", "USA" }, { "us", "USA" }, { "america", "USA" }, { "american", "USA" },
		{ "europe", "Europe" }, { "euro", "Europe" }, { "european", "Europe" },
		{ "japan", "Japan" }, { "japanese", "Japan" }, { "jpn", "Japan" },
		{ "asia", "Asia" }, { "asian", "Asia" },
		{ "korea", "Korea" }, { "korean", "Korea" },
		{ "china", "China" }, { "chinese", "China" },
		{ "taiwan", "Taiwan" },
		{ "hong", "Hong Kong" },
		{ "brazil", "Brazil" }, { "brazilian", "Brazil" },
		{ "uk", "UK" }, { "england", "UK" }, { "britain", "UK" }, { "british", "UK" },
		{ "germany", "Germany" }, { "german", "Germany" },
		{ "france", "France" }, { "french", "France" },
		{ "italy", "Italy" }, { "italian", "Italy" },
		{ "spain", "Spain" }, { "spanish", "Spain" },
		{ "netherlands", "Netherlands" }, { "dutch", "Netherlands" },
		{ "sweden", "Sweden" }, { "swedish", "Sweden" },
		{ "australia", "Australia" }, { "australian", "Australia" },
		{ "argentina", "Argentina" }
	};
	return map;
}

} // anonymous namespace

QStringList defaultRegionOrder()
{
	QStringList list;
	for (const char *region : kRegions)
		list << QString::fromLatin1(region);
	return list;
}

QString extractRegion(const QString &description)
{
	// Region tags live in parentheses; scan from the first '(' so title words
	// (e.g. a game literally named "Asia") are not misread as a region.
	int const open = description.indexOf(QLatin1Char('('));
	if (open < 0)
		return QString();

	const QHash<QString, QString> &map = keywordMap();
	QString token;
	for (int i = open; i <= description.size(); ++i)
	{
		QChar const c = (i < description.size()) ? description.at(i) : QLatin1Char(' ');
		if (c.isLetter())
		{
			token.append(c.toLower());
		}
		else if (!token.isEmpty())
		{
			auto it = map.constFind(token);
			if (it != map.constEnd())
				return it.value();
			token.clear();
		}
	}
	return QString();
}

QString systemRegion()
{
	switch (QLocale::system().territory())
	{
	case QLocale::UnitedStates:                          return QStringLiteral("USA");
	case QLocale::Japan:                                 return QStringLiteral("Japan");
	case QLocale::SouthKorea:                            return QStringLiteral("Korea");
	case QLocale::China:                                 return QStringLiteral("China");
	case QLocale::Taiwan:                                return QStringLiteral("Taiwan");
	case QLocale::HongKong:                              return QStringLiteral("Hong Kong");
	case QLocale::Brazil:                                return QStringLiteral("Brazil");
	case QLocale::UnitedKingdom:                         return QStringLiteral("UK");
	case QLocale::Germany:                               return QStringLiteral("Germany");
	case QLocale::France:                                return QStringLiteral("France");
	case QLocale::Italy:                                 return QStringLiteral("Italy");
	case QLocale::Spain:                                 return QStringLiteral("Spain");
	case QLocale::Netherlands:                           return QStringLiteral("Netherlands");
	case QLocale::Sweden:                                return QStringLiteral("Sweden");
	case QLocale::Australia:                             return QStringLiteral("Australia");
	case QLocale::Argentina:                             return QStringLiteral("Argentina");
	case QLocale::Canada:                                return QStringLiteral("USA");   // closest
	default:                                             return QStringLiteral("Europe");
	}
}

} // namespace osd::qtui
