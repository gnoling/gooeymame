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

#include <QtCore/QSet>
#include <QtGui/QBrush>
#include <QtGui/QPixmap>

#include <algorithm>
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

	// Row icons (icons.zip) are loaded lazily on a worker thread.
	m_iconsPath = frontendFolderPath(QStringLiteral("icons"));
	m_iconLoader = new IconLoader(this);
	connect(m_iconLoader, &IconLoader::loaded, this, &GameListModel::onIconLoaded);
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
