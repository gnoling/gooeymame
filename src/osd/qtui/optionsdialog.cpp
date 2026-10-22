// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  optionsdialog.cpp - editor for mame.ini options and front-end settings
//
//============================================================

#include "optionsdialog.h"

#include "emulator.h"
#include "artworkpanel.h"
#include "frontendpaths.h"
#include "gamelistmodel.h"
#include "regions.h"

#include <QtCore/QAbstractItemModel>
#include <QtCore/QHash>
#include <QtCore/QSettings>
#include <QtGui/QDoubleValidator>
#include <QtGui/QFont>
#include <QtGui/QIntValidator>
#include <QtGui/QPalette>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <cmath>


namespace osd::qtui {

namespace {

// Title-case an ALLCAPS header word-by-word ("SEARCH PATH" -> "Search Path").
QString titleCase(const QString &text)
{
	QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
	for (QString &word : words)
		word = word.left(1).toUpper() + word.mid(1).toLower();
	return words.join(QLatin1Char(' '));
}

// MAME option descriptions are lower-case fragments ("path to ROM sets");
// capitalise the first letter so they read as a sentence next to a control.
QString sentence(const QString &text)
{
	QString out = text.trimmed();
	if (!out.isEmpty())
		out[0] = out[0].toUpper();
	return out;
}

// Curated mapping of mame.ini headers (with " OPTIONS" stripped) into a
// top-level category and a short sub-tab title, so related headers from the
// CORE/OSD/SDL sections sit together with a manageable number of sub-tabs.
struct HeaderMapping
{
	const char *header;  // cleaned header (no trailing " OPTIONS")
	const char *top;
	const char *sub;
};

const HeaderMapping kHeaderMap[] =
{
	{ "CORE CONFIGURATION",          "General",     "Configuration"   },
	{ "CORE STATE/PLAYBACK",         "General",     "State/Playback"  },
	{ "FRONTEND COMMAND",            "General",     "Frontend"        },

	{ "CORE SEARCH PATH",            "Paths",       "Search Paths"    },
	{ "CORE OUTPUT DIRECTORY",       "Paths",       "Output Dirs"     },

	{ "CORE RENDER",                 "Video",       "Render"          },
	{ "CORE ROTATION",               "Video",       "Rotation"        },
	{ "CORE SCREEN",                 "Video",       "Screen"          },
	{ "CORE VECTOR",                 "Video",       "Vector"          },
	{ "CORE ARTWORK",                "Video",       "Artwork"         },

	{ "OSD VIDEO",                   "Display",     "Video"           },
	{ "OSD PER-WINDOW VIDEO",        "Display",     "Per-Window"      },
	{ "OSD FULL SCREEN",             "Display",     "Full Screen"     },
	{ "SDL VIDEO",                   "Display",     "SDL Video"       },
	{ "SDL FULL SCREEN",             "Display",     "SDL Full Screen" },

	{ "OSD ACCELERATED VIDEO",       "Renderer",    "Accelerated"     },
	{ "OpenGL-SPECIFIC",             "Renderer",    "OpenGL"          },
	{ "BGFX POST-PROCESSING",        "Renderer",    "BGFX"            },

	{ "CORE SOUND",                  "Sound",       "Core"            },
	{ "OSD SOUND",                   "Sound",       "OSD"             },
	{ "OSD MIDI",                    "Sound",       "MIDI"            },

	{ "CORE INPUT",                  "Input",       "Core"            },
	{ "CORE INPUT AUTOMATIC ENABLE", "Input",       "Auto Enable"     },
	{ "OSD INPUT",                   "Input",       "OSD"             },
	{ "SDL INPUT",                   "Input",       "SDL"             },

	{ "OSD INPUT MAPPING",           "Controllers", "Mapping"         },
	{ "SDL KEYBOARD MAPPING",        "Controllers", "Keyboard"        },
	{ "SDL LIGHTGUN MAPPING",        "Controllers", "Lightgun"        },

	{ "CORE PERFORMANCE",            "Performance", "Core"            },
	{ "OSD PERFORMANCE",             "Performance", "OSD"             },
	{ "SDL PERFORMANCE",             "Performance", "SDL"             },

	{ "OSD FONT",                    "Interface",   "Font"            },
	{ "OSD OUTPUT",                  "Interface",   "Output"          },

	{ "CORE DEBUGGING",              "Debugging",   "Core"            },
	{ "OSD DEBUGGING",               "Debugging",   "OSD"             },

	{ "CORE COMM",                   "Advanced",    "Comm"            },
	{ "CORE MISC",                   "Advanced",    "Misc"            },
	{ "HTTP SERVER",                 "Advanced",    "HTTP Server"     },
	{ "SCRIPTING",                   "Advanced",    "Scripting"       },
	{ "OSD EMULATED NETWORKING",     "Advanced",    "Networking"      },
	{ "SDL LOW-LEVEL DRIVER",        "Advanced",    "SDL Driver"      },
};

// Split a mame.ini header into a top-level category and sub-tab title using
// the curated table, falling back to a CORE/OSD/SDL prefix split for any
// header not listed (so future/unknown headers still appear).
void splitHeader(const std::string &header, QString &top, QString &sub)
{
	QString text = QString::fromStdString(header).trimmed();
	if (text.endsWith(QLatin1String(" OPTIONS")))
		text.chop(8);

	if (text.isEmpty())
	{
		top = QObject::tr("General");
		sub = QObject::tr("General");
		return;
	}

	for (const HeaderMapping &m : kHeaderMap)
	{
		if (text == QLatin1String(m.header))
		{
			top = QString::fromLatin1(m.top);
			sub = QString::fromLatin1(m.sub);
			return;
		}
	}

	if (text.startsWith(QLatin1String("CORE ")))      { top = QStringLiteral("Core"); sub = titleCase(text.mid(5)); }
	else if (text.startsWith(QLatin1String("OSD ")))  { top = QStringLiteral("OSD");  sub = titleCase(text.mid(4)); }
	else if (text.startsWith(QLatin1String("SDL ")))  { top = QStringLiteral("SDL");  sub = titleCase(text.mid(4)); }
	else                                              { top = QObject::tr("Advanced"); sub = titleCase(text); }

	if (sub.isEmpty())
		sub = top;
}

// Known enumerated string options -> their common choices.  The combo boxes
// are editable, so values not listed here (e.g. platform-specific providers)
// can still be typed.
struct OptionChoices
{
	const char *name;
	const char *choices;   // comma-separated
};

const OptionChoices kOptionChoices[] =
{
	{ "video",            "auto,opengl,bgfx,accel,soft,none" },
	{ "bgfx_backend",     "auto,opengl,gles,vulkan,metal,d3d9,d3d11,d3d12" },
	// The Qt-native OSD links no SDL: keyboard/mouse/lightgun/monitor come from
	// the "qt" provider and sound from a platform backend, so the old sdl/x11
	// choices are dropped here.  These combos are editable, so platform-specific
	// values not listed (e.g. coreaudio) can still be typed.  The Qt-native launch
	// also force-overrides these for play (see emulator.cpp); the joystick provider
	// is chosen via View ▸ Qt-native Gamepad.
	{ "sound",            "auto,pulse,pipewire,portaudio,wasapi,xaudio2,coreaudio,none" },
	{ "keyboardprovider", "auto,qt,none" },
	{ "mouseprovider",    "auto,qt,none" },
	{ "lightgunprovider", "auto,qt,none" },
	{ "joystickprovider", "auto,winhybrid,xinput,dinput,sdlgame,none" },
	{ "monitorprovider",  "auto,qt" },
	{ "debugger",         "auto,none,qt,gdbstub,imgui" },
	{ "output",           "auto,none,console,network" },
	{ "uimodekey",        "auto" },
};

// Return the comma-split choices for an option, or an empty list.
QStringList choicesFor(const QString &name)
{
	for (const OptionChoices &c : kOptionChoices)
		if (name == QLatin1String(c.name))
			return QString::fromLatin1(c.choices).split(QLatin1Char(','), Qt::SkipEmptyParts);
	return {};
}

// Append an in-place-editable entry to a search-path list.
QListWidgetItem *addPathItem(QListWidget *list, const QString &path)
{
	QListWidgetItem *item = new QListWidgetItem(path, list);
	item->setFlags(item->flags() | Qt::ItemIsEditable);
	return item;
}

// Move the current item of an ordered list up or down.
void moveCurrentItem(QListWidget *list, int delta)
{
	int const from = list->currentRow();
	int const to = from + delta;
	if (from < 0 || to < 0 || to >= list->count())
		return;
	QListWidgetItem *item = list->takeItem(from);
	list->insertItem(to, item);
	list->setCurrentItem(item);
}

// Settings keys for the remembered dialog geometry and last page.
constexpr char kGeometryKey[] = "options/geometry";
constexpr char kPageGlobalKey[] = "options/pageGlobal";
constexpr char kPageMachineKey[] = "options/pageMachine";

} // anonymous namespace


//-------------------------------------------------
//  construction / chrome
//-------------------------------------------------

OptionsDialog::OptionsDialog(QWidget *parent) :
	QDialog(parent)
{
	buildUi();
}

OptionsDialog::OptionsDialog(const QString &system, const QString &description, QWidget *parent) :
	QDialog(parent),
	m_system(system),
	m_systemDescription(description)
{
	buildUi();
}

void OptionsDialog::buildUi()
{
	bool const gameMode = !m_system.isEmpty();
	if (!gameMode)
		setWindowTitle(tr("Options"));
	else if (m_systemDescription.isEmpty())
		setWindowTitle(tr("Properties: %1").arg(m_system));
	else
		setWindowTitle(tr("Properties: %1 (%2)").arg(m_systemDescription, m_system));
	resize(980, 700);
	setMinimumSize(760, 520);

	// Left column: search box over a sectioned page list.
	m_search = new QLineEdit(this);
	m_search->setPlaceholderText(tr("Search settings…"));
	m_search->setClearButtonEnabled(true);
	m_search->setToolTip(tr("Filter every page by option name, description or page title."));
	connect(m_search, &QLineEdit::textChanged, this, &OptionsDialog::applyFilter);

	m_categoryList = new QListWidget(this);
	m_categoryList->setFixedWidth(210);
	m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_stack = new QStackedWidget(this);
	connect(m_categoryList, &QListWidget::currentItemChanged, this,
			[this] (QListWidgetItem *current, QListWidgetItem *) {
				if (!current)
					return;
				QVariant const page = current->data(Qt::UserRole);
				if (page.isValid())
					m_stack->setCurrentIndex(page.toInt());
			});

	if (!gameMode)
	{
		addSectionHeader(tr("Front-end"));
		buildFolderCategory();
		buildGameplayCategory();
		buildVersionsCategory();
		buildGridArtCategory();
		buildIconSourcesCategory();
		buildArtScaleCategory();
		addSectionHeader(tr("Emulator (mame.ini)"));
	}
	else
	{
		addSectionHeader(tr("%1.ini").arg(m_system));
	}
	buildOptionCategories();

	QVBoxLayout *left = new QVBoxLayout;
	left->setContentsMargins(0, 0, 0, 0);
	left->addWidget(m_search);
	left->addWidget(m_categoryList, 1);

	QVBoxLayout *right = new QVBoxLayout;
	right->setContentsMargins(0, 0, 0, 0);
	if (gameMode)
	{
		QLabel *note = makeMuted(
				tr("Changes are written to %1.ini and apply only to this machine, layered over "
				   "the global options.  Bold names are already set in that file.").arg(m_system), this);
		right->addWidget(note);
	}
	right->addWidget(m_stack, 1);

	QHBoxLayout *top = new QHBoxLayout;
	top->setSpacing(10);
	top->addLayout(left);
	top->addLayout(right, 1);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(top, 1);
	layout->addWidget(buttons);

	restoreState();
}

void OptionsDialog::addSectionHeader(const QString &title)
{
	QListWidgetItem *item = new QListWidgetItem(title.toUpper(), m_categoryList);
	item->setFlags(Qt::NoItemFlags);
	QFont font = item->font();
	font.setBold(true);
	font.setPointSizeF(font.pointSizeF() * 0.85);
	item->setFont(font);
	// Breathing room above every header but the first.
	int const extra = (m_categoryList->count() > 1) ? 10 : 2;
	item->setSizeHint(QSize(0, m_categoryList->fontMetrics().height() + 6 + extra));
	item->setTextAlignment(Qt::AlignLeft | Qt::AlignBottom);
}

int OptionsDialog::addCategory(const QString &title, QWidget *page, const QString &keywords)
{
	QScrollArea *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(page);
	int const index = m_stack->addWidget(scroll);

	QListWidgetItem *item = new QListWidgetItem(QStringLiteral("   ") + title, m_categoryList);
	item->setData(Qt::UserRole, index);
	item->setSizeHint(QSize(0, m_categoryList->fontMetrics().height() + 10));

	m_categories.push_back({ item, index, (title + QLatin1Char(' ') + keywords).toLower(), false });
	return int(m_categories.size()) - 1;
}

void OptionsDialog::registerRow(int category, QTabWidget *tabs, int tab, QFormLayout *form, QWidget *field, const QString &text)
{
	m_rows.push_back({ category, tabs, tab, form, field, text.toLower() });
	m_categories[category].hasRows = true;
}

void OptionsDialog::selectCategory(int stackIndex)
{
	for (int i = 0; i < m_categoryList->count(); i++)
	{
		QListWidgetItem *item = m_categoryList->item(i);
		if (item->data(Qt::UserRole).isValid() && item->data(Qt::UserRole).toInt() == stackIndex)
		{
			m_categoryList->setCurrentItem(item);
			return;
		}
	}
}

void OptionsDialog::applyFilter(const QString &text)
{
	QString const needle = text.trimmed().toLower();
	bool const filtering = !needle.isEmpty();

	// Rows first: hide every non-matching form row, remembering which sub-tabs
	// and pages keep at least one.
	std::vector<int> visibleRows(m_categories.size(), 0);
	QHash<QTabWidget *, QSet<int>> visibleTabs;
	for (const SearchRow &row : m_rows)
	{
		bool const show = !filtering || row.text.contains(needle);
		row.form->setRowVisible(row.field, show);
		if (row.tabs)
		{
			if (!visibleTabs.contains(row.tabs))
				visibleTabs.insert(row.tabs, {});
			if (show)
				visibleTabs[row.tabs].insert(row.tab);
		}
		if (show)
			visibleRows[row.category]++;
	}

	// Sub-tabs with nothing left are hidden; land on the first one still shown.
	for (auto it = visibleTabs.constBegin(); it != visibleTabs.constEnd(); ++it)
	{
		QTabWidget *tabs = it.key();
		int first = -1;
		for (int i = 0; i < tabs->count(); i++)
		{
			bool const show = !filtering || it.value().contains(i);
			tabs->setTabVisible(i, show);
			if (show && first < 0)
				first = i;
		}
		if (first >= 0 && !tabs->isTabVisible(tabs->currentIndex()))
			tabs->setCurrentIndex(first);
	}

	// Pages: hidden when none of their rows match (pages without rows match
	// on their title/keywords instead).  Keep a visible page selected.
	int const current = m_stack->currentIndex();
	int firstVisible = -1;
	bool currentVisible = false;
	for (std::size_t i = 0; i < m_categories.size(); i++)
	{
		const Category &cat = m_categories[i];
		bool const show = !filtering
				|| (cat.hasRows ? (visibleRows[i] > 0) : cat.keywords.contains(needle));
		cat.item->setHidden(!show);
		if (show)
		{
			if (firstVisible < 0)
				firstVisible = int(i);
			if (cat.stackIndex == current)
				currentVisible = true;
		}
	}
	if (!currentVisible && firstVisible >= 0)
		selectCategory(m_categories[firstVisible].stackIndex);

	// Section headers follow their pages.
	for (int i = 0; i < m_categoryList->count(); i++)
	{
		QListWidgetItem *header = m_categoryList->item(i);
		if (header->data(Qt::UserRole).isValid())
			continue;
		bool any = false;
		for (int j = i + 1; j < m_categoryList->count(); j++)
		{
			QListWidgetItem *next = m_categoryList->item(j);
			if (!next->data(Qt::UserRole).isValid())
				break;
			if (!next->isHidden())
			{
				any = true;
				break;
			}
		}
		header->setHidden(!any);
	}
}

void OptionsDialog::restoreState()
{
	QSettings settings;
	QByteArray const geometry = settings.value(QLatin1String(kGeometryKey)).toByteArray();
	if (!geometry.isEmpty())
		restoreGeometry(geometry);

	QString const key = QLatin1String(m_system.isEmpty() ? kPageGlobalKey : kPageMachineKey);
	QString const wanted = settings.value(key).toString();
	for (const Category &cat : m_categories)
	{
		if (!wanted.isEmpty() && cat.item->text().trimmed() == wanted)
		{
			m_categoryList->setCurrentItem(cat.item);
			return;
		}
	}
	if (!m_categories.empty())
		m_categoryList->setCurrentItem(m_categories.front().item);
}

void OptionsDialog::done(int result)
{
	QSettings settings;
	settings.setValue(QLatin1String(kGeometryKey), saveGeometry());
	if (QListWidgetItem *item = m_categoryList->currentItem())
		if (item->data(Qt::UserRole).isValid())
			settings.setValue(QLatin1String(m_system.isEmpty() ? kPageGlobalKey : kPageMachineKey), item->text().trimmed());
	QDialog::done(result);
}


//-------------------------------------------------
//  shared building blocks
//-------------------------------------------------

QLabel *OptionsDialog::makeIntro(const QString &text, QWidget *parent)
{
	QLabel *label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setContentsMargins(0, 0, 0, 6);
	return label;
}

QLabel *OptionsDialog::makeMuted(const QString &text, QWidget *parent)
{
	QLabel *label = new QLabel(text, parent);
	label->setWordWrap(true);
	QPalette pal = label->palette();
	pal.setColor(QPalette::WindowText, pal.color(QPalette::Disabled, QPalette::WindowText));
	label->setPalette(pal);
	QFont font = label->font();
	font.setPointSizeF(font.pointSizeF() * 0.92);
	label->setFont(font);
	return label;
}

QWidget *OptionsDialog::makeOrderedList(QWidget *parent, QListWidget *&list)
{
	QWidget *box = new QWidget(parent);
	QHBoxLayout *h = new QHBoxLayout(box);
	h->setContentsMargins(0, 0, 0, 0);

	list = new QListWidget(box);
	list->setToolTip(tr("Top is tried first.  Uncheck an entry to skip it."));
	h->addWidget(list, 1);

	QVBoxLayout *v = new QVBoxLayout;
	QPushButton *up = new QPushButton(tr("Up"), box);
	QPushButton *down = new QPushButton(tr("Down"), box);
	QListWidget *const target = list;
	connect(up, &QPushButton::clicked, list, [target] { moveCurrentItem(target, -1); });
	connect(down, &QPushButton::clicked, list, [target] { moveCurrentItem(target, +1); });
	v->addWidget(up);
	v->addWidget(down);
	v->addStretch(1);
	h->addLayout(v);
	return box;
}

QListWidget *OptionsDialog::makeSearchPathEditor(QWidget *parent, const QStringList &initial, bool allowArchives, QWidget *&container)
{
	QWidget *row = new QWidget(parent);
	QVBoxLayout *col = new QVBoxLayout(row);
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(2);

	QListWidget *list = new QListWidget(row);
	list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	list->setToolTip(tr("Searched top to bottom.  Double-click an entry to edit it."));
	for (const QString &path : initial)
		addPathItem(list, path);

	// Size each list to its contents (two to five rows) so a page full of
	// mostly-empty search paths stays compact.
	auto fit = [list] {
		int const rowHeight = list->fontMetrics().height() + 6;
		int const rows = qBound(2, list->count(), 5);
		list->setFixedHeight(rows * rowHeight + 2 * list->frameWidth() + 4);
	};
	fit();
	connect(list->model(), &QAbstractItemModel::rowsInserted, list, fit);
	connect(list->model(), &QAbstractItemModel::rowsRemoved, list, fit);

	QPushButton *addDir = new QPushButton(tr("Add Folder…"), row);
	connect(addDir, &QPushButton::clicked, this, [this, list] {
		QString const dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
		if (!dir.isEmpty() && !searchPathEntries(list).contains(dir))
			list->setCurrentItem(addPathItem(list, dir));
	});
	QPushButton *addZip = nullptr;
	if (allowArchives)
	{
		addZip = new QPushButton(tr("Add Zip…"), row);
		connect(addZip, &QPushButton::clicked, this, [this, list] {
			QString const zip = QFileDialog::getOpenFileName(this, tr("Add Zip Archive"), QString(), tr("Zip archives (*.zip *.7z);;All files (*)"));
			if (!zip.isEmpty() && !searchPathEntries(list).contains(zip))
				list->setCurrentItem(addPathItem(list, zip));
		});
	}
	QPushButton *remove = new QPushButton(tr("Remove"), row);
	connect(remove, &QPushButton::clicked, list, [list] {
		qDeleteAll(list->selectedItems());
	});
	QPushButton *up = new QPushButton(tr("Up"), row);
	QPushButton *down = new QPushButton(tr("Down"), row);
	connect(up, &QPushButton::clicked, list, [list] { moveCurrentItem(list, -1); });
	connect(down, &QPushButton::clicked, list, [list] { moveCurrentItem(list, +1); });

	QHBoxLayout *btns = new QHBoxLayout;
	btns->addWidget(addDir);
	if (addZip)
		btns->addWidget(addZip);
	btns->addWidget(remove);
	btns->addWidget(up);
	btns->addWidget(down);
	btns->addStretch(1);
	col->addWidget(list);
	col->addLayout(btns);

	container = row;
	return list;
}

QStringList OptionsDialog::searchPathEntries(const QListWidget *list)
{
	QStringList out;
	for (int i = 0; i < list->count(); i++)
	{
		QString const text = list->item(i)->text().trimmed();
		if (!text.isEmpty())
			out.append(text);
	}
	return out;
}


//-------------------------------------------------
//  mame.ini option pages
//-------------------------------------------------

void OptionsDialog::buildOptionCategories()
{
	std::vector<qtui_option_group> groups;
	if (m_system.isEmpty())
	{
		groups = qtui_read_options();
	}
	else
	{
		std::vector<std::string> overridden;
		groups = qtui_read_game_options(m_system.toStdString(), &overridden);
		for (const std::string &name : overridden)
			m_overridden.insert(QString::fromStdString(name));
	}

	// Bucket the ini headers into top-level categories (preserving order),
	// each holding an ordered list of sub-tabs.
	struct Sub { QString title; const qtui_option_group *group; };
	std::vector<QString> topOrder;
	std::vector<std::vector<Sub>> buckets;

	for (const qtui_option_group &group : groups)
	{
		if (group.options.empty())
			continue;

		QString top, sub;
		splitHeader(group.header, top, sub);

		int index = -1;
		for (int i = 0; i < int(topOrder.size()); i++)
			if (topOrder[i] == top) { index = i; break; }
		if (index < 0)
		{
			topOrder.push_back(top);
			buckets.emplace_back();
			index = int(topOrder.size()) - 1;
		}
		buckets[index].push_back({ sub, &group });
	}

	// Build one page per top group, with sub-tabs inside.
	for (int t = 0; t < int(topOrder.size()); t++)
	{
		QWidget *page = new QWidget;
		QVBoxLayout *outer = new QVBoxLayout(page);
		outer->setContentsMargins(0, 0, 0, 0);
		QTabWidget *subtabs = new QTabWidget(page);
		outer->addWidget(subtabs);
		int const category = addCategory(topOrder[t], page);

		for (const Sub &sub : buckets[t])
		{
			// Form + trailing stretch, so a page (or a filtered-down page) keeps
			// its rows packed at the top instead of spreading them out.
			QWidget *subPage = new QWidget;
			QVBoxLayout *subLayout = new QVBoxLayout(subPage);
			QFormLayout *form = new QFormLayout;
			form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
			form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
			form->setVerticalSpacing(10);
			subLayout->addLayout(form);
			subLayout->addStretch(1);

			QScrollArea *scroll = new QScrollArea;
			scroll->setWidgetResizable(true);
			scroll->setFrameShape(QFrame::NoFrame);
			scroll->setWidget(subPage);
			int const tab = subtabs->addTab(scroll, sub.title);

			QString const context = topOrder[t] + QLatin1Char(' ') + sub.title;
			for (const qtui_option &opt : sub.group->options)
				addOptionRow(form, opt, category, subtabs, tab, context);
		}
	}
}

void OptionsDialog::addOptionRow(QFormLayout *form, const qtui_option &opt, int category, QTabWidget *tabs, int tab, const QString &context)
{
	QString const name = QString::fromStdString(opt.name);
	QString const value = QString::fromStdString(opt.value);
	QString const help = sentence(QString::fromStdString(opt.description));
	QString const defaultValue = QString::fromStdString(opt.default_value);

	QWidget *editor = nullptr;      // widget whose value is read back
	QWidget *control = nullptr;     // widget placed in the row (editor or its container)

	if (opt.type == QTUI_OPT_BOOLEAN)
	{
		QCheckBox *check = new QCheckBox(help);
		check->setChecked(value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
		editor = control = check;
	}
	else if (opt.type == QTUI_OPT_MULTIPATH)
	{
		editor = makeSearchPathEditor(nullptr, value.split(QLatin1Char(';'), Qt::SkipEmptyParts), false, control);
	}
	else if (opt.type == QTUI_OPT_PATH)
	{
		QWidget *row = new QWidget;
		QHBoxLayout *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		QLineEdit *edit = new QLineEdit(value, row);
		QPushButton *browse = new QPushButton(tr("Browse…"), row);
		connect(browse, &QPushButton::clicked, this, [this, edit] {
			QString const dir = QFileDialog::getExistingDirectory(this, tr("Select Folder"));
			if (!dir.isEmpty())
				edit->setText(dir);
		});
		h->addWidget(edit, 1);
		h->addWidget(browse);
		editor = edit;
		control = row;
	}
	else if (opt.type == QTUI_OPT_INTEGER)
	{
		bool okMin = false, okMax = false;
		int const lo = QString::fromStdString(opt.minimum).toInt(&okMin);
		int const hi = QString::fromStdString(opt.maximum).toInt(&okMax);
		if (okMin && okMax && hi > lo)
		{
			QSpinBox *spin = new QSpinBox;
			spin->setRange(lo, hi);
			spin->setValue(value.toInt());
			editor = control = spin;
		}
		else
		{
			QLineEdit *edit = new QLineEdit(value);
			edit->setValidator(new QIntValidator(edit));
			editor = control = edit;
		}
	}
	else if (opt.type == QTUI_OPT_FLOAT)
	{
		bool okMin = false, okMax = false;
		double const lo = QString::fromStdString(opt.minimum).toDouble(&okMin);
		double const hi = QString::fromStdString(opt.maximum).toDouble(&okMax);
		if (okMin && okMax && hi > lo)
		{
			QDoubleSpinBox *spin = new QDoubleSpinBox;
			spin->setDecimals(3);
			spin->setRange(lo, hi);
			spin->setSingleStep((hi - lo) >= 10.0 ? 1.0 : 0.05);
			spin->setValue(value.toDouble());
			editor = control = spin;
		}
		else
		{
			QLineEdit *edit = new QLineEdit(value);
			edit->setValidator(new QDoubleValidator(edit));
			editor = control = edit;
		}
	}
	else
	{
		QStringList const choices = choicesFor(name);
		if (!choices.isEmpty())
		{
			QComboBox *combo = new QComboBox;
			combo->setEditable(true);
			combo->addItems(choices);
			if (combo->findText(value) < 0 && !value.isEmpty())
				combo->addItem(value);
			combo->setCurrentText(value);
			editor = control = combo;
		}
		else
		{
			editor = control = new QLineEdit(value);
		}
	}

	// Reset-to-default, visible only while the value differs from MAME's default.
	QToolButton *reset = nullptr;
	if (opt.has_default)
	{
		reset = new QToolButton;
		reset->setAutoRaise(true);
		reset->setText(tr("Reset"));
		reset->setToolTip(tr("Reset to MAME's default: %1").arg(defaultValue.isEmpty() ? tr("(empty)") : defaultValue));
	}

	// Field cell: the control (+ reset) with the description underneath, so a
	// page reads without hovering.  Booleans carry the description as their
	// checkbox text instead.
	QWidget *field = new QWidget;
	QVBoxLayout *col = new QVBoxLayout(field);
	col->setContentsMargins(0, 0, 0, 0);
	col->setSpacing(2);
	QHBoxLayout *line = new QHBoxLayout;
	line->setContentsMargins(0, 0, 0, 0);
	line->addWidget(control, 1);
	if (reset)
		line->addWidget(reset, 0, Qt::AlignTop);
	col->addLayout(line);
	if (opt.type != QTUI_OPT_BOOLEAN && !help.isEmpty())
		col->addWidget(makeMuted(help, field));

	QLabel *label = new QLabel(name);
	bool const overridden = m_overridden.contains(name);
	if (overridden)
	{
		QFont font = label->font();
		font.setBold(true);
		label->setFont(font);
		label->setToolTip(tr("Set in %1.ini").arg(m_system));
	}
	if (editor)
		editor->setToolTip(help);
	form->addRow(label, field);

	int const index = int(m_editors.size());
	m_editors.push_back({ name, opt.type, editor, value, defaultValue, opt.has_default, reset });

	if (reset)
	{
		connect(reset, &QToolButton::clicked, this, [this, index] {
			setEditorValue(m_editors[index], m_editors[index].defaultValue);
			refreshReset(index);
		});
		auto track = [this, index] { refreshReset(index); };
		if (auto *check = qobject_cast<QCheckBox *>(editor))
			connect(check, &QCheckBox::toggled, this, track);
		else if (auto *combo = qobject_cast<QComboBox *>(editor))
			connect(combo, &QComboBox::currentTextChanged, this, track);
		else if (auto *spin = qobject_cast<QSpinBox *>(editor))
			connect(spin, &QSpinBox::valueChanged, this, track);
		else if (auto *dspin = qobject_cast<QDoubleSpinBox *>(editor))
			connect(dspin, &QDoubleSpinBox::valueChanged, this, track);
		else if (auto *list = qobject_cast<QListWidget *>(editor))
		{
			connect(list->model(), &QAbstractItemModel::rowsInserted, this, track);
			connect(list->model(), &QAbstractItemModel::rowsRemoved, this, track);
			connect(list->model(), &QAbstractItemModel::rowsMoved, this, track);
			connect(list, &QListWidget::itemChanged, this, track);
		}
		else if (auto *edit = qobject_cast<QLineEdit *>(editor))
			connect(edit, &QLineEdit::textChanged, this, track);
		refreshReset(index);
	}

	registerRow(category, tabs, tab, form, field, name + QLatin1Char(' ') + help + QLatin1Char(' ') + context);
}

QString OptionsDialog::editorValue(const Editor &editor) const
{
	QWidget *w = editor.widget;
	if (auto *check = qobject_cast<QCheckBox *>(w))
		return check->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
	if (auto *combo = qobject_cast<QComboBox *>(w))
		return combo->currentText();
	if (auto *spin = qobject_cast<QSpinBox *>(w))
		return QString::number(spin->value());
	if (auto *dspin = qobject_cast<QDoubleSpinBox *>(w))
		return QString::number(dspin->value());
	if (auto *list = qobject_cast<QListWidget *>(w))
		return searchPathEntries(list).join(QLatin1Char(';'));
	if (auto *edit = qobject_cast<QLineEdit *>(w))
		return edit->text();
	return editor.original;
}

void OptionsDialog::setEditorValue(const Editor &editor, const QString &value)
{
	QWidget *w = editor.widget;
	if (auto *check = qobject_cast<QCheckBox *>(w))
		check->setChecked(value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
	else if (auto *combo = qobject_cast<QComboBox *>(w))
		combo->setCurrentText(value);
	else if (auto *spin = qobject_cast<QSpinBox *>(w))
		spin->setValue(value.toInt());
	else if (auto *dspin = qobject_cast<QDoubleSpinBox *>(w))
		dspin->setValue(value.toDouble());
	else if (auto *list = qobject_cast<QListWidget *>(w))
	{
		list->clear();
		for (const QString &part : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
			addPathItem(list, part);
	}
	else if (auto *edit = qobject_cast<QLineEdit *>(w))
		edit->setText(value);
}

bool OptionsDialog::valueEquals(const Editor &editor, const QString &a, const QString &b) const
{
	switch (editor.type)
	{
	case QTUI_OPT_BOOLEAN:
	{
		auto truth = [] (const QString &s) { return s == QLatin1String("1") || s.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0; };
		return truth(a) == truth(b);
	}
	case QTUI_OPT_INTEGER:
		return a.toInt() == b.toInt();
	case QTUI_OPT_FLOAT:
		return std::fabs(a.toDouble() - b.toDouble()) < 1e-6;
	default:
		return a.trimmed() == b.trimmed();
	}
}

void OptionsDialog::refreshReset(int index)
{
	const Editor &editor = m_editors[index];
	if (editor.reset)
		editor.reset->setVisible(editor.hasDefault && !valueEquals(editor, editorValue(editor), editor.defaultValue));
}


//-------------------------------------------------
//  front-end pages
//-------------------------------------------------

void OptionsDialog::buildGameplayCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	layout->addWidget(makeIntro(
			tr("Skip MAME's startup screens so games boot straight in.  Each screen "
			   "can be skipped independently; these apply to every launch."), page));

	// Fall back to the previous single "play/noNag" value so an existing
	// preference carries over to all three when the split keys aren't set yet.
	bool const old = QSettings().value(QStringLiteral("play/noNag"), false).toBool();

	QGroupBox *box = new QGroupBox(tr("Startup screens to skip"), page);
	QVBoxLayout *v = new QVBoxLayout(box);

	m_noNagInfo = new QCheckBox(tr("System-information screen"), box);
	m_noNagInfo->setChecked(QSettings().value(QStringLiteral("play/noNagInfo"), old).toBool());
	v->addWidget(m_noNagInfo);
	v->addWidget(makeMuted(tr("The blue screen listing the system name, manufacturer and emulation status."), box));

	m_noNagWarnings = new QCheckBox(tr("Warning screen"), box);
	m_noNagWarnings->setChecked(QSettings().value(QStringLiteral("play/noNagWarnings"), old).toBool());
	v->addWidget(m_noNagWarnings);
	v->addWidget(makeMuted(tr("The red/yellow screen warning about imperfect or non-working emulation."), box));

	m_noNagLoading = new QCheckBox(tr("Loading / initialising messages"), box);
	m_noNagLoading->setChecked(QSettings().value(QStringLiteral("play/noNagLoading"), old).toBool());
	v->addWidget(m_noNagLoading);
	v->addWidget(makeMuted(tr("The \"Initializing / Loading / Decrypting\" messages shown while the machine starts."), box));

	layout->addWidget(box);
	layout->addStretch(1);
	addCategory(tr("Gameplay"), page, tr("no nag skip startup screens information warning loading launch"));
}

void OptionsDialog::buildVersionsCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	layout->addWidget(makeIntro(
			tr("How the front-end picks the primary version of a game that has "
			   "clones (regional/revision variants).  Affects the top item, its "
			   "artwork, and what launching a group runs."), page));

	QFormLayout *form = new QFormLayout;
	m_versionMode = new QComboBox(page);
	m_versionMode->addItem(tr("Match MAME (use the parent set)"), GameListModel::MatchParent);
	m_versionMode->addItem(tr("Promote my preferred region"), GameListModel::PromoteRegion);
	form->addRow(tr("Default version"), m_versionMode);

	m_useSystemRegion = new QCheckBox(tr("Use my system region automatically"), page);
	form->addRow(QString(), m_useSystemRegion);
	layout->addLayout(form);

	QGroupBox *box = new QGroupBox(tr("Region priority"), page);
	QVBoxLayout *v = new QVBoxLayout(box);
	v->addWidget(makeMuted(tr("Top is preferred.  Uncheck a region to ignore it."), box));
	v->addWidget(makeOrderedList(box, m_regionList), 1);
	layout->addWidget(box, 1);

	// Load current settings.
	QSettings settings;
	int const mode = settings.value(QStringLiteral("versions/mode"), int(GameListModel::MatchParent)).toInt();
	int const modeIndex = m_versionMode->findData(mode);
	m_versionMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
	m_useSystemRegion->setChecked(settings.value(QStringLiteral("versions/useSystemRegion"), false).toBool());

	QStringList const saved = settings.value(QStringLiteral("versions/order")).toStringList();
	QStringList ordered;
	QSet<QString> checked;
	if (saved.isEmpty())
	{
		ordered = defaultRegionOrder();   // all enabled by default
		checked = QSet<QString>(ordered.begin(), ordered.end());
	}
	else
	{
		ordered = saved;
		checked = QSet<QString>(saved.begin(), saved.end());
		for (const QString &region : defaultRegionOrder())
			if (!ordered.contains(region))
				ordered << region;   // append the rest, unchecked
	}
	for (const QString &region : ordered)
	{
		QListWidgetItem *item = new QListWidgetItem(region, m_regionList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(checked.contains(region) ? Qt::Checked : Qt::Unchecked);
	}

	addCategory(tr("Versions & Regions"), page, tr("clone parent region version revision world usa japan europe"));
}

void OptionsDialog::buildGridArtCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	layout->addWidget(makeIntro(
			tr("When a grid tile has no image for the selected type, fall back to "
			   "these art types in order."), page));

	m_gridArtFamily = new QCheckBox(
			tr("Also use images from related sets (clone parent / other regions)"), page);
	layout->addWidget(m_gridArtFamily);

	QGroupBox *box = new QGroupBox(tr("Fallback order"), page);
	QVBoxLayout *v = new QVBoxLayout(box);
	v->addWidget(makeMuted(tr("Top is tried first.  Uncheck a type to skip it."), box));
	v->addWidget(makeOrderedList(box, m_gridArtList), 1);
	layout->addWidget(box, 1);

	// Load settings: the saved order is the checked subset; append the rest.
	QSettings settings;
	m_gridArtFamily->setChecked(settings.value(QStringLiteral("grid/artFallbackFamily"), true).toBool());

	QStringList all;
	for (std::size_t i = 0; i < THUMBNAIL_SOURCE_COUNT; ++i)
		all << QString::fromLatin1(THUMBNAIL_SOURCES[i].label);

	QStringList const saved = settings.value(QStringLiteral("grid/artFallbackOrder")).toStringList();
	QStringList ordered;
	QSet<QString> checked;
	if (saved.isEmpty())
	{
		ordered = all;
		checked = QSet<QString>(all.begin(), all.end());
	}
	else
	{
		ordered = saved;
		checked = QSet<QString>(saved.begin(), saved.end());
		for (const QString &label : all)
			if (!ordered.contains(label))
				ordered << label;
	}
	for (const QString &label : ordered)
	{
		QListWidgetItem *item = new QListWidgetItem(label, m_gridArtList);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(checked.contains(label) ? Qt::Checked : Qt::Unchecked);
	}

	addCategory(tr("Grid Artwork"), page, tr("grid tiles thumbnail fallback snapshot title flyer cover"));
}

void OptionsDialog::buildIconSourcesCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	layout->addWidget(makeIntro(
			tr("Which artwork the small row icons use, as an independent priority "
			   "list per list.  \"Icon\" is the icons.zip set; the rest are art folders."), page));

	// Build one reorderable, checkable icon-source section into `box`, wiring the
	// list + the two toggles.  `preferLabel` differs (parent vs host machine).
	auto buildSection = [this] (QGroupBox *box, const QString &orderKey,
			const QString &preferKey, const QString &familyKey, const QString &preferLabel,
			QListWidget *&listOut, QCheckBox *&preferOut, QCheckBox *&familyOut) {
		QVBoxLayout *v = new QVBoxLayout(box);

		preferOut = new QCheckBox(preferLabel, box);
		familyOut = new QCheckBox(tr("Also try region/family variants"), box);
		v->addWidget(preferOut);
		v->addWidget(familyOut);
		v->addWidget(makeOrderedList(box, listOut), 1);
		QListWidget *const list = listOut;

		// Load: saved order is the checked ids; default = just "icon" checked.
		QSettings settings;
		preferOut->setChecked(settings.value(preferKey, false).toBool());
		familyOut->setChecked(settings.value(familyKey, false).toBool());

		QStringList const savedIds = settings.value(orderKey).toStringList();
		QSet<QString> const checked = savedIds.isEmpty()
				? QSet<QString>{ QStringLiteral("icon") }
				: QSet<QString>(savedIds.begin(), savedIds.end());

		// Order: the saved ids first (in order), then any remaining sources.
		QStringList ordered = savedIds;
		for (std::size_t i = 0; i < ICON_SOURCE_COUNT; ++i)
		{
			QString const id = QString::fromLatin1(ICON_SOURCES[i].id);
			if (!ordered.contains(id))
				ordered << id;
		}
		for (const QString &id : ordered)
		{
			QString label = id;
			for (std::size_t i = 0; i < ICON_SOURCE_COUNT; ++i)
				if (id == QLatin1String(ICON_SOURCES[i].id))
					label = QString::fromLatin1(ICON_SOURCES[i].label);
			QListWidgetItem *item = new QListWidgetItem(label, list);
			item->setData(Qt::UserRole, id);
			item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
			item->setCheckState(checked.contains(id) ? Qt::Checked : Qt::Unchecked);
		}
	};

	QGroupBox *machineBox = new QGroupBox(tr("Machine list"), page);
	buildSection(machineBox, QStringLiteral("icons/machineSources"),
			QStringLiteral("icons/machinePreferOwn"), QStringLiteral("icons/machineFamily"),
			tr("Prefer this item's own artwork over the clone parent's icon"),
			m_iconMachineList, m_iconMachinePrefer, m_iconMachineFamily);
	layout->addWidget(machineBox, 1);

	QGroupBox *softwareBox = new QGroupBox(tr("Software list"), page);
	buildSection(softwareBox, QStringLiteral("icons/softwareSources"),
			QStringLiteral("icons/softwarePreferOwn"), QStringLiteral("icons/softwareFamily"),
			tr("Prefer this item's own artwork over the host machine's icon"),
			m_iconSoftwareList, m_iconSoftwarePrefer, m_iconSoftwareFamily);
	layout->addWidget(softwareBox, 1);

	addCategory(tr("Row Icons"), page, tr("icons list rows artwork priority machine software"));
}

void OptionsDialog::buildArtScaleCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	layout->addWidget(makeIntro(
			tr("How each art-view image is resampled when scaled larger than its "
			   "source.  Smooth blurs; nearest neighbour keeps crisp pixels.  Can "
			   "also be changed by right-clicking an image in the art view."), page));

