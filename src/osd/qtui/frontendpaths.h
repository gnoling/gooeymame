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

#include <QtCore/QHash>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <cstddef>

namespace osd::qtui {

// How a front-end folder entry is configured and consumed.
enum class FolderKind
{
	SearchPath,   // ordered list of folders and/or zip archives, searched in turn
	Dir,          // a single folder
	File          // a single file
};

// Which Options ▸ Front-end Folders tab an entry is shown on.
enum class FolderGroup
{
	MachineArt,   // per-machine image/video/sound sets
	SoftwareArt,  // per-software-list sets
	DataFiles     // category folders, history/info .dat files
};

struct FrontendFolder
{
	const char *key;      // QSettings key (under group "folders")
	const char *label;    // human-readable label
	FolderKind kind;
	FolderGroup group;
	const char *aliases;  // ';'-separated alternative basenames (file or folder
	                      // names, archive extension optional) recognised by
	                      // discoverFrontendFolders() in addition to `key`
};

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

// A folder value split on ';' into individual entries (empty parts dropped).
// SearchPath keys store their ordered list this way.
QStringList frontendFolderPathList(const QString &key);

// Every location to search for one art-type key, in priority order: the
// key's own ';'-separated folders or zip files.  All art lookups (machine and
// software alike) resolve through this, so any tool that generates the
// EXTRAs-style layout (<key>/<machine>.png, <key>/<list>/<software>.png)
// plugs in by adding its output folder to the relevant keys.
QStringList artSearchPaths(const QString &key);

// Look through one media root (a MAME EXTRAs download, a Multimedia pack, an
// optimize-sl-media.py / import-launchbox-media.py output tree, ...) for
// entries matching each front-end folder key or its aliases, case-insensitively:
// a folder or .zip/.7z archive named after the key for SearchPath/Dir kinds,
// and a file named after an alias (directly under the root or one subfolder
// down, e.g. dats/history.xml) for File kinds.  Returns key -> absolute path for
// every key that matched.  Used by the Options auto-fill and by the one-time
// secondary-root migration.
QHash<QString, QString> discoverFrontendFolders(const QString &root);

// One-time settings upgrade, run at startup: fold the retired
// "folders/secondaryRoot" list into the per-key search paths (appending
// <root>/<key> for every key that exists under each root) and remove the key.
void migrateFrontendFolders();

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_FRONTENDPATHS_H
