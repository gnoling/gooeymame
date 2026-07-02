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
#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>
#include <QtGui/QIcon>
#include <QtGui/QPixmap>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace osd::qtui {

class IconLoader;
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
		// Optional columns (hidden by default; toggled from the list header's
		// context menu).  Appended to keep the indices above stable.
		COLUMN_CLONEOF,   // clone parent's short name ("" for parents)
		COLUMN_ROMS,      // ROM availability (Available / Missing)
		COLUMN_REGION,    // region inferred from the title
		COLUMN_COUNT
	};

	enum Role
	{
		// support level: 0 = supported, 1 = partial, 2 = unsupported
		SupportedRole = Qt::UserRole + 1,
		// ROM availability: matches qtui_availability (0/1/2)
		AvailabilityRole,
		// true if this item is a clone (has a parent in the same list)
		IsCloneRole,
		// true if this item is its clone family's representative
		IsRepresentativeRole,
		// canonical region inferred from the longname ("" if none)
		RegionRole,
		// VersionFlag bitmask (bootleg/hack/prototype)
		VersionFlagsRole
	};

	// Mirrors GameListModel: how a family's representative is chosen, and the
	// version classification bits.
	enum VersionMode { MatchParent = 0, PromoteRegion = 1 };
	enum VersionFlag { VersionBootleg = 0x1, VersionHack = 0x2, VersionPrototype = 0x4 };

	explicit SoftwareModel(QObject *parent = nullptr);

	void setEntries(std::vector<qtui_software_entry> entries);

	// Re-read versions/* and recompute representatives; emits versionsChanged().
	void reloadVersionSettings();

	// Clone-family queries (rows are model rows).
	bool isClone(int row) const;
	bool isRepresentative(int row) const;
	int representativeRow(int row) const;
	QList<int> familyMemberRows(int row) const;   // representative first
	QList<int> groupRows() const;                 // one representative row per family
	// Persist a per-family default-version override for the family of `row`.
	void setVersionOverride(int row, const QString &memberShortName);

	// Host machine for the current entries (used for thumbnail fallback art).
	void setHostSystem(const QString &system);

	// Choose the image sets for grid thumbnails: an ordered list of (software
	// _SL key, host-machine key) pairs, primary first, tried in turn so a
	// missing primary image falls back to other art types.  When familyFallback
	// is true the other family members' images are tried too.
	void setThumbnailSources(const QVector<QPair<QString, QString>> &keys, bool familyFallback);

	// One entry in the software row-icon source priority list.
	struct IconSourceKeySw { QString swKey; QString machineKey; bool native; };

	// Configure the software row-icon sources: art-type priority list, whether the
	// item's own artwork is preferred over the host machine's icon (else per-type
	// item→host, the original behavior), and whether family siblings are tried.
	void setIconSources(const QVector<IconSourceKeySw> &sources, bool preferOwn, bool family);

	// Pixel size the list/tree views display row icons at; icons are rescaled
	// to this since QIcon won't upscale a pixmap on its own.  Invalidates the
	// icon cache.
	void setIconDisplaySize(int px);

	// Whether enlarged icons are scaled smoothly (true) or with nearest-
	// neighbour (false, crisp pixel art).  Invalidates the icon cache.
	void setIconSmoothScaling(bool smooth);

	// Apply availability results (qtui_availability) indexed to match the
	// current entries, from the background audit phase.
	void setAvailabilities(const QVector<int> &availability);

	// Software short name for a row, e.g. "smb", or empty if out of range.
	QString shortNameForRow(int row) const;

	// Software list name for a row, e.g. "nes", or empty if out of range.
	QString listForRow(int row) const;

	// Clone parent short name for a row (cloneof), or empty if this is a parent.
	QString parentForRow(int row) const;

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

signals:
	void versionsChanged();

private slots:
	void onThumbnailLoaded(int row, quint64 generation, const QByteArray &bytes);
	void onIconLoaded(int row, const QByteArray &bytes);

private:
	QVariant thumbnailForRow(int row) const;
	QVariant iconForRow(int row) const;   // lazily requests/caches the row icon
	void invalidateIconCache();           // drop cached icons so they re-scale
	void buildFamilies();           // families + region + version flags
	void computeRepresentatives();  // (re)derive representatives from settings
	void applyVersionSettings();    // read versions/* + computeRepresentatives (no emit)
	QString familyKey(int rootRow) const;   // "list\x1froot" for overrides

	std::vector<qtui_software_entry> m_entries;

	// Clone families (mirrors GameListModel).
	std::vector<int> m_familyRoot;
	std::unordered_map<int, std::vector<int>> m_familyMembers;
	std::vector<int> m_representative;
	std::vector<QString> m_region;
	std::vector<std::uint8_t> m_versionFlags;
	int m_versionMode = MatchParent;
	bool m_useSystemRegion = false;
	QStringList m_regionOrder;
	QHash<QString, QString> m_overrides;   // "list\x1froot" -> member short name

	// Grid thumbnails: ordered art-type sources (each = software _SL path +
	// host-machine fallback path), primary first.
	struct ThumbSource { QString swPath; QString machinePath; QString swKey; QString machineKey; };
	ThumbnailLoader *m_thumbLoader = nullptr;
	QVector<ThumbSource> m_thumbChain;
	bool m_thumbFamily = true;
	QString m_hostSystem, m_hostParent;
	quint64 m_thumbGen = 0;
	mutable QHash<int, QPixmap> m_thumbCache;
	mutable QSet<int> m_thumbRequested;

	// Row icons: per-software icon if present, else the host machine's icon.
	IconLoader *m_iconLoader = nullptr;
	QString m_iconsPath;
	mutable QHash<int, QIcon> m_iconCache;
	mutable QSet<int> m_iconRequested;
	int m_iconDisplaySize = 0;   // px the views show icons at (0 = native)
	bool m_iconSmooth = false;   // smooth vs nearest-neighbour upscaling

	// Resolved row-icon source chain + fallback-order toggles (see setIconSources).
	struct IconSrcSw
	{
		QString swPath, machinePath, swKey, machineKey;
		bool native;
		bool operator==(const IconSrcSw &o) const
		{
			return swPath == o.swPath && machinePath == o.machinePath && swKey == o.swKey
					&& machineKey == o.machineKey && native == o.native;
		}
	};
	QVector<IconSrcSw> m_iconChain;
	bool m_iconPreferOwn = false;
	bool m_iconFamily = false;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_SOFTWAREMODEL_H
