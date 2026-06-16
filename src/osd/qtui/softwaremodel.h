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
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtGui/QPixmap>

#include <vector>

namespace osd::qtui {

class ThumbnailLoader;

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

	// Host machine for the current entries (used for thumbnail fallback art).
	void setHostSystem(const QString &system);

	// Choose the image set for grid thumbnails: a software (_SL) key plus the
	// host-machine key to fall back to.  Either may be empty.
	void setThumbnailSource(const QString &softwareKey, const QString &machineKey);

	// Apply availability results (qtui_availability) indexed to match the
	// current entries, from the background audit phase.
	void setAvailabilities(const QVector<int> &availability);

	// Software short name for a row, e.g. "smb", or empty if out of range.
	QString shortNameForRow(int row) const;

	// Software list name for a row, e.g. "nes", or empty if out of range.
	QString listForRow(int row) const;

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private slots:
	void onThumbnailLoaded(int row, quint64 generation, const QByteArray &bytes);

private:
	QVariant thumbnailForRow(int row) const;

	std::vector<qtui_software_entry> m_entries;

	// Grid thumbnails: software (_SL) art with host-machine fallback.
	ThumbnailLoader *m_thumbLoader = nullptr;
	QString m_thumbSwKey,  m_thumbSwPath;       // software image set
	QString m_thumbMachineKey, m_thumbMachinePath;   // host-machine fallback set
	QString m_hostSystem, m_hostParent;
	quint64 m_thumbGen = 0;
	mutable QHash<int, QPixmap> m_thumbCache;
	mutable QSet<int> m_thumbRequested;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_SOFTWAREMODEL_H