	QFormLayout *form = new QFormLayout;
	form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
	layout->addLayout(form);
	layout->addStretch(1);
	int const category = addCategory(tr("Art Scaling"), page, tr("scaling smooth nearest pixel integer"));

	for (const auto &type : artScaleTypes())
	{
		QComboBox *combo = new QComboBox(page);
		combo->addItem(tr("Smooth"), int(ArtScaleSmooth));
		combo->addItem(tr("Nearest neighbour"), int(ArtScaleNearest));
		int const idx = combo->findData(artScaleMode(type.second));
		combo->setCurrentIndex(idx >= 0 ? idx : 0);

		QCheckBox *integer = new QCheckBox(tr("Integer"), page);
		integer->setToolTip(tr("Scale only by whole-pixel multiples"));
		integer->setChecked(artScaleInteger(type.second));

		QWidget *row = new QWidget(page);
		QHBoxLayout *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->addWidget(combo, 1);
		rowLayout->addWidget(integer);
		form->addRow(type.first, row);
		m_artScaleEditors.push_back({ type.second, combo, integer });
		registerRow(category, nullptr, 0, form, row, type.first + QLatin1String(" art scaling"));
	}
}

void OptionsDialog::buildFolderCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	layout->addWidget(makeIntro(
			tr("MAME EXTRAs used by the front-end.  Art sets are search paths: add "
			   "any number of folders or .zip archives and they are tried top to "
			   "bottom, so a second media set (a Multimedia pack, an "
			   "optimize-sl-media.py or LaunchBox import tree, …) simply goes below "
			   "the first.  Snapshots and artwork for the emulator itself are "
			   "configured under Emulator ▸ Paths."), page));

	// Settings that belong to no single tab: one-click setup and playback.
	QFormLayout *top = new QFormLayout;
	top->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	QWidget *fillRow = new QWidget(page);
	QHBoxLayout *fillH = new QHBoxLayout(fillRow);
	fillH->setContentsMargins(0, 0, 0, 0);
	QPushButton *fill = new QPushButton(tr("Auto-fill from a media folder…"), fillRow);
	fill->setToolTip(tr("Pick the top folder of a MAME EXTRAs download, a Multimedia pack or a "
						"generated media tree.  Everything inside named after an art type "
						"(snap.zip, titles/, dats/history.xml, box_sl/, …) is added to the "
						"matching setting on every tab: search paths gain an entry, single "
						"folders and files are filled only while still empty.  Repeat for "
						"each root you have; each one is appended below the previous."));
	connect(fill, &QPushButton::clicked, this, &OptionsDialog::autoFillFolders);
	fillH->addWidget(fill);
	fillH->addWidget(makeMuted(tr("Adds to the lists on every tab; run once per media folder."), fillRow), 1);
	top->addRow(tr("Quick setup"), fillRow);

	m_videoAutoplay = new QCheckBox(tr("Auto-play videos when a system is selected"), page);
	m_videoAutoplay->setChecked(QSettings().value(QStringLiteral("artwork/videoAutoplay"), true).toBool());
	m_videoAutoplay->setToolTip(tr("When off, the video tab loads paused and you press Play yourself."));
	top->addRow(tr("Video"), m_videoAutoplay);
	layout->addLayout(top);

	// One tab per folder group, each a scrolling form.
	QTabWidget *tabs = new QTabWidget(page);
	struct Tab { FolderGroup group; QString title; QFormLayout *form; QWidget *page; int index; };
	Tab tabPages[] = {
		{ FolderGroup::MachineArt,  tr("Machine Art"),  nullptr, nullptr, -1 },
		{ FolderGroup::SoftwareArt, tr("Software Art"), nullptr, nullptr, -1 },
		{ FolderGroup::DataFiles,   tr("Data Files"),   nullptr, nullptr, -1 },
	};
	for (Tab &tab : tabPages)
	{
		QWidget *tabPage = new QWidget;
		QVBoxLayout *tabLayout = new QVBoxLayout(tabPage);
		tab.form = new QFormLayout;
		tab.form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
		tab.form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
		tabLayout->addLayout(tab.form);
		tabLayout->addStretch(1);
		tab.page = tabPage;
		QScrollArea *scroll = new QScrollArea(tabs);
		scroll->setWidgetResizable(true);
		scroll->setFrameShape(QFrame::NoFrame);
		scroll->setWidget(tabPage);
		tab.index = tabs->addTab(scroll, tab.title);
	}
	layout->addWidget(tabs, 1);
	int const category = addCategory(tr("Front-end Folders"), page);

	for (std::size_t i = 0; i < FRONTEND_FOLDER_COUNT; i++)
	{
		const FrontendFolder &folder = FRONTEND_FOLDERS[i];
		QString const key = QString::fromLatin1(folder.key);
		QString const label = QString::fromLatin1(folder.label);

		const Tab *tab = nullptr;
		for (const Tab &t : tabPages)
			if (t.group == folder.group)
				tab = &t;
		if (!tab)
			continue;
		QFormLayout *form = tab->form;

		// On the Software Art tab the "Software " prefix is implied by the tab.
		QString rowLabel = label;
		if (folder.group == FolderGroup::SoftwareArt && rowLabel.startsWith(QLatin1String("Software ")))
		{
			rowLabel.remove(0, 9);
			rowLabel[0] = rowLabel[0].toUpper();
		}

		QWidget *tabPage = tab->page;
		QWidget *row = nullptr;
		if (folder.kind == FolderKind::SearchPath)
		{
			QListWidget *list = makeSearchPathEditor(tabPage, frontendFolderPathList(key), true, row);
			m_folderEditors.push_back({ key, label, list });
		}
		else
		{
			row = new QWidget(tabPage);
			QHBoxLayout *h = new QHBoxLayout(row);
			h->setContentsMargins(0, 0, 0, 0);
			QLineEdit *edit = new QLineEdit(frontendFolderPath(key), row);
			h->addWidget(edit, 1);

			if (folder.kind == FolderKind::File)
			{
				QPushButton *file = new QPushButton(tr("File…"), row);
				connect(file, &QPushButton::clicked, this, [this, edit] {
					QString const picked = QFileDialog::getOpenFileName(this, tr("Select File"));
					if (!picked.isEmpty())
						edit->setText(picked);
				});
				h->addWidget(file);
			}
			else
			{
				QPushButton *dir = new QPushButton(tr("Folder…"), row);
				connect(dir, &QPushButton::clicked, this, [this, edit] {
					QString const picked = QFileDialog::getExistingDirectory(this, tr("Select Folder"));
					if (!picked.isEmpty())
						edit->setText(picked);
				});
				h->addWidget(dir);
			}
			m_folderEditors.push_back({ key, label, edit });
		}

		form->addRow(rowLabel, row);
		registerRow(category, tabs, tab->index, form, row,
				label + QLatin1Char(' ') + key + QLatin1Char(' ') + tab->title + QLatin1String(" folders extras art"));
	}
}

