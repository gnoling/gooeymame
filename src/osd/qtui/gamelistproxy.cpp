// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gamelistproxy.cpp - sorting/filtering proxy for the system list
//
//============================================================

#include "gamelistproxy.h"

#include "gamelistmodel.h"
#include "naturalsort.h"


namespace osd::qtui {

GameListProxy::GameListProxy(QObject *parent) :
	QSortFilterProxyModel(parent)
{
	setSortCaseSensitivity(Qt::CaseInsensitive);
	setSortLocaleAware(true);
}

bool GameListProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	QVariant const l = left.data(sortRole());
	QVariant const r = right.data(sortRole());
	if (l.typeId() == QMetaType::QString && r.typeId() == QMetaType::QString)
		return naturalCompare(l.toString(), r.toString()) < 0;
	return QSortFilterProxyModel::lessThan(left, right);
}

void GameListProxy::setFolderFilter(const FolderFilter &filter)
{
	m_filter = filter;
	invalidateFilter();
}

void GameListProxy::setStatusFilter(int flags)
{
	if (flags == m_status)
		return;
	m_status = flags;
	invalidateFilter();
}

void GameListProxy::setSearchText(const QString &text)
{
	QString const trimmed = text.trimmed();
	if (trimmed == m_search)
		return;
	m_search = trimmed;
	invalidateFilter();
}

void GameListProxy::setHideClones(bool hide)
{
	if (hide == m_hideClones)
		return;
	m_hideClones = hide;
	invalidateFilter();
}

void GameListProxy::setHideBootlegs(bool hide)
{
	if (hide == m_hideBootlegs)
		return;
	m_hideBootlegs = hide;
	invalidateFilter();
}

void GameListProxy::setHideHacks(bool hide)
{
	if (hide == m_hideHacks)
		return;
	m_hideHacks = hide;
	invalidateFilter();
}

void GameListProxy::setHidePrototypes(bool hide)
{
	if (hide == m_hidePrototypes)
		return;
	m_hidePrototypes = hide;
	invalidateFilter();
}

void GameListProxy::setHideMechanical(bool hide)
{
	if (hide == m_hideMechanical)
		return;
	m_hideMechanical = hide;
	invalidateFilter();
}

void GameListProxy::setHideScreenless(bool hide)
{
	if (hide == m_hideScreenless)
		return;
	m_hideScreenless = hide;
	invalidateFilter();
}

bool GameListProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	QAbstractItemModel *model = sourceModel();
	if (!model)
		return false;

	QModelIndex const index = model->index(sourceRow, 0, sourceParent);

	// Structural folder/category filter.
	switch (m_filter.kind)
	{
	case FolderFilter::All:
		break;
	case FolderFilter::Arcade:
		if (!index.data(GameListModel::ArcadeRole).toBool())
			return false;
		break;
	case FolderFilter::Console:
		if (index.data(GameListModel::ArcadeRole).toBool())
			return false;
		break;
	case FolderFilter::Manufacturer:
		if (index.data(GameListModel::ManufacturerRole).toString() != m_filter.value)
			return false;
		break;
	case FolderFilter::Year:
		if (index.data(GameListModel::YearRole).toString() != m_filter.value)
			return false;
		break;
	case FolderFilter::Category:
	{
		// A clone inherits its parent's category if not listed itself.
		if (!m_filter.members.contains(index.data(GameListModel::ShortNameRole).toString()))
		{
			QString const parent = index.data(GameListModel::ParentNameRole).toString();
			if (parent.isEmpty() || !m_filter.members.contains(parent))
				return false;
		}
		break;
	}
	}

	// Version filters: hide clones (keep only family representatives) and hide
	// bootleg/hack/prototype sets.
	if (m_hideClones && !index.data(GameListModel::IsRepresentativeRole).toBool())
		return false;
	if (m_hideBootlegs || m_hideHacks || m_hidePrototypes)
	{
		int const flags = index.data(GameListModel::VersionFlagsRole).toInt();
		if (m_hideBootlegs && (flags & GameListModel::VersionBootleg))
			return false;
		if (m_hideHacks && (flags & GameListModel::VersionHack))
			return false;
		if (m_hidePrototypes && (flags & GameListModel::VersionPrototype))
			return false;
	}

	// System-type filters.  Mechanical is a static flag; screenless comes from a
	// background scan, so only hide rows with a definite NoScreen verdict (rows
	// still ScreenlessUnknown stay visible until the scan fills them in).
	if (m_hideMechanical && index.data(GameListModel::IsMechanicalRole).toBool())
		return false;
	if (m_hideScreenless
			&& index.data(GameListModel::IsScreenlessRole).toInt() == GameListModel::NoScreen)
		return false;

	// Emulation-status modifier (orthogonal to the folder).  OR within the
	// group; an empty group imposes no constraint.
	int const emuGroup = m_status & (StatusWorking | StatusNotWorking);
	if (emuGroup)
	{
		bool const working = index.data(GameListModel::WorkingRole).toBool();
		bool const accept = (working && (emuGroup & StatusWorking)) ||
				(!working && (emuGroup & StatusNotWorking));
		if (!accept)
			return false;
	}

	// Availability modifier (AND'd with the emulation group).  OR within the
	// group; Unknown availability matches neither toggle.
	int const availGroup = m_status & (StatusAvailable | StatusUnavailable);
	if (availGroup)
	{
		int const avail = index.data(GameListModel::AvailabilityRole).toInt();
		bool const accept =
				((avail == GameListModel::Available) && (availGroup & StatusAvailable)) ||
				((avail == GameListModel::Unavailable) && (availGroup & StatusUnavailable));
		if (!accept)
			return false;
	}

	// Free-text search across description, short name and manufacturer.
	if (!m_search.isEmpty())
	{
		QModelIndex const descIndex = model->index(sourceRow, GameListModel::COLUMN_DESCRIPTION, sourceParent);
		QModelIndex const mfrIndex = model->index(sourceRow, GameListModel::COLUMN_MANUFACTURER, sourceParent);

		bool const match =
				descIndex.data(Qt::DisplayRole).toString().contains(m_search, Qt::CaseInsensitive) ||
				index.data(GameListModel::ShortNameRole).toString().contains(m_search, Qt::CaseInsensitive) ||
				mfrIndex.data(Qt::DisplayRole).toString().contains(m_search, Qt::CaseInsensitive);

		if (!match)
			return false;
	}

	return true;
}

} // namespace osd::qtui
