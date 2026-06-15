// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  foldertree.cpp - category tree for filtering the system list
//
//============================================================

#include "foldertree.h"

#include "gamelistmodel.h"


namespace osd::qtui {

namespace {

// Item data roles used to stash the FolderFilter on each tree item.
constexpr int KIND_ROLE = Qt::UserRole + 1;
constexpr int VALUE_ROLE = Qt::UserRole + 2;

} // anonymous namespace

FolderTree::FolderTree(GameListModel *model, QWidget *parent) :
	QTreeWidget(parent)
{
	setHeaderHidden(true);
	setSelectionMode(QAbstractItemView::SingleSelection);

	QTreeWidgetItem *all = addFolder(nullptr, tr("All Systems"), FolderFilter::All);
	addFolder(nullptr, tr("Arcade"), FolderFilter::Arcade);
	addFolder(nullptr, tr("Computers & Consoles"), FolderFilter::Console);

	// Manufacturer subtree.  The parent node itself maps to "All".
	QTreeWidgetItem *manu = addFolder(nullptr, tr("Manufacturer"), FolderFilter::All);
	for (const QString &name : model->manufacturers())
		addFolder(manu, name, FolderFilter::Manufacturer, name);

	// Year subtree.
	QTreeWidgetItem *year = addFolder(nullptr, tr("Year"), FolderFilter::All);
	for (const QString &y : model->years())
		addFolder(year, y, FolderFilter::Year, y);

	connect(this, &QTreeWidget::itemSelectionChanged, this, &FolderTree::onItemSelectionChanged);

	setCurrentItem(all);
}

QTreeWidgetItem *FolderTree::addFolder(QTreeWidgetItem *parent, const QString &label,
		FolderFilter::Kind kind, const QString &value)
{
	QTreeWidgetItem *item = parent
			? new QTreeWidgetItem(parent)
			: new QTreeWidgetItem(this);
	item->setText(0, label);
	item->setData(0, KIND_ROLE, int(kind));
	item->setData(0, VALUE_ROLE, value);
	return item;
}

void FolderTree::onItemSelectionChanged()
{
	QTreeWidgetItem *item = currentItem();
	if (!item)
		return;

	FolderFilter filter;
	filter.kind = FolderFilter::Kind(item->data(0, KIND_ROLE).toInt());
	filter.value = item->data(0, VALUE_ROLE).toString();
	emit folderSelected(filter);
}

} // namespace osd::qtui
