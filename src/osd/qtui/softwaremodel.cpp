// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwaremodel.cpp - Qt item model for a system's software lists
//
//============================================================

#include "softwaremodel.h"

#include "frontendpaths.h"
#include "iconloader.h"
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

	// Row icons (icons.zip) loaded lazily on a worker thread, like the machine list.
	m_iconsPath = frontendFolderPath(QStringLiteral("icons"));
	m_iconLoader = new IconLoader(this);
	connect(m_iconLoader, &IconLoader::loaded, this, &SoftwareModel::onIconLoaded);
	// Default = native icon set (per-software icon, then host machine), matching
	// the historical behavior until MainWindow applies the user's configuration.
	if (!m_iconsPath.isEmpty())
		m_iconChain.append({ QString(), QString(), QString(), QString(), true });
}

void SoftwareModel::setIconSources(const QVector<IconSourceKeySw> &sources, bool preferOwn, bool family)
{
	QVector<IconSrcSw> chain;
	for (const IconSourceKeySw &s : sources)
	{
		IconSrcSw src;
		src.native = s.native;
		src.swKey = s.swKey;
		src.machineKey = s.machineKey;
		src.swPath = s.swKey.isEmpty() ? QString() : frontendFolderPath(s.swKey);
		src.machinePath = s.machineKey.isEmpty() ? QString() : frontendFolderPath(s.machineKey);
		// Native uses the icons path; a non-native source is kept even with no
		// primary path since the secondary media root may still supply it.
		if (src.native && m_iconsPath.isEmpty())
			continue;
		chain.append(src);
	}
	if (chain == m_iconChain && preferOwn == m_iconPreferOwn && family == m_iconFamily)
		return;
	m_iconChain = chain;
	m_iconPreferOwn = preferOwn;
	m_iconFamily = family;
	invalidateIconCache();
}

void SoftwareModel::setEntries(std::vector<qtui_software_entry> entries)
{
	beginResetModel();
	m_entries = std::move(entries);
	++m_thumbGen;   // entries replaced: invalidate thumbnails
	m_thumbCache.clear();
	m_thumbRequested.clear();
	m_iconCache.clear();      // row->entry mapping changed: invalidate icons
	m_iconRequested.clear();
	buildFamilies();
	applyVersionSettings();   // compute representatives without emitting mid-reset
	endResetModel();          // endResetModel signals the change to proxies
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

void SoftwareModel::applyVersionSettings()
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
}