void OptionsDialog::autoFillFolders()
{
	QString const root = QFileDialog::getExistingDirectory(this, tr("Select Media Folder"));
	if (root.isEmpty())
		return;

	QHash<QString, QString> const found = discoverFrontendFolders(root);
	QStringList added, unchanged;
	for (const FolderEditor &folder : m_folderEditors)
	{
		auto const it = found.constFind(folder.key);
		if (it == found.constEnd())
			continue;
		if (auto *list = qobject_cast<QListWidget *>(folder.widget))
		{
			if (searchPathEntries(list).contains(it.value()))
			{
				unchanged.append(folder.label);
				continue;
			}
			addPathItem(list, it.value());
			added.append(folder.label);
		}
		else if (auto *edit = qobject_cast<QLineEdit *>(folder.widget))
		{
			if (edit->text().trimmed().isEmpty())
			{
				edit->setText(it.value());
				added.append(folder.label);
			}
			else
			{
				unchanged.append(folder.label);
			}
		}
	}

	if (added.isEmpty() && unchanged.isEmpty())
	{
		QMessageBox::information(this, tr("Auto-fill"),
				tr("Nothing recognised under\n%1\n\nExpected folders or archives named after an art "
				   "type (snap, titles, flyers, box_sl, …) or data files such as dats/history.xml.").arg(root));
		return;
	}

	QString text = tr("From %1:").arg(root);
	if (!added.isEmpty())
		text += tr("\n\nAdded (%1):\n%2").arg(added.size()).arg(added.join(QLatin1String(", ")));
	if (!unchanged.isEmpty())
		text += tr("\n\nAlready configured, left as is (%1):\n%2").arg(unchanged.size()).arg(unchanged.join(QLatin1String(", ")));
	text += tr("\n\nNothing is saved until you press OK.");
	QMessageBox::information(this, tr("Auto-fill"), text);
}


