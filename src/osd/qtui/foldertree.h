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

#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtWidgets/QTreeWidget>

#include <vector>

namespace osd::qtui {

class GameListModel;

class FolderTree : public QTreeWidget
{
	Q_OBJECT

public:
	explicit FolderTree(GameListModel *model, QWidget *parent = nullptr);

	// Stable identifier of the current node (its label path), and re-selection
	// by that identifier, for persisting the selected folder across sessions.
	QString currentPath() const;
	void selectPath(const QString &path);

	// Persist/restore which top-level sections are expanded (the labels of the
	// expanded parent nodes), so the tree's section layout survives a restart.
	QStringList expandedSections() const;
	void setExpandedSections(const QStringList &labels);

signals:
	void folderSelected(const FolderFilter &filter);

private slots:
	void onItemSelectionChanged();

private:
	QTreeWidgetItem *addFolder(QTreeWidgetItem *parent, const QString &label,
			FolderFilter::Kind kind, const QString &value = QString());

	// Build the category subtrees (Category/Genre/Series/...) from the
	// configured folder of MAME EXTRAs .ini files.  Section names are split
	// on ':' and '/' into a nested hierarchy.
	void loadCategories();
	void addCategoryIni(const QString &dir, const QString &file, const QString &title);

	// Union the member sets of an item and all of its descendants, so that
	// selecting a parent category shows everything beneath it.
	void collectMembers(const QTreeWidgetItem *item, QSet<QString> &out) const;

	// Recursively drop category nodes none of whose members are present in the
	// build; returns true if the item should be kept.  `model` is the system set.
	bool pruneEmpty(QTreeWidgetItem *item, const GameListModel *model);

	// Member sets for category nodes, referenced by index stored on the item.
	std::vector<QSet<QString>> m_categorySets;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_FOLDERTREE_H
