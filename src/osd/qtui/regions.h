// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  regions.h - region detection for clone "version" preference
//
//  MAME has no region field for arcade machines (only software lists, see
//  src/frontend/mame/ui/utils.cpp), so the region of a machine is inferred
//  heuristically from the parenthetical tokens in its description, e.g.
//  "... (World)", "... (US set 1)", "... (Japan ver. JAA)".
//
//============================================================
#ifndef MAME_OSD_QTUI_REGIONS_H
#define MAME_OSD_QTUI_REGIONS_H

#pragma once

#include <QtCore/QString>
#include <QtCore/QStringList>

namespace osd::qtui {

// Canonical region names in the default (US-first) priority order.
QStringList defaultRegionOrder();

// Canonical region inferred from a driver description ("" if none recognised).
QString extractRegion(const QString &description);

// The user's system region from QLocale, mapped to a canonical region
// ("" if it cannot be mapped to one we know about).
QString systemRegion();

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_REGIONS_H
