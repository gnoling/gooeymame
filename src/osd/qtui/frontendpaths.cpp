// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  frontendpaths.cpp - front-end-specific asset folder configuration
//
//============================================================

#include "frontendpaths.h"

#include <QtCore/QSettings>


namespace osd::qtui {

// The image-set directories and data files shipped in MAME EXTRAs that have
// no mame.ini equivalent.  (Snapshots and artwork use mame.ini's
// snapshot_directory / artpath and are edited on the options tabs.)
const FrontendFolder FRONTEND_FOLDERS[] =
{
	{ "snap",       "Snapshots",            false },
	{ "snap_sl",    "Software snapshots",   false },
	{ "titles",     "Title screens",        false },
	{ "titles_sl",  "Software titles",      false },
	{ "cabinets",   "Cabinets",             false },
	{ "cpanel",     "Control panels",       false },
	{ "marquees",   "Marquees",             false },
	{ "flyers",     "Flyers",               false },
	{ "pcb",        "PCBs",                 false },
	{ "bosses",     "Bosses",               false },
	{ "covers",     "Covers",               false },
	{ "icons",      "Icons",                false },
	{ "logos",      "Logos",                false },
	{ "scores",     "High scores",          false },
	{ "select",     "Select screens",       false },
	{ "versus",     "Versus screens",       false },
	{ "gameover",   "Game over screens",    false },
	{ "howto",      "How-to screens",       false },
	{ "artpreview", "Artwork previews",     false },
	{ "categories", "Category folders (folder of .ini files)", false },
	{ "history",    "History (history.xml)", true },
	{ "mameinfo",   "MAME info (mameinfo.dat)", true },
	{ "messinfo",   "MESS info (messinfo.dat)", true },
	{ "command",    "Command list (command.dat)", true },
};

const std::size_t FRONTEND_FOLDER_COUNT = sizeof(FRONTEND_FOLDERS) / sizeof(FRONTEND_FOLDERS[0]);

QString frontendFolderPath(const QString &key)
{
	QSettings settings;
	return settings.value(QStringLiteral("folders/") + key).toString();
}

void setFrontendFolderPath(const QString &key, const QString &path)
{
	QSettings settings;
	settings.setValue(QStringLiteral("folders/") + key, path);
}

} // namespace osd::qtui
