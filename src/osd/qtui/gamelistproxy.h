// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gamelistproxy.h - sorting/filtering proxy for the system list
//
//  Adds folder (category) filtering and free-text search on top of the
//  GameListModel, while still providing the column sorting of the base
//  QSortFilterProxyModel.
//
//============================================================
#ifndef MAME_OSD_QTUI_GAMELISTPROXY_H
#define MAME_OSD_QTUI_GAMELISTPROXY_H

#pragma once

#include <QtCore/QSet>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>

namespace osd::qtui {

//============================================================
//  FolderFilter - the structural category selected in the folder tree.
//
//  Status (working / availability) is intentionally NOT a folder; it is an
//  orthogonal modifier (see StatusFilter) applied on top of whatever folder
//  is selected.
//============================================================
struct FolderFilter
{
	enum Kind
	{
		All,            // every system
		Arcade,         // arcade machines
		Console,        // consoles / computers / other (non-arcade)
		Manufacturer,   // a specific manufacturer (value)
		Year,           // a specific release year (value)
		Category        // membership in `members` (from a .ini category)
	};

	Kind kind = All;
	QString value;            // used by Manufacturer / Year
	QSet<QString> members;    // used by Category (set of short names)
};


//============================================================
//  StatusFlag - combinable status modifiers applied to the current list.
//
//  Flags within a group (emulation: Working/NotWorking; availability:
//  Available/Unavailable) are OR'd together; the groups are AND'd.  An empty
//  group imposes no constraint.  This lets e.g. "Working" combine with
//  "Available" to show working *and* available systems.
//============================================================
enum StatusFlag
{
	StatusWorking     = 0x01,
	StatusNotWorking  = 0x02,
	StatusAvailable   = 0x04,   // wired in the audit phase
	StatusUnavailable = 0x08    // wired in the audit phase
};


class GameListProxy : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit GameListProxy(QObject *parent = nullptr);

	void setFolderFilter(const FolderFilter &filter);
	void setStatusFilter(int flags);   // bitwise OR of StatusFlag
	void setSearchText(const QString &text);

	// Version filters: hide clones (show only family representatives), and hide
	// bootleg/hack/prototype sets.
	void setHideClones(bool hide);
	void setHideBootlegs(bool hide);
	void setHideHacks(bool hide);
	void setHidePrototypes(bool hide);

	// System-type filters: hide mechanical systems (pinball/redemption/etc.) and
	// hide screenless systems.  Screenless verdicts come from a background scan;
	// rows with no verdict yet are not hidden.
	void setHideMechanical(bool hide);
	void setHideScreenless(bool hide);

	// Whether the system with this short name passes the version / system-type /
	// status filters, ignoring the selected folder and the search text.  Used to
	// decide whether a category still has any visible member.  Returns false for
	// a short name not present in the build.
	bool acceptsSystemAttributes(const QString &shortName) const;

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	// The version / system-type / status attribute checks shared by
	// filterAcceptsRow() and acceptsSystemAttributes() (folder + search excluded).
	bool acceptsAttributes(const QModelIndex &sourceIndex) const;

	FolderFilter m_filter;
	int m_status = 0;
	QString m_search;
	bool m_hideClones = false;
	bool m_hideBootlegs = false;
	bool m_hideHacks = false;
	bool m_hidePrototypes = false;
	bool m_hideMechanical = false;
	bool m_hideScreenless = false;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_GAMELISTPROXY_H
