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
#include <QtCore/QStringList>

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

// Image types offered as grid/thumbnail sources.  machineKey/softwareKey are
// frontendpaths folder keys ("" = not available for that scope; software then
// falls back to the host machine's image).
struct ThumbnailSource
{
	const char *label;
	const char *machineKey;
	const char *softwareKey;
};

extern const ThumbnailSource THUMBNAIL_SOURCES[];
extern const std::size_t THUMBNAIL_SOURCE_COUNT;

// Art types offered as row-icon sources (an independent priority list from the
// grid thumbnails).  "native" = the icons.zip/.ico set (uses the "icons" folder
// path, not machineKey/softwareKey); the rest are PNG art folders.
struct IconSourceType
{
	const char *id;          // stable QSettings id
	const char *label;       // human-readable label
	const char *machineKey;  // frontendpaths key for machine art ("" = n/a)
	const char *softwareKey; // frontendpaths key for software art ("" = n/a)
	bool native;             // true = icons.zip (.ico), resolved via the "icons" folder
};

extern const IconSourceType ICON_SOURCES[];
extern const std::size_t ICON_SOURCE_COUNT;

// Read/write a configured front-end folder path (empty if unset).
QString frontendFolderPath(const QString &key);
void setFrontendFolderPath(const QString &key, const QString &path);

// A folder value split on ';' into individual roots (empty parts dropped).
// Used by keys that accept several directories, e.g. secondaryRoot.
QStringList frontendFolderPathList(const QString &key);

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_FRONTENDPATHS_H
