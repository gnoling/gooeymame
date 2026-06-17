// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gamelistmodel.cpp - Qt item model exposing MAME's driver list
//
//============================================================

#include "emu.h"

#include "gamelistmodel.h"

#include "drivenum.h"

#include "frontendpaths.h"
#include "iconloader.h"
#include "regions.h"
#include "thumbnailloader.h"

#include <QtCore/QSet>
#include <QtCore/QSettings>
#include <QtGui/QBrush>
#include <QtGui/QPixmap>

#include <algorithm>
#include <climits>
#include <cstring>


namespace osd::qtui {

GameListModel::GameListModel(QObject *parent) :
	QAbstractTableModel(parent)
{
	// Build the row -> driver_list index map once.  The list is static for
	// the lifetime of the process, so this never needs to change.  Skip the
	// internal placeholder driver ("___empty") and BIOS root entries, which
	// are not directly launchable systems.
	std::size_t const total = driver_list::total();
	m_rows.reserve(total);
	for (std::size_t i = 0; i < total; i++)
	{
		const game_driver &drv = driver_list::driver(i);
		if (&drv == &GAME_NAME(___empty))
			continue;
		if (drv.flags & machine_flags::IS_BIOS_ROOT)
			continue;
		m_rows.push_back(int(i));
	}

	m_availability.assign(m_rows.size(), AvailabilityUnknown);

	m_nameToRow.reserve(int(m_rows.size()));
	for (int row = 0; row < int(m_rows.size()); row++)
		m_nameToRow.insert(QString::fromLatin1(driver_list::driver(m_rows[row]).name), row);

	// Clone families + region/version classification (one-time), then the
	// representative selection from the saved version preference.
	buildFamilies();
	reloadVersionSettings();

	// Row icons (icons.zip) are loaded lazily on a worker thread.
	m_iconsPath = frontendFolderPath(QStringLiteral("icons"));
	m_iconLoader = new IconLoader(this);
	connect(m_iconLoader, &IconLoader::loaded, this, &GameListModel::onIconLoaded);

	// Grid thumbnails (image set chosen at runtime) load on their own thread.
	m_thumbLoader = new ThumbnailLoader(this);
	connect(m_thumbLoader, &ThumbnailLoader::loaded, this, &GameListModel::onThumbnailLoaded);
}

void GameListModel::setThumbnailSources(const QStringList &machineKeys, bool familyFallback)
{
	QVector<QPair<QString, QString>> chain;
	for (const QString &key : machineKeys)
	{
		if (key.isEmpty())
			continue;
		QString const path = frontendFolderPath(key);
		if (!path.isEmpty())
			chain.append({ key, path });
	}
	if (chain == m_thumbChain && familyFallback == m_thumbFamily)
		return;
	m_thumbChain = chain;
	m_thumbFamily = familyFallback;
	++m_thumbGen;   // discard any in-flight results for the previous source
	m_thumbCache.clear();
	m_thumbRequested.clear();
	if (!m_rows.empty())
		emit dataChanged(index(0, COLUMN_DESCRIPTION),
				index(int(m_rows.size()) - 1, COLUMN_DESCRIPTION), { kThumbnailRole });
}

QVariant GameListModel::thumbnailForRow(int row) const
{
	if (m_thumbChain.isEmpty())
		return QVariant();

	auto it = m_thumbCache.constFind(row);
	if (it != m_thumbCache.constEnd())
		return it.value();

	if (!m_thumbRequested.contains(row))
	{
		m_thumbRequested.insert(row);

		// Base names to try, in order: this set, its clone parent, then the
		// other family members (different-region variants).
		QStringList names;
		const game_driver &drv = driverForRow(row);
		names << QString::fromLatin1(drv.name);
		if (drv.parent && drv.parent[0] && std::strcmp(drv.parent, "0") != 0)
			names << QString::fromLatin1(drv.parent);
		if (m_thumbFamily)
			for (int member : familyMemberRows(row))
			{
				QString const n = QString::fromLatin1(driverForRow(member).name);
				if (!names.contains(n))
					names << n;
			}

		// Try each art type (primary first) across every candidate name; the
		// loader takes the first file that exists.
		ArtCandidates candidates;
		for (const auto &src : m_thumbChain)
			for (const QString &n : names)
				candidates.append({ src.second, n + QStringLiteral(".png") });
		m_thumbLoader->request(row, m_thumbGen, candidates);
	}
	return QVariant();
}

void GameListModel::onThumbnailLoaded(int row, quint64 generation, const QByteArray &bytes)
{
	if (generation != m_thumbGen)
		return;   // stale: the source changed after this request

	QPixmap pixmap;
	if (!bytes.isEmpty())
		pixmap.loadFromData(bytes);

	// Cache even a null pixmap so we neither re-request nor re-decode.
	m_thumbCache.insert(row, pixmap);
	if (!pixmap.isNull())
	{
		QModelIndex const idx = index(row, COLUMN_DESCRIPTION);
		emit dataChanged(idx, idx, { kThumbnailRole });
	}
}

void GameListModel::buildFamilies()
{
	int const n = int(m_rows.size());
	m_familyRoot.assign(n, -1);
	m_region.assign(n, QString());
	m_versionFlags.assign(n, 0);
	m_representative.assign(n, -1);
	m_familyMembers.clear();

	// driver_list index -> our row, to resolve parents to rows.
	std::unordered_map<int, int> idxToRow;
	idxToRow.reserve(n * 2);
	for (int row = 0; row < n; row++)
		idxToRow.emplace(m_rows[row], row);

	for (int row = 0; row < n; row++)
	{
		int const di = m_rows[row];
		const game_driver &drv = driver_list::driver(di);

		int rootRow = row;
		int const parentIdx = driver_list::non_bios_clone(di);
		if (parentIdx >= 0)
		{
			auto it = idxToRow.find(parentIdx);
			if (it != idxToRow.end())
				rootRow = it->second;
		}
		m_familyRoot[row] = rootRow;

		QString const desc = QString::fromUtf8(drv.type.fullname());
		m_region[row] = extractRegion(desc);

		std::uint8_t flags = 0;
		QString const lower = desc.toLower();
		if (drv.flags & machine_flags::UNOFFICIAL)
			flags |= VersionHack;
		if (lower.contains(QStringLiteral("bootleg")))
			flags |= VersionBootleg;
		if (lower.contains(QStringLiteral("hack")))
			flags |= VersionHack;
		if (lower.contains(QStringLiteral("prototype")) || (drv.flags & machine_flags::IS_INCOMPLETE))
			flags |= VersionPrototype;
		m_versionFlags[row] = flags;
	}

	for (int row = 0; row < n; row++)
		m_familyMembers[m_familyRoot[row]].push_back(row);

	// Ensure each family lists its root first (parents are not guaranteed to
	// precede their clones in driver_list order).
	for (auto &entry : m_familyMembers)
	{
		std::vector<int> &members = entry.second;
		for (std::size_t i = 1; i < members.size(); i++)
		{
			if (members[i] == entry.first)
			{
				std::swap(members[0], members[i]);
				break;
			}
		}
	}
}

void GameListModel::computeRepresentatives()
{
	// Effective priority order; the system region jumps to the front when the
	// auto toggle is on.
	QStringList order = m_regionOrder;
	if (m_useSystemRegion)
	{
		QString const sys = systemRegion();
		if (!sys.isEmpty())
		{
			order.removeAll(sys);
			order.prepend(sys);
		}
	}
	QHash<QString, int> rank;
	for (int i = 0; i < order.size(); i++)
		rank.insert(order[i], i);

	for (auto &entry : m_familyMembers)
	{
		int const root = entry.first;
		const std::vector<int> &members = entry.second;
		int rep = root;   // default: MAME's parent

		QString const rootName = QString::fromLatin1(driver_list::driver(m_rows[root]).name);
		auto ov = m_overrides.constFind(rootName);
		if (ov != m_overrides.constEnd())
		{
			int const r = rowForName(ov.value());
			if (r >= 0 && r < int(m_familyRoot.size()) && m_familyRoot[r] == root)
				rep = r;
		}
		else if (m_versionMode == PromoteRegion)
		{
			int bestRank = INT_MAX;
			int bestRow = root;   // members lists root first, so it wins ties
			for (int member : members)
			{
				int const rk = m_region[member].isEmpty()
						? INT_MAX : rank.value(m_region[member], INT_MAX);
				if (rk < bestRank)
				{
					bestRank = rk;
					bestRow = member;
				}
			}
			rep = bestRow;
		}

		for (int member : members)
			m_representative[member] = rep;
	}
}

void GameListModel::reloadVersionSettings()
{
	QSettings settings;
	m_versionMode = settings.value(QStringLiteral("versions/mode"), int(MatchParent)).toInt();
	m_useSystemRegion = settings.value(QStringLiteral("versions/useSystemRegion"), false).toBool();
	QStringList const order = settings.value(QStringLiteral("versions/order")).toStringList();
	m_regionOrder = order.isEmpty() ? defaultRegionOrder() : order;

	m_overrides.clear();
	settings.beginGroup(QStringLiteral("versions/overrides"));
	const QStringList keys = settings.childKeys();
	for (const QString &key : keys)
		m_overrides.insert(key, settings.value(key).toString());
	settings.endGroup();

	computeRepresentatives();

	if (!m_rows.empty())
		emit dataChanged(index(0, 0), index(int(m_rows.size()) - 1, COLUMN_COUNT - 1),
				{ IsRepresentativeRole, IsCloneRole });
	emit versionsChanged();
}

bool GameListModel::isClone(int row) const
{
	return row >= 0 && row < int(m_familyRoot.size()) && m_familyRoot[row] != row;
}

bool GameListModel::isRepresentative(int row) const
{
	return row >= 0 && row < int(m_representative.size()) && m_representative[row] == row;
}

int GameListModel::representativeRow(int row) const
{
	return (row >= 0 && row < int(m_representative.size())) ? m_representative[row] : row;
}

QList<int> GameListModel::familyMemberRows(int row) const
{
	QList<int> out;
	if (row < 0 || row >= int(m_familyRoot.size()))
		return out;
	auto it = m_familyMembers.find(m_familyRoot[row]);
	if (it == m_familyMembers.end())
		return out;
	int const rep = m_representative[m_familyRoot[row]];
	if (rep >= 0)
		out << rep;   // representative first
	for (int member : it->second)
		if (member != rep)
			out << member;
	return out;
}

QList<int> GameListModel::groupRows() const
{
	QList<int> out;
	out.reserve(int(m_familyMembers.size()));
	for (const auto &entry : m_familyMembers)
		out << m_representative[entry.first];
	return out;
}

int GameListModel::rowForName(const QString &shortName) const
{
	return m_nameToRow.value(shortName, -1);
}

void GameListModel::setVersionOverride(int row, const QString &memberShortName)
{
	if (row < 0 || row >= int(m_familyRoot.size()))
		return;
	QString const rootName = QString::fromLatin1(driver_list::driver(m_rows[m_familyRoot[row]]).name);
	QSettings settings;
	settings.beginGroup(QStringLiteral("versions/overrides"));
	settings.setValue(rootName, memberShortName);
	settings.endGroup();
	reloadVersionSettings();
}

int GameListModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return int(m_rows.size());
}

int GameListModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return COLUMN_COUNT;
}

int GameListModel::driverIndexForRow(int row) const
{
	if (row < 0 || row >= int(m_rows.size()))
		return -1;
	return m_rows[row];
}

const game_driver &GameListModel::driverForRow(int row) const
{
	return driver_list::driver(m_rows[row]);
}

QVariant GameListModel::iconForRow(int row) const
{
	if (m_iconsPath.isEmpty())
		return QVariant();

	auto it = m_iconCache.constFind(row);
	if (it != m_iconCache.constEnd())
		return it.value();

	// Not cached yet: queue a one-time async load (system icon, parent
	// fallback) and show nothing until it arrives.
	if (!m_iconRequested.contains(row))
	{
		m_iconRequested.insert(row);
		const game_driver &drv = driverForRow(row);
		QStringList entries;
		entries << QString::fromLatin1(drv.name) + QStringLiteral(".ico");
		if (drv.parent && drv.parent[0] && std::strcmp(drv.parent, "0") != 0)
			entries << QString::fromLatin1(drv.parent) + QStringLiteral(".ico");
		m_iconLoader->request(row, m_iconsPath, entries);
	}
	return QVariant();
}

void GameListModel::onIconLoaded(int row, const QByteArray &bytes)
{
	QIcon icon;
	if (!bytes.isEmpty())
	{
		QPixmap pixmap;
		if (pixmap.loadFromData(bytes))
			icon = QIcon(pixmap);
	}

	// Cache (even an empty icon) so we neither re-request nor re-decode.
	m_iconCache.insert(row, icon);
	if (!icon.isNull())
	{
		QModelIndex const idx = index(row, COLUMN_DESCRIPTION);
		emit dataChanged(idx, idx, { Qt::DecorationRole });
	}
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= int(m_rows.size()))
		return QVariant();

	const game_driver &drv = driverForRow(index.row());

	switch (role)
	{
	case DriverIndexRole:
		return m_rows[index.row()];

	case ShortNameRole:
		return QString::fromLatin1(drv.name);

	case WorkingRole:
		return !(drv.type.emulation_flags() & emu::detail::device_flags::NOT_WORKING);

	case ArcadeRole:
		return bool(drv.flags & machine_flags::TYPE_ARCADE);

	case ManufacturerRole:
		return normaliseManufacturer(drv.manufacturer);

	case YearRole:
		return QString::fromLatin1(drv.year ? drv.year : "");

	case AvailabilityRole:
		return int(m_availability[index.row()]);

	case ParentNameRole:
		return (drv.parent && drv.parent[0] && std::strcmp(drv.parent, "0") != 0)
				? QString::fromLatin1(drv.parent) : QString();

	case IsCloneRole:
		return isClone(index.row());

	case IsRepresentativeRole:
		return isRepresentative(index.row());

	case RegionRole:
		return m_region.empty() ? QString() : m_region[index.row()];

	case VersionFlagsRole:
		return m_versionFlags.empty() ? 0 : int(m_versionFlags[index.row()]);

	case kThumbnailRole:
		// Grid thumbnail for the row (lazily loaded for the chosen image set).
		return thumbnailForRow(index.row());

	case Qt::DecorationRole:
		// Show the system icon at the start of the Description column.
		if (index.column() == COLUMN_DESCRIPTION)
			return iconForRow(index.row());
		return QVariant();

	case Qt::ForegroundRole:
		// Grey out systems whose ROMs are known to be missing.
		if (m_availability[index.row()] == Unavailable)
			return QBrush(Qt::gray);
		return QVariant();

	case Qt::TextAlignmentRole:
		if (index.column() == COLUMN_YEAR)
			return int(Qt::AlignCenter);
		return QVariant();

	case Qt::DisplayRole:
	case Qt::ToolTipRole:
		switch (index.column())
		{
		case COLUMN_DESCRIPTION:
			return QString::fromUtf8(drv.type.fullname());
		case COLUMN_NAME:
			return QString::fromLatin1(drv.name);
		case COLUMN_YEAR:
			return QString::fromLatin1(drv.year ? drv.year : "");
		case COLUMN_MANUFACTURER:
			return QString::fromUtf8(drv.manufacturer ? drv.manufacturer : "");
		case COLUMN_STATUS:
			// Emulation status only for now; ROM availability arrives with
			// the audit phase.
			return (drv.type.emulation_flags() & emu::detail::device_flags::NOT_WORKING)
					? tr("Not working")
					: tr("Working");
		}
		break;

	default:
		break;
	}

	return QVariant();
}