//-------------------------------------------------
//  save
//-------------------------------------------------

void OptionsDialog::accept()
{
	// Collect changed mame.ini options.
	std::vector<std::pair<std::string, std::string>> changes;
	for (const Editor &editor : m_editors)
	{
		QString const current = editorValue(editor);
		if (current != editor.original)
			changes.emplace_back(editor.name.toStdString(), current.toStdString());
	}

	if (!changes.empty())
	{
		std::string path;
		bool const ok = m_system.isEmpty()
				? qtui_write_options(changes, &path)
				: qtui_write_game_options(m_system.toStdString(), changes, &path);
		if (!ok)
		{
			QMessageBox::warning(this, windowTitle(),
					m_system.isEmpty() ? tr("Failed to write mame.ini.")
									   : tr("Failed to write the per-machine ini."));
			return;
		}

		for (const auto &change : changes)
			if (change.first == "rompath" || change.first == "hashpath")
				m_romPathsChanged = true;
	}

	// Front-end folders and playback preferences are global-only settings.
	if (m_system.isEmpty())
	{
		for (const FolderEditor &folder : m_folderEditors)
		{
			QString value;
			if (auto *list = qobject_cast<QListWidget *>(folder.widget))
				value = searchPathEntries(list).join(QLatin1Char(';'));
			else if (auto *edit = qobject_cast<QLineEdit *>(folder.widget))
				value = edit->text().trimmed();
			setFrontendFolderPath(folder.key, value);
		}
		if (m_videoAutoplay)
			QSettings().setValue(QStringLiteral("artwork/videoAutoplay"), m_videoAutoplay->isChecked());
		if (m_noNagInfo)
			QSettings().setValue(QStringLiteral("play/noNagInfo"), m_noNagInfo->isChecked());
		if (m_noNagWarnings)
			QSettings().setValue(QStringLiteral("play/noNagWarnings"), m_noNagWarnings->isChecked());
		if (m_noNagLoading)
			QSettings().setValue(QStringLiteral("play/noNagLoading"), m_noNagLoading->isChecked());

		// Versions & Regions.
		if (m_versionMode)
			QSettings().setValue(QStringLiteral("versions/mode"), m_versionMode->currentData().toInt());
		if (m_useSystemRegion)
			QSettings().setValue(QStringLiteral("versions/useSystemRegion"), m_useSystemRegion->isChecked());
		if (m_regionList)
		{
			QStringList order;
			for (int i = 0; i < m_regionList->count(); i++)
			{
				QListWidgetItem *item = m_regionList->item(i);
				if (item->checkState() == Qt::Checked)
					order << item->text();
			}
			QSettings().setValue(QStringLiteral("versions/order"), order);
		}

		// Grid artwork fallback.
		if (m_gridArtFamily)
			QSettings().setValue(QStringLiteral("grid/artFallbackFamily"), m_gridArtFamily->isChecked());
		if (m_gridArtList)
		{
			QStringList order;
			for (int i = 0; i < m_gridArtList->count(); i++)
			{
				QListWidgetItem *item = m_gridArtList->item(i);
				if (item->checkState() == Qt::Checked)
					order << item->text();
			}
			QSettings().setValue(QStringLiteral("grid/artFallbackOrder"), order);
		}

		// Row-icon source priority lists (ids, in order) + per-list toggles.
		auto saveIconList = [] (QListWidget *list, const QString &orderKey) {
			if (!list)
				return;
			QStringList ids;
			for (int i = 0; i < list->count(); i++)
			{
				QListWidgetItem *item = list->item(i);
				if (item->checkState() == Qt::Checked)
					ids << item->data(Qt::UserRole).toString();
			}
			QSettings().setValue(orderKey, ids);
		};
		saveIconList(m_iconMachineList, QStringLiteral("icons/machineSources"));
		saveIconList(m_iconSoftwareList, QStringLiteral("icons/softwareSources"));
		if (m_iconMachinePrefer)
			QSettings().setValue(QStringLiteral("icons/machinePreferOwn"), m_iconMachinePrefer->isChecked());
		if (m_iconMachineFamily)
			QSettings().setValue(QStringLiteral("icons/machineFamily"), m_iconMachineFamily->isChecked());
		if (m_iconSoftwarePrefer)
			QSettings().setValue(QStringLiteral("icons/softwarePreferOwn"), m_iconSoftwarePrefer->isChecked());
		if (m_iconSoftwareFamily)
			QSettings().setValue(QStringLiteral("icons/softwareFamily"), m_iconSoftwareFamily->isChecked());

		// Per-art-type image scaling.
		for (const ArtScaleEditor &e : m_artScaleEditors)
		{
			setArtScaleMode(e.key, e.combo->currentData().toInt());
			setArtScaleInteger(e.key, e.integer->isChecked());
		}
	}

	QDialog::accept();
}

} // namespace osd::qtui