void SoftwareModel::reloadVersionSettings()
{
	applyVersionSettings();

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

QList<int> SoftwareModel::groupRows() const
{
	QList<int> out;
	out.reserve(int(m_familyMembers.size()));
	for (const auto &entry : m_familyMembers)
		out << m_representative[entry.first];
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
	// New host: any machine-fallback thumbnails and host-icon rows are stale.
	++m_thumbGen;
	m_thumbCache.clear();
	m_thumbRequested.clear();
	m_iconCache.clear();
	m_iconRequested.clear();
}

void SoftwareModel::setThumbnailSources(const QVector<QPair<QString, QString>> &keys, bool familyFallback)
{
	QVector<ThumbSource> chain;
	for (const auto &pair : keys)
	{
		ThumbSource src;
		src.swKey = pair.first;
		src.machineKey = pair.second;
		src.swPath = pair.first.isEmpty() ? QString() : frontendFolderPath(pair.first);
		src.machinePath = pair.second.isEmpty() ? QString() : frontendFolderPath(pair.second);
		// Keep the source even if neither primary path is configured: the
		// secondary media root (resolved at request time) may still supply it.
		chain.append(src);
	}
	bool same = (familyFallback == m_thumbFamily) && (chain.size() == m_thumbChain.size());
	for (int i = 0; same && i < chain.size(); ++i)
		same = (chain[i].swPath == m_thumbChain[i].swPath) && (chain[i].machinePath == m_thumbChain[i].machinePath)
				&& (chain[i].swKey == m_thumbChain[i].swKey) && (chain[i].machineKey == m_thumbChain[i].machineKey);
	if (same)
		return;

	m_thumbChain = chain;
	m_thumbFamily = familyFallback;
	++m_thumbGen;
	m_thumbCache.clear();
	m_thumbRequested.clear();
	if (!m_entries.empty())
		emit dataChanged(index(0, COLUMN_DESCRIPTION),
				index(int(m_entries.size()) - 1, COLUMN_DESCRIPTION), { kThumbnailRole });
}

QVariant SoftwareModel::thumbnailForRow(int row) const
{
	if (m_thumbChain.isEmpty())
		return QVariant();

	auto it = m_thumbCache.constFind(row);
	if (it != m_thumbCache.constEnd())
		return it.value();

	if (!m_thumbRequested.contains(row))
	{
		m_thumbRequested.insert(row);

		// Software (_SL) names to try: this item, then the other family members.
		QVector<QPair<QString, QString>> swNames;   // (list, shortname)
		auto addSw = [&swNames] (const qtui_software_entry &e) {
			if (e.list.empty() || e.shortname.empty())
				return;
			QPair<QString, QString> nm(QString::fromStdString(e.list), QString::fromStdString(e.shortname));
			if (!swNames.contains(nm))
				swNames.append(nm);
		};
		addSw(m_entries[row]);
		if (m_thumbFamily)
			for (int member : familyMemberRows(row))
				if (member >= 0 && member < int(m_entries.size()))
					addSw(m_entries[member]);

		// For each art type (primary first): the software _SL images, then the
		// host-machine image as a last resort for that type; finally the optional
		// secondary media root(s) (<root>/<key>/<list>/<sw>.png) fill any gaps.
		QStringList const secondaryRoots = frontendFolderPathList(QStringLiteral("secondaryRoot"));
		ArtCandidates candidates;
		for (const ThumbSource &src : m_thumbChain)
		{
			if (!src.swPath.isEmpty())
				for (const auto &nm : swNames)
					candidates.append({ src.swPath, nm.first + QLatin1Char('/') + nm.second + QStringLiteral(".png") });
			if (!src.machinePath.isEmpty() && !m_hostSystem.isEmpty())
			{
				candidates.append({ src.machinePath, m_hostSystem + QStringLiteral(".png") });
				if (!m_hostParent.isEmpty())
					candidates.append({ src.machinePath, m_hostParent + QStringLiteral(".png") });
			}
			for (const QString &secondaryRoot : secondaryRoots)
			{
				if (!src.swKey.isEmpty())
				{
					QString const base = secondaryRoot + QLatin1Char('/') + src.swKey;
					for (const auto &nm : swNames)
						candidates.append({ base, nm.first + QLatin1Char('/') + nm.second + QStringLiteral(".png") });
				}
				if (!src.machineKey.isEmpty() && !m_hostSystem.isEmpty())
				{
					QString const base = secondaryRoot + QLatin1Char('/') + src.machineKey;
					candidates.append({ base, m_hostSystem + QStringLiteral(".png") });
					if (!m_hostParent.isEmpty())
						candidates.append({ base, m_hostParent + QStringLiteral(".png") });
				}
			}
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

QVariant SoftwareModel::iconForRow(int row) const
{
	if (m_iconChain.isEmpty() || row < 0 || row >= int(m_entries.size()))
		return QVariant();

	auto it = m_iconCache.constFind(row);
	if (it != m_iconCache.constEnd())
		return it.value();

	if (!m_iconRequested.contains(row))
	{
		m_iconRequested.insert(row);

		// Software (_SL) names to try: this item, then (optionally) the other
		// family members (different-region variants).
		QVector<QPair<QString, QString>> swNames;   // (list, shortname)
		auto addSw = [&swNames] (const qtui_software_entry &e) {
			if (e.list.empty() || e.shortname.empty())
				return;
			QPair<QString, QString> nm(QString::fromStdString(e.list), QString::fromStdString(e.shortname));
			if (!swNames.contains(nm))
				swNames.append(nm);
		};
		addSw(m_entries[row]);
		if (m_iconFamily)
			for (int member : familyMemberRows(row))
				if (member >= 0 && member < int(m_entries.size()))
					addSw(m_entries[member]);

		QStringList const secondaryRoots = frontendFolderPathList(QStringLiteral("secondaryRoot"));

		// Candidates for one art source, split into the software item's own art
		// and the host-machine fallback so the two orderings can interleave them.
		auto ownCands = [&] (const IconSrcSw &src) {
			ArtCandidates c;
			if (src.native)
			{
				for (const auto &nm : swNames)
				{
					c.append({ m_iconsPath, nm.first + QLatin1Char('/') + nm.second + QStringLiteral(".ico") });
					c.append({ m_iconsPath, nm.second + QStringLiteral(".ico") });
				}
			}
			else
			{
				if (!src.swPath.isEmpty())
					for (const auto &nm : swNames)
						c.append({ src.swPath, nm.first + QLatin1Char('/') + nm.second + QStringLiteral(".png") });
				if (!src.swKey.isEmpty())
					for (const QString &secondaryRoot : secondaryRoots)
					{
						QString const base = secondaryRoot + QLatin1Char('/') + src.swKey;
						for (const auto &nm : swNames)
							c.append({ base, nm.first + QLatin1Char('/') + nm.second + QStringLiteral(".png") });
					}
			}
			return c;
		};
		auto hostCands = [&] (const IconSrcSw &src) {
			ArtCandidates c;
			QString const ext = src.native ? QStringLiteral(".ico") : QStringLiteral(".png");
			if (src.native)
			{
				if (!m_hostSystem.isEmpty()) c.append({ m_iconsPath, m_hostSystem + ext });
				if (!m_hostParent.isEmpty()) c.append({ m_iconsPath, m_hostParent + ext });
			}
			else
			{
				if (!src.machinePath.isEmpty() && !m_hostSystem.isEmpty())
				{
					c.append({ src.machinePath, m_hostSystem + ext });
					if (!m_hostParent.isEmpty()) c.append({ src.machinePath, m_hostParent + ext });
				}
				if (!src.machineKey.isEmpty() && !m_hostSystem.isEmpty())
					for (const QString &secondaryRoot : secondaryRoots)
					{
						QString const base = secondaryRoot + QLatin1Char('/') + src.machineKey;
						c.append({ base, m_hostSystem + ext });
						if (!m_hostParent.isEmpty()) c.append({ base, m_hostParent + ext });
					}
			}
			return c;
		};

		ArtCandidates candidates;
		if (m_iconPreferOwn)
		{
			// The item's own artwork (any listed type) beats the host icon.
			for (const IconSrcSw &src : m_iconChain)
				candidates += ownCands(src);
			for (const IconSrcSw &src : m_iconChain)
				candidates += hostCands(src);
		}
		else
		{
			// Each art type checks the item then the host before the next type.
			for (const IconSrcSw &src : m_iconChain)
			{
				candidates += ownCands(src);
				candidates += hostCands(src);
			}
		}
		if (!candidates.isEmpty())
			m_iconLoader->request(row, candidates);
	}
	return QVariant();
}

void SoftwareModel::onIconLoaded(int row, const QByteArray &bytes)
{
	// Scale to the size/mode the views display icons at (QIcon won't upscale a
	// pixmap on its own).
	QIcon const icon = makeRowIcon(bytes, m_iconDisplaySize, m_iconSmooth);

	// Cache (even an empty icon) so we neither re-request nor re-decode.
	m_iconCache.insert(row, icon);
	if (!icon.isNull())
	{
		QModelIndex const idx = index(row, COLUMN_DESCRIPTION);
		emit dataChanged(idx, idx, { Qt::DecorationRole });
	}
}

void SoftwareModel::invalidateIconCache()
{
	// Drop cached icons so they re-decode and re-scale as the views ask again.
	if (m_iconCache.isEmpty() && m_iconRequested.isEmpty())
		return;
	m_iconCache.clear();
	m_iconRequested.clear();
	if (rowCount() > 0)
	{
		emit dataChanged(index(0, COLUMN_DESCRIPTION),
				index(rowCount() - 1, COLUMN_DESCRIPTION), { Qt::DecorationRole });
	}
}

void SoftwareModel::setIconDisplaySize(int px)
{
	if (px == m_iconDisplaySize)
		return;
	m_iconDisplaySize = px;
	invalidateIconCache();
}

void SoftwareModel::setIconSmoothScaling(bool smooth)
{
	if (smooth == m_iconSmooth)
		return;
	m_iconSmooth = smooth;
	invalidateIconCache();
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

QString SoftwareModel::parentForRow(int row) const
{
	if (row < 0 || row >= int(m_entries.size()))
		return QString();
	return QString::fromStdString(m_entries[row].parent);
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

	if (role == Qt::DecorationRole && index.column() == COLUMN_DESCRIPTION)
		return iconForRow(index.row());

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
		case COLUMN_CLONEOF:     return QString::fromStdString(entry.parent);
		case COLUMN_ROMS:
			switch (entry.availability)
			{
			case 1:  return tr("Available");
			case 2:  return tr("Missing");
			default: return QString();
			}
		case COLUMN_REGION:      return m_region.empty() ? QString() : m_region[index.row()];
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
	case COLUMN_CLONEOF:     return tr("Clone of");
	case COLUMN_ROMS:        return tr("ROMs");
	case COLUMN_REGION:      return tr("Region");
	}
	return QVariant();
}

} // namespace osd::qtui
