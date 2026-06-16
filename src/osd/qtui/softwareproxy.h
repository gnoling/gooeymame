// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  softwareproxy.h - sorting/filtering proxy for the software list
//
//  Adds free-text search and support-level quick filters on top of the
//  SoftwareModel.
//
//============================================================
#ifndef MAME_OSD_QTUI_SOFTWAREPROXY_H
#define MAME_OSD_QTUI_SOFTWAREPROXY_H

#pragma once

#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QString>

namespace osd::qtui {

// Support-level quick-filter flags (OR'd; empty = no constraint).
enum SoftwareSupportFlag
{
	SwSupported   = 0x01,
	SwPartial     = 0x02,
	SwUnsupported = 0x04
};

// Availability quick-filter flags (OR'd; empty = no constraint).
enum SoftwareAvailFlag
{
	SwAvailable   = 0x01,
	SwUnavailable = 0x02
};


class SoftwareProxy : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	explicit SoftwareProxy(QObject *parent = nullptr);

	void setSearchText(const QString &text);
	void setSupportFilter(int flags);        // bitwise OR of SoftwareSupportFlag
	void setAvailabilityFilter(int flags);   // bitwise OR of SoftwareAvailFlag

	// Version filters, mirroring the machine list.
	void setHideClones(bool hide);
	void setHideBootlegs(bool hide);
	void setHideHacks(bool hide);
	void setHidePrototypes(bool hide);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
	QString m_search;
	int m_support = 0;
	int m_avail = 0;
	bool m_hideClones = false;
	bool m_hideBootlegs = false;
	bool m_hideHacks = false;
	bool m_hidePrototypes = false;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_SOFTWAREPROXY_H
