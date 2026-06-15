// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwaremodel.h - Qt item model for a system's software lists
//
//  Holds the flattened software entries returned by
//  qtui_enumerate_software() for the currently selected system.
//
//============================================================
#ifndef MAME_OSD_QTUI_SOFTWAREMODEL_H
#define MAME_OSD_QTUI_SOFTWAREMODEL_H

#pragma once

#include "emulator.h"

#include <QtCore/QAbstractTableModel>
#include <QtCore/QVector>

#include <vector>

namespace osd::qtui {

class SoftwareModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		COLUMN_DESCRIPTION = 0,
		COLUMN_NAME,
		COLUMN_YEAR,
		COLUMN_PUBLISHER,
		COLUMN_SUPPORTED,
		COLUMN_LIST,
		COLUMN_COUNT
	};

	enum Role
	{
		// support level: 0 = supported, 1 = partial, 2 = unsupported
		SupportedRole = Qt::UserRole + 1,
		// ROM availability: matches qtui_availability (0/1/2)
		AvailabilityRole
	};

	explicit SoftwareModel(QObject *parent = nullptr);

	void setEntries(std::vector<qtui_software_entry> entries);

	// Apply availability results (qtui_availability) indexed to match the
	// current entries, from the background audit phase.
	void setAvailabilities(const QVector<int> &availability);

	// Software short name for a row, e.g. "smb", or empty if out of range.
	QString shortNameForRow(int row) const;

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
	std::vector<qtui_software_entry> m_entries;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_SOFTWAREMODEL_H
