// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareproxy.cpp - sorting/filtering proxy for the software list
//
//============================================================

#include "softwareproxy.h"

#include "naturalsort.h"
#include "softwaremodel.h"


namespace osd::qtui {

SoftwareProxy::SoftwareProxy(QObject *parent) :
	QSortFilterProxyModel(parent)
{
	setSortCaseSensitivity(Qt::CaseInsensitive);
	setSortLocaleAware(true);
}

bool SoftwareProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
	QVariant const l = left.data(sortRole());
	QVariant const r = right.data(sortRole());
	if (l.typeId() == QMetaType::QString && r.typeId() == QMetaType::QString)
		return naturalCompare(l.toString(), r.toString()) < 0;
	return QSortFilterProxyModel::lessThan(left, right);
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

void SoftwareProxy::setHideClones(bool hide)
{
	if (hide == m_hideClones)
		return;
	m_hideClones = hide;
	invalidateFilter();
}

void SoftwareProxy::setHideBootlegs(bool hide)
{
	if (hide == m_hideBootlegs)
		return;
	m_hideBootlegs = hide;
	invalidateFilter();
}

void SoftwareProxy::setHideHacks(bool hide)
{
	if (hide == m_hideHacks)
		return;
	m_hideHacks = hide;
	invalidateFilter();
}

void SoftwareProxy::setHidePrototypes(bool hide)
{
	if (hide == m_hidePrototypes)
		return;
	m_hidePrototypes = hide;
	invalidateFilter();
}

bool SoftwareProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	QAbstractItemModel *model = sourceModel();
	if (!model)
		return false;

	QModelIndex const index = model->index(sourceRow, 0, sourceParent);

	// Version filters (hide clones / bootlegs / hacks / prototypes).
	if (m_hideClones && !index.data(SoftwareModel::IsRepresentativeRole).toBool())
		return false;
	if (m_hideBootlegs || m_hideHacks || m_hidePrototypes)
	{
		int const flags = index.data(SoftwareModel::VersionFlagsRole).toInt();
		if (m_hideBootlegs && (flags & SoftwareModel::VersionBootleg))
			return false;
		if (m_hideHacks && (flags & SoftwareModel::VersionHack))
			return false;
		if (m_hidePrototypes && (flags & SoftwareModel::VersionPrototype))
			return false;
	}

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
