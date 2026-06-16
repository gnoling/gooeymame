// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwaremodel.cpp - Qt item model for a system's software lists
//
//============================================================

#include "softwaremodel.h"

#include "frontendpaths.h"
#include "regions.h"
#include "thumbnailloader.h"

#include <QtCore/QSettings>
#include <QtGui/QBrush>

#include <climits>


namespace osd::qtui {

SoftwareModel::SoftwareModel(QObject *parent) :
	QAbstractTableModel(parent)
{
	m_thumbLoader = new ThumbnailLoader(this);
	connect(m_thumbLoader, &ThumbnailLoader::loaded, this, &SoftwareModel::onThumbnailLoaded);
}

void SoftwareModel::setEntries(std::vector<qtui_software_entry> entries)
{
	beginResetModel();
	m_entries = std::move(entries);
	++m_thumbGen;   // entries replaced: invalidate thumbnails
	m_thumbCache.clear();
	m_thumbRequested.clear();
	buildFamilies();
	reloadVersionSettings();   // reads versions/* and computes representatives
	endResetModel();
}

QString SoftwareModel::familyKey(int rootRow) const
{
	if (rootRow < 0 || rootRow >= int(m_entries.size()))
		return QString();
	return QString::fromStdString(m_entries[rootRow].list) + QLatin1Char('\x1f')
			+ QString::fromStdString(m_entries[rootRow].shortname);
}

void SoftwareModel::buildFamilies()
{
	int const n = int(m_entries.size());
	m_familyRoot.assign(n, -1);
	m_region.assign(n, QString());
	m_versionFlags.assign(n, 0);
	m_representative.assign(n, -1);
	m_familyMembers.clear();

	// (list, short name) -> row, to resolve a clone's parent within its list.
	QHash<QString, int> keyToRow;
	keyToRow.reserve(n * 2);
	for (int row = 0; row < n; row++)
		keyToRow.insert(QString::fromStdString(m_entries[row].list) + QLatin1Char('\x1f')
				+ QString::fromStdString(m_entries[row].shortname), row);

	for (int row = 0; row < n; row++)
	{
		const qtui_software_entry &entry = m_entries[row];

		int rootRow = row;
		if (!entry.parent.empty())
		{
			auto it = keyToRow.constFind(QString::fromStdString(entry.list) + QLatin1Char('\x1f')
					+ QString::fromStdString(entry.parent));
			if (it != keyToRow.constEnd())
				rootRow = it.value();
		}
		m_familyRoot[row] = rootRow;

		QString const desc = QString::fromStdString(entry.description);
		m_region[row] = extractRegion(desc);

		std::uint8_t flags = 0;
		QString const lower = desc.toLower();
		if (lower.contains(QStringLiteral("bootleg")))
			flags |= VersionBootleg;
		if (lower.contains(QStringLiteral("hack")))
			flags |= VersionHack;
		if (lower.contains(QStringLiteral("prototype")))
			flags |= VersionPrototype;
		m_versionFlags[row] = flags;
	}

	for (int row = 0; row < n; row++)
		m_familyMembers[m_familyRoot[row]].push_back(row);
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

void SoftwareModel::computeRepresentatives()
{
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
		int rep = root;

		auto ov = m_overrides.constFind(familyKey(root));
		if (ov != m_overrides.constEnd())
		{
			for (int member : members)
				if (QString::fromStdString(m_entries[member].shortname) == ov.value())
				{
					rep = member;
					break;
				}
		}
		else if (m_versionMode == PromoteRegion)
		{
			int bestRank = INT_MAX;
			int bestRow = root;
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

void SoftwareModel::reloadVersionSettings()
{
	QSettings settings;
	m_versionMode = settings.value(QStringLiteral("versions/mode"), int(MatchParent)).toInt();
	m_useSystemRegion = settings.value(QStringLiteral("versions/useSystemRegion"), false).toBool();
	QStringList const order = settings.value(QStringLiteral("versions/order")).toStringList();
	m_regionOrder = order.isEmpty() ? defaultRegionOrder() : order;

	m_overrides.clear();
	settings.beginGroup(QStringLiteral("versions/swoverrides"));
	const QStringList keys = settings.childKeys();
	for (const QString &key : keys)
		m_overrides.insert(key, settings.value(key).toString());
	settings.endGroup();

	computeRepresentatives();

	if (!m_entries.empty())
		emit dataChanged(index(0, 0), index(int(m_entries.size()) - 1, COLUMN_COUNT - 1),
				{ IsRepresentativeRole, IsCloneRole });
	emit versionsChanged();
}

bool SoftwareModel::isClone(int row) const
{
	return row >= 0 && row < int(m_familyRoot.size()) && m_familyRoot[row] != row;
}

bool SoftwareModel::isRepresentative(int row) const
{
	return row >= 0 && row < int(m_representative.size()) && m_representative[row] == row;
}

int SoftwareModel::representativeRow(int row) const
{
	return (row >= 0 && row < int(m_representative.size())) ? m_representative[row] : row;
}

QList<int> SoftwareModel::familyMemberRows(int row) const
{
	QList<int> out;
	if (row < 0 || row >= int(m_familyRoot.size()))
		return out;
	auto it = m_familyMembers.find(m_familyRoot[row]);
	if (it == m_familyMembers.end())
		return out;
	int const rep = m_representative[m_familyRoot[row]];
	if (rep >= 0)
		out << rep;
	for (int member : it->second)
		if (member != rep)
			out << member;
	return out;
}

void SoftwareModel::setVersionOverride(int row, const QString &memberShortName)
{
	if (row < 0 || row >= int(m_familyRoot.size()))
		return;
	QString const key = familyKey(m_familyRoot[row]);
	if (key.isEmpty())
		return;
	QSettings settings;
	settings.beginGroup(QStringLiteral("versions/swoverrides"));
	settings.setValue(key, memberShortName);
	settings.endGroup();
	reloadVersionSettings();
}

void SoftwareModel::setHostSystem(const QString &system)
{
	if (system == m_hostSystem)
		return;
	m_hostSystem = system;
	std::string const parent = qtui_parent_of(system.toStdString());
	m_hostParent = QString::fromStdString(parent);
	// New host: any machine-fallback thumbnails are stale.
	++m_thumbGen;
	m_thumbCache.clear();
	m_thumbRequested.clear();
}

void SoftwareModel::setThumbnailSource(const QString &softwareKey, const QString &machineKey)
{
	if (softwareKey == m_thumbSwKey && machineKey == m_thumbMachineKey)
		return;
	m_thumbSwKey = softwareKey;
	m_thumbMachineKey = machineKey;
	m_thumbSwPath = softwareKey.isEmpty() ? QString() : frontendFolderPath(softwareKey);
	m_thumbMachinePath = machineKey.isEmpty() ? QString() : frontendFolderPath(machineKey);
	++m_thumbGen;
	m_thumbCache.clear();
	m_thumbRequested.clear();
	if (!m_entries.empty())
		emit dataChanged(index(0, COLUMN_DESCRIPTION),
				index(int(m_entries.size()) - 1, COLUMN_DESCRIPTION), { kThumbnailRole });
}

QVariant SoftwareModel::thumbnailForRow(int row) const
{
	if (m_thumbSwPath.isEmpty() && m_thumbMachinePath.isEmpty())
		return QVariant();

	auto it = m_thumbCache.constFind(row);
	if (it != m_thumbCache.constEnd())
		return it.value();

	if (!m_thumbRequested.contains(row))
	{
		m_thumbRequested.insert(row);
		const qtui_software_entry &entry = m_entries[row];
		ArtCandidates candidates;
		if (!m_thumbSwPath.isEmpty() && !entry.list.empty() && !entry.shortname.empty())
			candidates.append({ m_thumbSwPath,
					QString::fromStdString(entry.list) + QLatin1Char('/')
							+ QString::fromStdString(entry.shortname) + QStringLiteral(".png") });
		if (!m_thumbMachinePath.isEmpty() && !m_hostSystem.isEmpty())
		{
			candidates.append({ m_thumbMachinePath, m_hostSystem + QStringLiteral(".png") });
			if (!m_hostParent.isEmpty())
				candidates.append({ m_thumbMachinePath, m_hostParent + QStringLiteral(".png") });
		}
		if (!candidates.isEmpty())
			m_thumbLoader->request(row, m_thumbGen, candidates);
	}
	return QVariant();
}

void SoftwareModel::onThumbnailLoaded(int row, quint64 generation, const QByteArray &bytes)
{
	if (generation != m_thumbGen)
		return;   // stale

	QPixmap pixmap;
	if (!bytes.isEmpty())
		pixmap.loadFromData(bytes);

	m_thumbCache.insert(row, pixmap);
	if (!pixmap.isNull())
	{
		QModelIndex const idx = index(row, COLUMN_DESCRIPTION);
		emit dataChanged(idx, idx, { kThumbnailRole });
	}
}

void SoftwareModel::setAvailabilities(const QVector<int> &availability)
{
	int const count = qMin(int(m_entries.size()), int(availability.size()));
	for (int i = 0; i < count; i++)
		m_entries[i].availability = availability[i];

	if (count > 0)
		emit dataChanged(index(0, 0), index(count - 1, COLUMN_COUNT - 1));
}

QString SoftwareModel::shortNameForRow(int row) const
{
	if (row < 0 || row >= int(m_entries.size()))
		return QString();
	return QString::fromStdString(m_entries[row].shortname);
}

QString SoftwareModel::listForRow(int row) const
{
	if (row < 0 || row >= int(m_entries.size()))
		return QString();
	return QString::fromStdString(m_entries[row].list);
}

int SoftwareModel::rowCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return int(m_entries.size());
}

int SoftwareModel::columnCount(const QModelIndex &parent) const
{
	if (parent.isValid())
		return 0;
	return COLUMN_COUNT;
}

QVariant SoftwareModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= int(m_entries.size()))
		return QVariant();

	const qtui_software_entry &entry = m_entries[index.row()];

	if (role == SupportedRole)
		return entry.supported;

	if (role == AvailabilityRole)
		return entry.availability;

	if (role == kThumbnailRole)
		return thumbnailForRow(index.row());

	if (role == IsCloneRole)
		return isClone(index.row());
	if (role == IsRepresentativeRole)
		return isRepresentative(index.row());
	if (role == RegionRole)
		return m_region.empty() ? QString() : m_region[index.row()];
	if (role == VersionFlagsRole)
		return m_versionFlags.empty() ? 0 : int(m_versionFlags[index.row()]);

	if (role == Qt::ForegroundRole)
	{
		// Grey out software whose ROMs are missing (matches the system list).
		if (entry.availability == 2 /* QTUI_AVAIL_UNAVAILABLE */)
			return QBrush(Qt::gray);
		return QVariant();
	}

	if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
	{
		switch (index.column())
		{
		case COLUMN_DESCRIPTION: return QString::fromStdString(entry.description);
		case COLUMN_NAME:        return QString::fromStdString(entry.shortname);
		case COLUMN_YEAR:        return QString::fromStdString(entry.year);
		case COLUMN_PUBLISHER:   return QString::fromStdString(entry.publisher);
		case COLUMN_SUPPORTED:
			switch (entry.supported)
			{
			case 0:  return tr("Supported");
			case 1:  return tr("Partial");
			default: return tr("Unsupported");
			}
		case COLUMN_LIST:        return QString::fromStdString(entry.list);
		}
	}
	else if (role == Qt::TextAlignmentRole && index.column() == COLUMN_YEAR)
	{
		return int(Qt::AlignCenter);
	}

	return QVariant();
}

QVariant SoftwareModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
		return QVariant();

	switch (section)
	{
	case COLUMN_DESCRIPTION: return tr("Software");
	case COLUMN_NAME:        return tr("Short name");
	case COLUMN_YEAR:        return tr("Year");
	case COLUMN_PUBLISHER:   return tr("Publisher");
	case COLUMN_SUPPORTED:   return tr("Supported");
	case COLUMN_LIST:        return tr("List");
	}
	return QVariant();
}

} // namespace osd::qtui
