// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwaremodel.cpp - Qt item model for a system's software lists
//
//============================================================

#include "softwaremodel.h"

#include <QtGui/QBrush>


namespace osd::qtui {

SoftwareModel::SoftwareModel(QObject *parent) :
	QAbstractTableModel(parent)
{
}

void SoftwareModel::setEntries(std::vector<qtui_software_entry> entries)
{
	beginResetModel();
	m_entries = std::move(entries);
	endResetModel();
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
