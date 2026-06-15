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

	// Category subtrees from the EXTRAs .ini files.
	loadCategories();

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

} // namespace osd::qtui
