// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  familytreemodel.cpp - tree wrapper grouping clone families
//
//============================================================

#include "familytreemodel.h"


namespace osd::qtui {

FamilyTreeModel::FamilyTreeModel(QAbstractItemModel *source, GroupsFn groups, ChildrenFn children,
		QObject *parent) :
	QAbstractItemModel(parent),
	m_source(source),
	m_groupsFn(std::move(groups)),
	m_childrenFn(std::move(children))
{
	rebuild();
}

void FamilyTreeModel::rebuild()
{
	beginResetModel();
	m_groups.clear();
	m_children.clear();
	m_rowToPos.clear();

	QList<int> const groups = m_groupsFn();
	m_groups.reserve(groups.size());
	m_children.reserve(groups.size());
	for (int rep : groups)
	{
		int const group = int(m_groups.size());
		m_groups.push_back(rep);
		m_rowToPos.insert(rep, { group, -1 });

		std::vector<int> kids;
		const QList<int> childRows = m_childrenFn(rep);
		kids.reserve(childRows.size());
		for (int child : childRows)
		{
			m_rowToPos.insert(child, { group, int(kids.size()) });
			kids.push_back(child);
		}
		m_children.push_back(std::move(kids));
	}
	endResetModel();
}

int FamilyTreeModel::sourceRow(const QModelIndex &index) const
{
	if (!index.isValid())
		return -1;
	if (index.internalId() == TOP)
		return (index.row() >= 0 && index.row() < int(m_groups.size())) ? m_groups[index.row()] : -1;
	int const group = int(index.internalId());
	if (group < 0 || group >= int(m_children.size()))
		return -1;
	return (index.row() >= 0 && index.row() < int(m_children[group].size()))
			? m_children[group][index.row()] : -1;
}

QModelIndex FamilyTreeModel::indexForSourceRow(int row) const
{
	auto it = m_rowToPos.constFind(row);
	if (it == m_rowToPos.constEnd())
		return QModelIndex();
	int const group = it.value().first;
	int const child = it.value().second;
	if (child < 0)
		return createIndex(group, 0, TOP);
	return createIndex(child, 0, quintptr(group));
}

QModelIndex FamilyTreeModel::index(int row, int column, const QModelIndex &parent) const
{
	if (!hasIndex(row, column, parent))
		return QModelIndex();
	if (!parent.isValid())
		return createIndex(row, column, TOP);          // top-level group
	return createIndex(row, column, quintptr(parent.row()));   // child of group parent.row()
}

QModelIndex FamilyTreeModel::parent(const QModelIndex &index) const
{
	if (!index.isValid() || index.internalId() == TOP)
		return QModelIndex();
	return createIndex(int(index.internalId()), 0, TOP);
}

int FamilyTreeModel::rowCount(const QModelIndex &parent) const
{
	if (!parent.isValid())
		return int(m_groups.size());
	if (parent.internalId() != TOP)
		return 0;   // children have no further children
	int const group = parent.row();
	return (group >= 0 && group < int(m_children.size())) ? int(m_children[group].size()) : 0;
}

int FamilyTreeModel::columnCount(const QModelIndex &) const
{
	return m_source ? m_source->columnCount() : 0;
}

QVariant FamilyTreeModel::data(const QModelIndex &index, int role) const
{
	int const row = sourceRow(index);
	if (row < 0 || !m_source)
		return QVariant();
	return m_source->data(m_source->index(row, index.column()), role);
}

QVariant FamilyTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	return m_source ? m_source->headerData(section, orientation, role) : QVariant();
}

Qt::ItemFlags FamilyTreeModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return Qt::NoItemFlags;
	return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}


//============================================================
//  TreeFilterProxy
//============================================================

TreeFilterProxy::TreeFilterProxy(FamilyTreeModel *tree, QSortFilterProxyModel *flatProxy,
		QAbstractItemModel *flatModel, QObject *parent) :
	QSortFilterProxyModel(parent),
	m_tree(tree),
	m_flatProxy(flatProxy),
	m_flatModel(flatModel)
{
	setSortCaseSensitivity(Qt::CaseInsensitive);
	setSortLocaleAware(true);
	setRecursiveFilteringEnabled(true);   // keep a group if any child matches
	setSourceModel(tree);
}

bool TreeFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
	QModelIndex const treeIndex = m_tree->index(sourceRow, 0, sourceParent);
	int const row = m_tree->sourceRow(treeIndex);
	if (row < 0)
		return false;
	// Accept iff the flat proxy accepts the same underlying row.
	return m_flatProxy->mapFromSource(m_flatModel->index(row, 0)).isValid();
}

} // namespace osd::qtui
