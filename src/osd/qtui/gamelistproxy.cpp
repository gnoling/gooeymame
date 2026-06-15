// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gamelistproxy.cpp - sorting/filtering proxy for the system list
//
//============================================================

#include "gamelistproxy.h"

#include "gamelistmodel.h"


namespace osd::qtui {

GameListProxy::GameListProxy(QObject *parent) :
	QSortFilterProxyModel(parent)
{
	setSortCaseSensitivity(Qt::CaseInsensitive);
	setSortLocaleAware(true);
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
	}

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
