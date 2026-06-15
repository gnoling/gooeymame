// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  foldertree.h - category tree for filtering the system list
//
//  Mirrors a subset of MAMEUI's treeview categories (All / Working /
//  Not Working / Arcade / Computer & Console / Manufacturer / Year).
//  Selecting a node emits a FolderFilter consumed by GameListProxy.
//
//============================================================
#ifndef MAME_OSD_QTUI_FOLDERTREE_H
#define MAME_OSD_QTUI_FOLDERTREE_H

#pragma once

#include "gamelistproxy.h"

#include <QtWidgets/QTreeWidget>

namespace osd::qtui {

class GameListModel;

class FolderTree : public QTreeWidget
{
	Q_OBJECT

public:
	explicit FolderTree(GameListModel *model, QWidget *parent = nullptr);

signals:
	void folderSelected(const FolderFilter &filter);

private slots:
	void onItemSelectionChanged();

private:
	QTreeWidgetItem *addFolder(QTreeWidgetItem *parent, const QString &label,
			FolderFilter::Kind kind, const QString &value = QString());
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_FOLDERTREE_H
