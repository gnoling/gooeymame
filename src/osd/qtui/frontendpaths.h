// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  frontendpaths.h - front-end-specific asset folder configuration
//
//  These are the MAME EXTRAs the front-end will use in later phases
//  (snapshots/artwork live in mame.ini, but the per-image-type sets and
//  the data files do not).  Paths are stored with QSettings so they are
//  configured now and consumed by future screenshot/artwork/history work.
//
//============================================================
#ifndef MAME_OSD_QTUI_FRONTENDPATHS_H
#define MAME_OSD_QTUI_FRONTENDPATHS_H

#pragma once

#include <QtCore/QString>

#include <cstddef>

namespace osd::qtui {

struct FrontendFolder
{
	const char *key;    // QSettings key (under group "folders")
	const char *label;  // human-readable label
	bool isFile;        // true = pick a file, false = pick a directory
};

// Curated list of front-end asset folders/files (MAME EXTRAs).
extern const FrontendFolder FRONTEND_FOLDERS[];
extern const std::size_t FRONTEND_FOLDER_COUNT;

// Read/write a configured front-end folder path (empty if unset).
QString frontendFolderPath(const QString &key);
void setFrontendFolderPath(const QString &key, const QString &path);

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_FRONTENDPATHS_H
