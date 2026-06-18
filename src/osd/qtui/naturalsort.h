// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  naturalsort.h - natural ("version") string comparison for the list proxies
//
//  QString's locale-aware compare orders "foo10" before "foo2"; a natural
//  compare treats embedded numbers numerically so "foo2" < "foo10", which is
//  what users expect for game/version lists.  Shared by every list proxy's
//  lessThan() so all views sort consistently.
//
//============================================================
#ifndef MAME_OSD_QTUI_NATURALSORT_H
#define MAME_OSD_QTUI_NATURALSORT_H

#pragma once

#include <QtCore/QCollator>
#include <QtCore/QString>

namespace osd::qtui {

// Case-insensitive, numeric-aware ("natural"/version) comparison.
// Returns <0, 0 or >0 like QString::compare.  The collator is built once per
// thread (construction is relatively expensive); sorting runs on the GUI thread.
inline int naturalCompare(const QString &a, const QString &b)
{
	static thread_local QCollator collator = [] {
		QCollator c;
		c.setNumericMode(true);
		c.setCaseSensitivity(Qt::CaseInsensitive);
		return c;
	}();
	return collator.compare(a, b);
}

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_NATURALSORT_H
