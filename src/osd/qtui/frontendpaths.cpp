// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  frontendpaths.cpp - front-end-specific asset folder configuration
//
//============================================================

#include "frontendpaths.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>


namespace osd::qtui {

// The image-set directories and data files shipped in MAME EXTRAs that have
// no mame.ini equivalent.  (Snapshots and artwork use mame.ini's
// snapshot_directory / artpath and are edited on the options tabs.)
//
// Aliases list the other names these sets go by in common downloads
// (MAME EXTRAs: snap_SL.zip, covers_SL.zip, logo.zip, dats/*.dat, folders/;
// the Multimedia pack: videosnaps_SL, soundtrack) so an auto-fill from a root
// folder finds them without the user renaming anything.
#define SP FolderKind::SearchPath
#define DR FolderKind::Dir
#define FL FolderKind::File
#define MCH FolderGroup::MachineArt
#define SWA FolderGroup::SoftwareArt
#define DAT FolderGroup::DataFiles
const FrontendFolder FRONTEND_FOLDERS[] =
{
	{ "snap",       "Snapshots",            SP, MCH, "" },
	{ "snap_sl",    "Software snapshots",   SP, SWA, "snap_SL" },
	{ "titles",     "Title screens",        SP, MCH, "" },
	{ "titles_sl",  "Software titles",      SP, SWA, "titles_SL" },
	{ "cabinets",   "Cabinets",             SP, MCH, "" },
	{ "cpanel",     "Control panels",       SP, MCH, "" },
	{ "marquees",   "Marquees",             SP, MCH, "" },
	{ "flyers",     "Flyers",               SP, MCH, "" },
	{ "pcb",        "PCBs",                 SP, MCH, "" },
	{ "bosses",     "Bosses",               SP, MCH, "" },
	{ "covers",     "Covers",               SP, SWA, "covers_SL" },
	{ "icons",      "Icons",                SP, MCH, "" },
	{ "logos",      "Logos",                SP, MCH, "logo" },
	{ "scores",     "High scores",          SP, MCH, "" },
	{ "select",     "Select screens",       SP, MCH, "" },
	{ "versus",     "Versus screens",       SP, MCH, "" },
	{ "gameover",   "Game over screens",    SP, MCH, "" },
	{ "howto",      "How-to screens",       SP, MCH, "" },
	{ "ends",       "End screens",          SP, MCH, "" },
	{ "warning",    "Warning screens",      SP, MCH, "" },
	{ "devices",    "Device images",        SP, MCH, "" },
	{ "artpreview", "Artwork previews",     SP, MCH, "" },
	{ "videosnaps",    "Video snaps",            SP, MCH, "" },
	{ "videosnaps_sl", "Software video snaps",   SP, SWA, "videosnaps_SL" },
	{ "soundtrack",    "Soundtracks (folder per machine)", SP, MCH, "" },
	{ "categories", "Category folders (folder of .ini files)", DR, DAT, "folders" },
	{ "history",    "History (history.xml)", FL, DAT, "history.xml" },
	{ "mameinfo",   "MAME info (mameinfo.dat)", FL, DAT, "mameinfo.dat" },
	{ "messinfo",   "MESS info (messinfo.dat)", FL, DAT, "messinfo.dat" },
	{ "command",    "Command list (command.dat)", FL, DAT, "command.dat" },
	{ "gameinit",   "Game init (gameinit.dat)", FL, DAT, "gameinit.dat" },
	{ "sysinfo",    "System info (sysinfo.dat)", FL, DAT, "sysinfo.dat" },
	{ "story",      "Story (story.dat)",     FL, DAT, "story.dat" },
	{ "topscores",  "Top scores (MARP scores3.htm)", FL, DAT, "scores3.htm" },
	{ "manuals",    "Manuals (folder or .zip of PDFs)",        SP, MCH, "" },
	{ "manuals_sl", "Software manuals (folder or .zip of PDFs)", SP, SWA, "manuals_SL" },
	// Extended software-list art sets (tools/optimize-sl-media.py and
	// tools/import-launchbox-media.py output, laid out <root>/<key>/<list>/<sw>.ext).
	{ "box_sl",       "Software box (2D front)",   SP, SWA, "" },
	{ "box3d_sl",     "Software box (3D)",         SP, SWA, "" },
	{ "boxback_sl",   "Software box (back)",       SP, SWA, "" },
	{ "boxfull_sl",   "Software box (full)",       SP, SWA, "" },
	{ "boxspine_sl",  "Software box (spine)",      SP, SWA, "" },
	{ "cart_sl",      "Software cartridge",        SP, SWA, "" },
	{ "cart3d_sl",    "Software cartridge (3D)",   SP, SWA, "" },
	{ "carttop_sl",   "Software cartridge (top)",  SP, SWA, "" },
	{ "background_sl", "Software backgrounds",     SP, SWA, "" },
	{ "logos_sl",     "Software logos",            SP, SWA, "" },
	{ "banner_sl",    "Software banners",          SP, SWA, "" },
	{ "marquees_sl",  "Software marquees",         SP, SWA, "" },
	{ "advertimg_sl", "Software advert images",    SP, SWA, "" },
	{ "advert_sl",    "Software advert videos",    SP, SWA, "" },
	{ "maps_sl",      "Software maps (folder of PDFs)", SP, SWA, "" },
	{ "music_sl",     "Software music",            SP, SWA, "" },
};
#undef SP
#undef DR
#undef FL
#undef MCH
#undef SWA
#undef DAT

const std::size_t FRONTEND_FOLDER_COUNT = sizeof(FRONTEND_FOLDERS) / sizeof(FRONTEND_FOLDERS[0]);

const ThumbnailSource THUMBNAIL_SOURCES[] =
{
	{ "Snapshot", "snap",       "snap_sl"   },
	{ "Title",    "titles",     "titles_sl" },
	{ "Cover",    "",           "covers"    },
	{ "Flyer",    "flyers",     ""          },
	{ "Cabinet",  "cabinets",   ""          },
	{ "Marquee",  "marquees",   "marquees_sl" },
	{ "Artwork",  "artpreview", "artpreview"},
	// Extended software-list art sets (optimize-sl-media.py / LaunchBox import).
	{ "Box",      "",           "box_sl"    },
	{ "Box 3D",   "",           "box3d_sl"  },
	{ "Cart",     "",           "cart_sl"   },
	{ "Logo",     "logos",      "logos_sl"  },
	{ "Banner",   "",           "banner_sl" },
};

const std::size_t THUMBNAIL_SOURCE_COUNT = sizeof(THUMBNAIL_SOURCES) / sizeof(THUMBNAIL_SOURCES[0]);

const IconSourceType ICON_SOURCES[] =
{
	{ "icon",    "Icon",     "",           "",           true  },
	{ "snap",    "Snapshot", "snap",       "snap_sl",    false },
	{ "title",   "Title",    "titles",     "titles_sl",  false },
	{ "cover",   "Cover",    "",           "covers",     false },
	{ "cart",    "Cart",     "",           "cart_sl",    false },
	{ "box",     "Box",      "",           "box_sl",     false },
	{ "box3d",   "Box 3D",   "",           "box3d_sl",   false },
	{ "flyer",   "Flyer",    "flyers",     "",           false },
	{ "marquee", "Marquee",  "marquees",   "marquees_sl",false },
	{ "logo",    "Logo",     "logos",      "logos_sl",   false },
	{ "cabinet", "Cabinet",  "cabinets",   "",           false },
	{ "artwork", "Artwork",  "artpreview", "artpreview", false },
	{ "banner",  "Banner",   "",           "banner_sl",  false },
};

const std::size_t ICON_SOURCE_COUNT = sizeof(ICON_SOURCES) / sizeof(ICON_SOURCES[0]);

QString frontendFolderPath(const QString &key)
{
	QSettings settings;
	return settings.value(QStringLiteral("folders/") + key).toString();
}

QStringList frontendFolderPathList(const QString &key)
{
	QStringList out;
	for (const QString &part : frontendFolderPath(key).split(QLatin1Char(';')))
	{
		QString const dir = part.trimmed();
		if (!dir.isEmpty())
			out.append(dir);
	}
	return out;
}

QStringList artSearchPaths(const QString &key)
{
	if (key.isEmpty())
		return QStringList();
	return frontendFolderPathList(key);
}

namespace {

bool isArchiveName(const QString &name)
{
	return name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)
			|| name.endsWith(QLatin1String(".7z"), Qt::CaseInsensitive);
}

// Lower-cased lookup name for a directory entry: folders and archives drop
// the archive extension (so "snap_SL.zip" and "snap_SL/" both key as
// "snap_sl"); plain files keep their full name ("history.xml").
QString lookupName(const QFileInfo &info)
{
	QString name = info.fileName();
	if (!info.isDir() && isArchiveName(name))
		name.truncate(name.lastIndexOf(QLatin1Char('.')));
	return name.toLower();
}

// Index one directory: lookup name -> absolute path.  Folders are listed
// first so a folder beats a same-named archive.
void indexDirectory(const QDir &dir, QHash<QString, QString> &sets, QHash<QString, QString> &files)
{
	QFileInfoList const entries = dir.entryInfoList(
			QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
	for (const QFileInfo &info : entries)
	{
		QString const name = lookupName(info);
		if (info.isDir() || isArchiveName(info.fileName()))
		{
			if (!sets.contains(name))
				sets.insert(name, info.absoluteFilePath());
		}
		else if (!files.contains(name))
		{
			files.insert(name, info.absoluteFilePath());
		}
	}
}

} // anonymous namespace

QHash<QString, QString> discoverFrontendFolders(const QString &root)
{
	QHash<QString, QString> found;
	QDir const dir(root);
	if (root.isEmpty() || !dir.exists())
		return found;

	// Folders/archives directly under the root; files under the root and one
	// level down (EXTRAs keeps its .dat files in dats/ and history.xml in history/).
	QHash<QString, QString> sets, files;
	indexDirectory(dir, sets, files);
	QHash<QString, QString> subSets;
	for (const QFileInfo &sub : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		indexDirectory(QDir(sub.absoluteFilePath()), subSets, files);

	for (std::size_t i = 0; i < FRONTEND_FOLDER_COUNT; i++)
	{
		const FrontendFolder &folder = FRONTEND_FOLDERS[i];
		QString const key = QString::fromLatin1(folder.key);
		QStringList names;
		if (folder.kind != FolderKind::File)
			names.append(key);
		for (const QString &alias : QString::fromLatin1(folder.aliases).split(QLatin1Char(';'), Qt::SkipEmptyParts))
			names.append(alias);

		for (const QString &candidate : names)
		{
			QString name = candidate.toLower();
			if (folder.kind != FolderKind::File && isArchiveName(name))
				name.truncate(name.lastIndexOf(QLatin1Char('.')));
			const QHash<QString, QString> &table = (folder.kind == FolderKind::File) ? files : sets;
			auto const it = table.constFind(name);
			if (it != table.constEnd())
			{
				found.insert(key, it.value());
				break;
			}
		}
	}
	return found;
}

void migrateFrontendFolders()
{
	QSettings settings;
	QString const rootKey = QStringLiteral("folders/secondaryRoot");
	if (!settings.contains(rootKey))
		return;

	// A root that cannot be seen right now (a network share that is not
	// mounted, a per-session mapped drive invisible to a headless/SSH run)
	// is kept for a later launch instead of being silently dropped.
	QStringList deferred;
	for (const QString &root : frontendFolderPathList(QStringLiteral("secondaryRoot")))
	{
		if (!QDir(root).exists())
		{
			deferred.append(root);
			continue;
		}
		QHash<QString, QString> const found = discoverFrontendFolders(root);
		for (std::size_t i = 0; i < FRONTEND_FOLDER_COUNT; i++)
		{
			const FrontendFolder &folder = FRONTEND_FOLDERS[i];
			if (folder.kind != FolderKind::SearchPath)
				continue;
			QString const key = QString::fromLatin1(folder.key);
			auto const it = found.constFind(key);
			if (it == found.constEnd())
				continue;
			QStringList paths = frontendFolderPathList(key);
			if (!paths.contains(it.value()))
			{
				paths.append(it.value());
				setFrontendFolderPath(key, paths.join(QLatin1Char(';')));
			}
		}
	}
	if (deferred.isEmpty())
		settings.remove(rootKey);
	else
		settings.setValue(rootKey, deferred.join(QLatin1Char(';')));
}

void setFrontendFolderPath(const QString &key, const QString &path)
{
	QSettings settings;
	settings.setValue(QStringLiteral("folders/") + key, path);
}

} // namespace osd::qtui
