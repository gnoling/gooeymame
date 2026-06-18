// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  familytreemodel.h - tree wrapper grouping clone families
//
//  Presents a flat list/software model as a two-level tree: one top-level row
//  per clone family (the representative) with the family's other members as
//  expandable children.  All data/flags are delegated to the source model, so
//  the same roles, icons, and thumbnails work unchanged.
//
//  TreeFilterProxy keeps the tree in sync with the flat list's filters: a tree
//  node is shown iff the matching source row is accepted by the flat proxy, so
//  folder/search/status/version filters need no duplication.
//
//============================================================
#ifndef MAME_OSD_QTUI_FAMILYTREEMODEL_H
#define MAME_OSD_QTUI_FAMILYTREEMODEL_H

#pragma once

#include <QtCore/QAbstractItemModel>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QSortFilterProxyModel>

#include <functional>
#include <vector>

namespace osd::qtui {

class FamilyTreeModel : public QAbstractItemModel
{
	Q_OBJECT

public:
	using GroupsFn = std::function<QList<int>()>;        // representative source rows
	using ChildrenFn = std::function<QList<int>(int)>;   // child source rows of a rep

	FamilyTreeModel(QAbstractItemModel *source, GroupsFn groups, ChildrenFn children,
			QObject *parent = nullptr);

	void rebuild();   // recompute the grouping (after a reset or version change)

	// Source-model row for a tree index, or -1.
	int sourceRow(const QModelIndex &index) const;
	// Tree index (top-level or child) for a source row, or invalid.
	QModelIndex indexForSourceRow(int sourceRow) const;

	QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
	QModelIndex parent(const QModelIndex &index) const override;
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
	static constexpr quintptr TOP = quintptr(-1);   // internalId for top-level rows

	QAbstractItemModel *m_source = nullptr;
	GroupsFn m_groupsFn;
	ChildrenFn m_childrenFn;

	std::vector<int> m_groups;                 // top-level representative source rows
	std::vector<std::vector<int>> m_children;  // children per group
	QHash<int, QPair<int, int>> m_rowToPos;    // source row -> (group, child index or -1)
};


// Filters the tree by deferring to a flat proxy's acceptance of the matching
// source row, so every flat filter applies to the tree with no duplication.
class TreeFilterProxy : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	TreeFilterProxy(FamilyTreeModel *tree, QSortFilterProxyModel *flatProxy,
			QAbstractItemModel *flatModel, QObject *parent = nullptr);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	FamilyTreeModel *m_tree = nullptr;
	QSortFilterProxyModel *m_flatProxy = nullptr;
	QAbstractItemModel *m_flatModel = nullptr;
};


// Flat proxy that shows only clone-family representatives (one row per family),
// deferring the rest of the filtering to a flat proxy's acceptance — used by
// the grid views so each family collapses to a single tile.
class RepresentativeProxy : public QSortFilterProxyModel
{
	Q_OBJECT

public:
	RepresentativeProxy(QAbstractItemModel *source, QSortFilterProxyModel *flatProxy,
			std::function<bool(int)> isRepresentative, QObject *parent = nullptr);

protected:
	bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
	bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
	QAbstractItemModel *m_source = nullptr;
	QSortFilterProxyModel *m_flatProxy = nullptr;
	std::function<bool(int)> m_isRepresentative;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_FAMILYTREEMODEL_H
