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
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
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
		// Additional, optional columns (hidden by default; toggled from the
		// list header's context menu).  Appended so the indices of the columns
		// above stay stable for saved header state and sort settings.
		COLUMN_CLONEOF,     // clone parent's short name ("" for parents)
		COLUMN_SOURCE,      // driver source file basename (e.g. galaga.cpp)
		COLUMN_ROMS,        // ROM availability (Available / Missing)
		COLUMN_SAVE,        // save-state support (Supported / No)
		COLUMN_REGION,      // region inferred from the description
		COLUMN_MEDIA,       // media types the driver uses (ROM / CHD)
		COLUMN_GENRE,       // genre from catlist/genre.ini
		COLUMN_LANGUAGE,    // language from languages.ini
		COLUMN_CONTROL,     // control types (needs a background machine scan)
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
		// emulation quality as an EmulationStatus (good / imperfect / preliminary)
		EmulationStatusRole,
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
		VersionFlagsRole,
		// true if the system has mechanical parts (MACHINE_MECHANICAL)
		IsMechanicalRole,
		// screenless state (one of GameListModel::Screenless); requires a
		// background scan, so it is ScreenlessUnknown until that completes
		IsScreenlessRole
	};

	// Whether a system has any screen device.  Determining this needs a
	// machine_config, so it is filled in lazily by a background scan.
	enum Screenless { ScreenlessUnknown = -1, HasScreen = 0, NoScreen = 1 };

	// ROM availability of a system, as determined by the auditor.
	enum Availability
	{
		AvailabilityUnknown = 0,
		Available = 1,
		Unavailable = 2
	};

	// Emulation quality of a system (mirrors the Status column text).
	enum EmulationStatus { EmuWorking = 0, EmuImperfect = 1, EmuNotWorking = 2 };

	// How the representative ("primary") member of a clone family is chosen.
	enum VersionMode { MatchParent = 0, PromoteRegion = 1 };

	// Version classification flags (from MACHINE_UNOFFICIAL + description tags).
	enum VersionFlag { VersionBootleg = 0x1, VersionHack = 0x2, VersionPrototype = 0x4 };

	// Media the driver declares in its ROM definition (COLUMN_MEDIA).
	enum MediaFlag { MediaRom = 0x1, MediaDisk = 0x2 };

	// Per-row control-type verdict (COLUMN_CONTROL); needs a machine_config
	// scan, so it stays ControlUnknown until the background scan fills it in.
	enum { ControlUnknown = -1 };

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

	// Whether a system short name is present in this build (used to hide folder
	// categories whose members aren't in the loaded driver set).
	bool hasSystem(const QString &shortName) const { return m_nameToRow.contains(shortName); }

	// Apply a batch of availability results keyed by system short name
	// (from the auditor or the cache).  Unknown names are ignored.
	void applyAvailabilityBatch(const QVector<QPair<QString, int>> &results);

	// Apply screenless scan results keyed by system short name (true = no
	// screen).  Unknown names are ignored.  Marks the model as having a
	// screenless verdict so IsScreenlessRole returns NoScreen/HasScreen.
	void applyScreenlessBatch(const std::vector<std::pair<std::string, bool>> &results);
	// Whether the one-time screenless scan has populated any verdicts.
	bool hasScreenlessData() const { return m_screenlessScanned; }

	// Provide the short-name -> value maps for the Genre and Language columns,
	// built from the EXTRAs .ini files (see MainWindow).  Empty until set.
	void setCategoryData(QHash<QString, QString> genre, QHash<QString, QString> language);

	// Apply control-type scan results keyed by system short name (a short human
	// label such as "Lightgun" / "Joystick (8-way)").  Marks the model as having
	// control data so COLUMN_CONTROL shows the verdicts.
	void applyControlBatch(const std::vector<std::pair<std::string, std::string>> &results);
	// Whether the one-time control-type scan has populated any verdicts.
	bool hasControlData() const { return m_controlScanned; }

	// Normalise a manufacturer string for grouping (strip "(...)" notes).
	static QString normaliseManufacturer(const char *manufacturer);

	// Split a filterable column's display text into the individual values used by
	// the combinable column filters.  Most columns yield one value; Media splits
	// "ROM + CHD" into {ROM, CHD} and Controls splits its comma list, so selecting
	// e.g. "CHD" matches a "ROM + CHD" row.  Shared with GameListProxy.
	static QStringList valueTokens(int column, const QString &display);

	// Sorted, de-duplicated set of values present in a filterable column (from
	// valueTokens over every row), for populating that column's filter submenu.
	QStringList distinctColumnValues(int column) const;

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
	QList<int> groupRows() const;               // one representative row per family
	int rowForName(const QString &shortName) const;   // -1 if unknown
	// Persist a per-family default-version override for the family of `row`.
	void setVersionOverride(int row, const QString &memberShortName);

	// Choose the image sets used for grid thumbnails: an ordered list of
	// frontendpaths machine keys (e.g. {"snap","titles",…}) tried in turn so a
	// missing primary image falls back to other art types.  When familyFallback
	// is true, each art type is also tried for the clone parent and the other
	// family members (different-region variants).  Invalidates the cache.
	void setThumbnailSources(const QStringList &machineKeys, bool familyFallback);

	// Pixel size the list/tree views display row icons at.  Icons are rescaled
	// to this since QIcon won't upscale a pixmap past its native size on its
	// own.  Invalidates the icon cache.
	void setIconDisplaySize(int px);

	// Whether enlarged icons are scaled smoothly (true) or with nearest-
	// neighbour (false, crisp pixel art).  Invalidates the icon cache.
	void setIconSmoothScaling(bool smooth);

private slots:
	void onIconLoaded(int row, const QByteArray &bytes);
	void onThumbnailLoaded(int row, quint64 generation, const QByteArray &bytes);

private:
	const game_driver &driverForRow(int row) const;
	QVariant iconForRow(int row) const;   // lazily requests/caches the icon
	void invalidateIconCache();           // drop cached icons so they re-scale
	QVariant thumbnailForRow(int row) const;   // lazily requests/caches the thumbnail

	// driver_list indices of the rows we expose (the internal "___empty"
	// dummy driver and BIOS roots are filtered out at build time).
	std::vector<int> m_rows;

	// Per-row ROM availability (Availability), default AvailabilityUnknown.
	std::vector<std::int8_t> m_availability;

	// Per-row screenless verdict (Screenless), default ScreenlessUnknown until
	// the background scan fills it in.
	std::vector<std::int8_t> m_screenless;
	bool m_screenlessScanned = false;

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
	std::vector<std::uint8_t> m_media;           // MediaFlag bitmask per row (ROM/CHD)

	// Optional column data filled in after construction (see setters above).
	QHash<QString, QString> m_genre;             // short name -> genre
	QHash<QString, QString> m_language;          // short name -> language
	std::vector<QString> m_control;              // per-row control label
	bool m_controlScanned = false;

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
	int m_iconDisplaySize = 0;   // px the views show icons at (0 = native)
	bool m_iconSmooth = false;   // smooth vs nearest-neighbour upscaling

	// Lazy, cached grid thumbnails for the selected image set (worker thread).
	// A generation counter invalidates in-flight loads when the source changes.
	ThumbnailLoader *m_thumbLoader = nullptr;
	QVector<QPair<QString, QString>> m_thumbChain;   // ordered (key, resolved path), primary first
	bool m_thumbFamily = true;                       // also try parent/sibling sets
	quint64 m_thumbGen = 0;
	mutable QHash<int, QPixmap> m_thumbCache;
	mutable QSet<int> m_thumbRequested;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_GAMELISTMODEL_H
