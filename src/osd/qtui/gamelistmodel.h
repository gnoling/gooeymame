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
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtGui/QIcon>

#include <cstdint>
#include <unordered_map>
#include <vector>


class game_driver;

namespace osd::qtui {

class IconLoader;
class ThumbnailLoader;

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
		YearRole,
		// ROM availability (one of GameListModel::Availability)
		AvailabilityRole,
		// parent/clone source short name, or empty
		ParentNameRole,
		// true if this driver is a clone (has a non-BIOS parent)
		IsCloneRole,
		// true if this driver is its clone family's representative
		IsRepresentativeRole,
		// canonical region inferred from the description ("" if none)
		RegionRole,
		// VersionFlag bitmask (bootleg/hack/prototype)
		VersionFlagsRole
	};

	// ROM availability of a system, as determined by the auditor.
	enum Availability
	{
		AvailabilityUnknown = 0,
		Available = 1,
		Unavailable = 2
	};

	// How the representative ("primary") member of a clone family is chosen.
	enum VersionMode { MatchParent = 0, PromoteRegion = 1 };

	// Version classification flags (from MACHINE_UNOFFICIAL + description tags).
	enum VersionFlag { VersionBootleg = 0x1, VersionHack = 0x2, VersionPrototype = 0x4 };

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

	// Apply a batch of availability results keyed by system short name
	// (from the auditor or the cache).  Unknown names are ignored.
	void applyAvailabilityBatch(const QVector<QPair<QString, int>> &results);

	// Normalise a manufacturer string for grouping (strip "(...)" notes).
	static QString normaliseManufacturer(const char *manufacturer);

signals:
	// Emitted when clone-family representatives change (after a version setting
	// or per-family override changes), so proxies can re-filter.
	void versionsChanged();

public:

	// Re-read the clone "version" preference (versions/* in QSettings) and
	// recompute each family's representative; emits versionsChanged().
	void reloadVersionSettings();

	// Clone-family queries (rows are model rows, not driver_list indices).
	bool isClone(int row) const;
	bool isRepresentative(int row) const;
	int representativeRow(int row) const;       // representative of row's family
	QList<int> familyMemberRows(int row) const; // representative first
	int rowForName(const QString &shortName) const;   // -1 if unknown
	// Persist a per-family default-version override for the family of `row`.
	void setVersionOverride(int row, const QString &memberShortName);

	// Choose the image set used for grid thumbnails (a frontendpaths machine
	// key, e.g. "snap"); invalidates the cache and reloads on demand.
	void setThumbnailSource(const QString &machineKey);

private slots:
	void onIconLoaded(int row, const QByteArray &bytes);
	void onThumbnailLoaded(int row, quint64 generation, const QByteArray &bytes);

private:
	const game_driver &driverForRow(int row) const;
	QVariant iconForRow(int row) const;   // lazily requests/caches the icon
	QVariant thumbnailForRow(int row) const;   // lazily requests/caches the thumbnail

	// driver_list indices of the rows we expose (the internal "___empty"
	// dummy driver and BIOS roots are filtered out at build time).
	std::vector<int> m_rows;

	// Per-row ROM availability (Availability), default AvailabilityUnknown.
	std::vector<std::int8_t> m_availability;

	// Short name -> row, for applying availability results by name.
	QHash<QString, int> m_nameToRow;

	// Clone families.  m_familyRoot[row] is the parent row (or row itself for a
	// parent/standalone); m_familyMembers maps a root row to its members (root
	// first); m_representative[row] is the chosen "primary" row of its family.
	std::vector<int> m_familyRoot;
	std::unordered_map<int, std::vector<int>> m_familyMembers;
	std::vector<int> m_representative;
	std::vector<QString> m_region;               // canonical region per row
	std::vector<std::uint8_t> m_versionFlags;    // VersionFlag bitmask per row

	// "Version" preference (read from versions/* in QSettings).
	int m_versionMode = MatchParent;
	bool m_useSystemRegion = false;
	QStringList m_regionOrder;
	QHash<QString, QString> m_overrides;         // family root name -> member name

	void buildFamilies();           // one-time: families, region, version flags
	void computeRepresentatives();  // (re)derive m_representative from settings

	// Lazy, cached row icons from icons.zip (loaded on a worker thread).
	IconLoader *m_iconLoader = nullptr;
	QString m_iconsPath;
	mutable QHash<int, QIcon> m_iconCache;
	mutable QSet<int> m_iconRequested;

	// Lazy, cached grid thumbnails for the selected image set (worker thread).
	// A generation counter invalidates in-flight loads when the source changes.
	ThumbnailLoader *m_thumbLoader = nullptr;
	QString m_thumbKey;     // frontendpaths machine key ("" = none)
	QString m_thumbPath;    // resolved folder/zip for m_thumbKey
	quint64 m_thumbGen = 0;
	mutable QHash<int, QPixmap> m_thumbCache;
	mutable QSet<int> m_thumbRequested;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_GAMELISTMODEL_H
