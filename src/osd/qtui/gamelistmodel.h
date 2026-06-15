// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gamelistmodel.h - Qt item model exposing MAME's driver list
//
//  A read-only QAbstractTableModel backed directly by the static
//  driver_list (src/emu/drivenum.h).  No emulator state is created;
//  the model simply reads the compiled-in game_driver metadata.
//
//============================================================
#ifndef MAME_OSD_QTUI_GAMELISTMODEL_H
#define MAME_OSD_QTUI_GAMELISTMODEL_H

#pragma once

#include <QtCore/QAbstractTableModel>
#include <QtCore/QStringList>

#include <vector>


class game_driver;

namespace osd::qtui {

//============================================================
//  GameListModel - one row per playable driver.
//============================================================
class GameListModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	enum Column
	{
		COLUMN_DESCRIPTION = 0,
		COLUMN_NAME,
		COLUMN_YEAR,
		COLUMN_MANUFACTURER,
		COLUMN_STATUS,
		COLUMN_COUNT
	};

	// Custom roles used by the filtering proxy and the folder tree.
	enum Role
	{
		// driver_list index for the row (int)
		DriverIndexRole = Qt::UserRole + 1,
		// short name as a QString, for case-insensitive sorting/searching
		ShortNameRole,
		// true if the system is emulated well enough to be considered working
		WorkingRole,
		// true if the system is an arcade machine (vs. a console/computer/etc.)
		ArcadeRole,
		// normalised manufacturer name (parenthetical notes stripped)
		ManufacturerRole,
		// release year as a QString
		YearRole
	};

	explicit GameListModel(QObject *parent = nullptr);

	// QAbstractTableModel
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

	// Map a model row back to a driver_list index, or -1 if out of range.
	int driverIndexForRow(int row) const;

	// Sorted, de-duplicated lists for populating the folder tree.
	QStringList manufacturers() const;
	QStringList years() const;

	// Normalise a manufacturer string for grouping (strip "(...)" notes).
	static QString normaliseManufacturer(const char *manufacturer);

private:
	const game_driver &driverForRow(int row) const;

	// driver_list indices of the rows we expose (the internal "___empty"
	// dummy driver and BIOS roots are filtered out at build time).
	std::vector<int> m_rows;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_GAMELISTMODEL_H
