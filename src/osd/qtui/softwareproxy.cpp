// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareproxy.cpp - sorting/filtering proxy for the software list
//
//============================================================

#include "softwareproxy.h"

#include "softwaremodel.h"


namespace osd::qtui {

SoftwareProxy::SoftwareProxy(QObject *parent) :
	QSortFilterProxyModel(parent)
{
	setSortCaseSensitivity(Qt::CaseInsensitive);
	setSortLocaleAware(true);
}

void SoftwareProxy::setSearchText(const QString &text)
{
	QString const trimmed = text.trimmed();
	if (trimmed == m_search)
		return;
	m_search = trimmed;
	invalidateFilter();
}

void SoftwareProxy::setSupportFilter(int flags)
{
	if (flags == m_support)
		return;
	m_support = flags;
	invalidateFilter();
}

void SoftwareProxy::setAvailabilityFilter(int flags)
{
	if (flags == m_avail)
		return;
	m_avail = flags;
	invalidateFilter();
}

bool SoftwareProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	QAbstractItemModel *model = sourceModel();
	if (!model)
		return false;

	QModelIndex const index = model->index(sourceRow, 0, sourceParent);

	// Support-level quick filter (OR within the group).
	if (m_support)
	{
		int const supported = index.data(SoftwareModel::SupportedRole).toInt();
		int const bit = (supported == 0) ? SwSupported
				: (supported == 1) ? SwPartial
				: SwUnsupported;
		if (!(m_support & bit))
			return false;
	}

	// Availability quick filter (OR within the group).
	if (m_avail)
	{
		int const avail = index.data(SoftwareModel::AvailabilityRole).toInt();
		int const bit = (avail == 1 /* available */) ? SwAvailable : SwUnavailable;
		if (!(m_avail & bit))
			return false;
	}

	// Free-text search across every column.
	if (!m_search.isEmpty())
	{
		bool match = false;
		for (int col = 0; col < model->columnCount(sourceParent) && !match; col++)
		{
			QModelIndex const cell = model->index(sourceRow, col, sourceParent);
			if (cell.data(Qt::DisplayRole).toString().contains(m_search, Qt::CaseInsensitive))
				match = true;
		}
		if (!match)
			return false;
	}

	return true;
}

} // namespace osd::qtui
