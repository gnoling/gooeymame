// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  foldertree.cpp - category tree for filtering the system list
//
//============================================================

#include "foldertree.h"

#include "frontendpaths.h"
#include "gamelistmodel.h"

#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>


namespace osd::qtui {

namespace {

// Item data roles used to stash the FolderFilter on each tree item.
constexpr int KIND_ROLE = Qt::UserRole + 1;
constexpr int VALUE_ROLE = Qt::UserRole + 2;
constexpr int SET_ROLE = Qt::UserRole + 3;   // index into m_categorySets (-1 = none)

// Category .ini files to expose, with the subtree title shown for each.
struct CategoryIni { const char *file; const char *title; };
const CategoryIni kCategoryInis[] =
{
	{ "catlist.ini",   "Category"   },
	{ "genre.ini",     "Genre"      },
	{ "series.ini",    "Series"     },
	{ "languages.ini", "Language"   },
	{ "bestgames.ini", "Best Games" },
	{ "version.ini",   "Version"    },
};

} // anonymous namespace

FolderTree::FolderTree(GameListModel *model, QWidget *parent) :
	QTreeWidget(parent)
{
	setHeaderHidden(true);
	setSelectionMode(QAbstractItemView::SingleSelection);

	// Quick filters grouped under one collapsible section (the parent maps to
	// "All", like the other section parents).
	QTreeWidgetItem *quick = addFolder(nullptr, tr("Quick Filters"), FolderFilter::All);
	QTreeWidgetItem *all = addFolder(quick, tr("All Systems"), FolderFilter::All);
	addFolder(quick, tr("Arcade"), FolderFilter::Arcade);
	addFolder(quick, tr("Computers & Consoles"), FolderFilter::Console);

	// Manufacturer subtree.  The parent node itself maps to "All".
	QTreeWidgetItem *manu = addFolder(nullptr, tr("Manufacturer"), FolderFilter::All);
	for (const QString &name : model->manufacturers())
		addFolder(manu, name, FolderFilter::Manufacturer, name);

	// Year subtree.
	QTreeWidgetItem *year = addFolder(nullptr, tr("Year"), FolderFilter::All);
	for (const QString &y : model->years())
		addFolder(year, y, FolderFilter::Year, y);

	// Category subtrees from the EXTRAs .ini files.
	loadCategories();

	// Hide category nodes whose systems aren't present in this build, and drop
	// any category subtree left empty.
	for (int i = topLevelItemCount() - 1; i >= 0; --i)
	{
		QTreeWidgetItem *root = topLevelItem(i);
		if (FolderFilter::Kind(root->data(0, KIND_ROLE).toInt()) == FolderFilter::Category
				&& !pruneEmpty(root, model))
			delete takeTopLevelItem(i);
	}

	connect(this, &QTreeWidget::itemSelectionChanged, this, &FolderTree::onItemSelectionChanged);

	// Quick Filters expanded by default; other sections start collapsed (their
	// persisted state, if any, is applied by setExpandedSections()).
	quick->setExpanded(true);
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
	item->setData(0, SET_ROLE, -1);
	return item;
}

void FolderTree::loadCategories()
{
	QString const dir = frontendFolderPath(QStringLiteral("categories"));
	if (dir.isEmpty())
		return;
	for (const CategoryIni &ini : kCategoryInis)
		addCategoryIni(dir, QString::fromLatin1(ini.file), tr(ini.title));
}

void FolderTree::addCategoryIni(const QString &dir, const QString &file, const QString &title)
{
	QFile in(dir + QLatin1Char('/') + file);
	if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
		return;

	QTreeWidgetItem *root = nullptr;
	QHash<QString, QTreeWidgetItem *> nodes;   // joined path -> item (this ini)
	int currentSet = -1;

	QTextStream stream(&in);
	while (!stream.atEnd())
	{
		QString const line = stream.readLine().trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
			continue;

		if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
		{
			QString const section = line.mid(1, line.size() - 2);
			if (section == QLatin1String("FOLDER_SETTINGS") || section == QLatin1String("ROOT_FOLDER"))
			{
				currentSet = -1;
				continue;
			}
			if (!root)
				root = addFolder(nullptr, title, FolderFilter::Category);

			// Split "Arcade: Maze / Driving" into ["Arcade", "Maze", "Driving"]
			// and create/reuse a nested node per component.
			QString normalised = section;
			normalised.replace(QLatin1Char(':'), QLatin1Char('/'));
			QTreeWidgetItem *parentItem = root;
			QString path;
			for (const QString &raw : normalised.split(QLatin1Char('/'), Qt::SkipEmptyParts))
			{
				QString const component = raw.trimmed();
				if (component.isEmpty())
					continue;
				path += QLatin1Char('/') + component;
				QTreeWidgetItem *node = nodes.value(path, nullptr);
				if (!node)
				{
					node = addFolder(parentItem, component, FolderFilter::Category);
					nodes.insert(path, node);
				}
				parentItem = node;
			}

			// The final node owns this section's members.
			m_categorySets.emplace_back();
			currentSet = int(m_categorySets.size()) - 1;
			parentItem->setData(0, SET_ROLE, currentSet);
			continue;
		}

		// A system short name belonging to the current section.
		if (currentSet >= 0)
			m_categorySets[std::size_t(currentSet)].insert(line);
	}
}

void FolderTree::collectMembers(const QTreeWidgetItem *item, QSet<QString> &out) const
{
	int const setIndex = item->data(0, SET_ROLE).toInt();
	if (setIndex >= 0 && setIndex < int(m_categorySets.size()))
		out.unite(m_categorySets[std::size_t(setIndex)]);
	for (int i = 0; i < item->childCount(); i++)
		collectMembers(item->child(i), out);
}

bool FolderTree::pruneEmpty(QTreeWidgetItem *item, const GameListModel *model)
{
	// Prune children first; a parent is kept if any descendant survives.
	for (int i = item->childCount() - 1; i >= 0; --i)
	{
		if (!pruneEmpty(item->child(i), model))
			delete item->takeChild(i);
	}

	int const setIndex = item->data(0, SET_ROLE).toInt();
	if (setIndex >= 0 && setIndex < int(m_categorySets.size()))
	{
		for (const QString &name : m_categorySets[std::size_t(setIndex)])
			if (model->hasSystem(name))
				return true;
	}
	return item->childCount() > 0;
}

void FolderTree::applyCategoryFilter(const std::function<bool (const QString &)> &isVisible)
{
	for (int i = 0; i < topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = topLevelItem(i);
		if (FolderFilter::Kind(item->data(0, KIND_ROLE).toInt()) == FolderFilter::Category)
			applyCategoryFilterRec(item, isVisible);
	}
}

bool FolderTree::applyCategoryFilterRec(QTreeWidgetItem *item,
		const std::function<bool (const QString &)> &isVisible)
{
	bool anyDescendantVisible = false;
	for (int i = 0; i < item->childCount(); ++i)
		anyDescendantVisible |= applyCategoryFilterRec(item->child(i), isVisible);

	bool ownVisible;
	if (!isVisible)
	{
		ownVisible = true;   // no predicate: show every (build-surviving) category
	}
	else
	{
		ownVisible = false;
		int const setIndex = item->data(0, SET_ROLE).toInt();
		if (setIndex >= 0 && setIndex < int(m_categorySets.size()))
		{
			for (const QString &name : m_categorySets[std::size_t(setIndex)])
			{
				if (isVisible(name))
				{
					ownVisible = true;
					break;
				}
			}
		}
	}

	bool const visible = ownVisible || anyDescendantVisible;
	item->setHidden(!visible);
	return visible;
}

QStringList FolderTree::expandedSections() const
{
	QStringList out;
	for (int i = 0; i < topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = topLevelItem(i);
		if (item->childCount() > 0 && item->isExpanded())
			out << item->text(0);
	}
	return out;
}

void FolderTree::setExpandedSections(const QStringList &labels)
{
	QSet<QString> const wanted(labels.begin(), labels.end());
	for (int i = 0; i < topLevelItemCount(); ++i)
	{
		QTreeWidgetItem *item = topLevelItem(i);
		if (item->childCount() > 0)
			item->setExpanded(wanted.contains(item->text(0)));
	}
}

void FolderTree::onItemSelectionChanged()
{
	QTreeWidgetItem *item = currentItem();
	if (!item)
		return;

	FolderFilter filter;
	filter.kind = FolderFilter::Kind(item->data(0, KIND_ROLE).toInt());
	filter.value = item->data(0, VALUE_ROLE).toString();

	if (filter.kind == FolderFilter::Category)
	{
		// Selecting any category node shows its own members plus everything
		// under it.
		collectMembers(item, filter.members);
	}

	emit folderSelected(filter);
}

QString FolderTree::currentPath() const
{
	QTreeWidgetItem *item = currentItem();
	if (!item)
		return QString();
	QStringList parts;
	for (; item; item = item->parent())
		parts.prepend(item->text(0));
	// Unit separator avoids clashing with '/' that appears in category labels.
	return parts.join(QLatin1Char('\x1f'));
}

void FolderTree::selectPath(const QString &path)
{
	if (path.isEmpty())
		return;

	QTreeWidgetItem *item = nullptr;
	for (const QString &part : path.split(QLatin1Char('\x1f')))
	{
		QTreeWidgetItem *found = nullptr;
		int const count = item ? item->childCount() : topLevelItemCount();
		for (int i = 0; i < count; i++)
		{
			QTreeWidgetItem *child = item ? item->child(i) : topLevelItem(i);
			if (child->text(0) == part)
			{
				found = child;
				break;
			}
		}
		if (!found)
			return;   // path no longer exists; leave the current selection
		item = found;
	}

	for (QTreeWidgetItem *ancestor = item->parent(); ancestor; ancestor = ancestor->parent())
		ancestor->setExpanded(true);
	setCurrentItem(item);
	scrollToItem(item);
}

} // namespace osd::qtui
