// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwaremodel.cpp - Qt item model for a system's software lists
//
//============================================================

#include "softwaremodel.h"

#include "frontendpaths.h"
#include "thumbnailloader.h"

#include <QtGui/QBrush>


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
	endResetModel();
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