QVariant GameListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();

	switch (section)
	{
	case COLUMN_DESCRIPTION:  return tr("Description");
	case COLUMN_NAME:         return tr("Short name");
	case COLUMN_YEAR:         return tr("Year");
	case COLUMN_MANUFACTURER: return tr("Manufacturer");
	case COLUMN_STATUS:       return tr("Status");
	}
	return QVariant();
}

QString GameListModel::normaliseManufacturer(const char *manufacturer)
{
	if (!manufacturer || !*manufacturer)
		return tr("<unknown>");

	QString result = QString::fromUtf8(manufacturer);

	// Drop any parenthetical qualifier, e.g. "Atari (Games)" -> "Atari".
	int const paren = result.indexOf('(');
	if (paren > 0)
		result.truncate(paren);

	return result.trimmed();
}

QStringList GameListModel::manufacturers() const
{
	QSet<QString> seen;
	for (int row : m_rows)
		seen.insert(normaliseManufacturer(driver_list::driver(row).manufacturer));

	QStringList list(seen.cbegin(), seen.cend());
	std::sort(list.begin(), list.end(), [] (const QString &a, const QString &b) {
		return a.localeAwareCompare(b) < 0;
	});
	return list;
}

void GameListModel::applyAvailabilityBatch(const QVector<QPair<QString, int>> &results)
{
	int minRow = -1;
	int maxRow = -1;
	for (const auto &entry : results)
	{
		auto it = m_nameToRow.constFind(entry.first);
		if (it == m_nameToRow.constEnd())
			continue;
		int const row = it.value();
		m_availability[row] = std::int8_t(entry.second);
		if (minRow < 0 || row < minRow)
			minRow = row;
		if (row > maxRow)
			maxRow = row;
	}

	if (minRow >= 0)
		emit dataChanged(index(minRow, 0), index(maxRow, COLUMN_COUNT - 1));
}

QStringList GameListModel::years() const
{
	QSet<QString> seen;
	for (int row : m_rows)
	{
		const char *year = driver_list::driver(row).year;
		if (year && *year)
			seen.insert(QString::fromLatin1(year));
	}

	QStringList list(seen.cbegin(), seen.cend());
	std::sort(list.begin(), list.end());
	return list;
}

} // namespace osd::qtui
