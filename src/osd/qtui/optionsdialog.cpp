// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  optionsdialog.cpp - editor for mame.ini options and front-end folders
//
//============================================================

#include "optionsdialog.h"

#include "emulator.h"
#include "frontendpaths.h"
#include "gamelistmodel.h"
#include "regions.h"

#include <QtCore/QEvent>
#include <QtCore/QSettings>
#include <QtGui/QDoubleValidator>
#include <QtGui/QFont>
#include <QtGui/QIntValidator>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>


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
	{ "sound",            "auto,sdl,portaudio,pipewire,none" },
	{ "keyboardprovider", "auto,sdl,x11,none" },
	{ "mouseprovider",    "auto,sdl,x11,none" },
	{ "lightgunprovider", "auto,sdl,x11,none" },
	{ "joystickprovider", "auto,sdl,sdlgame,none" },
	{ "monitorprovider",  "auto,sdl,x11" },
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

} // anonymous namespace

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
	resize(820, 620);

	m_categoryList = new QListWidget(this);
	m_categoryList->setMaximumWidth(220);
	m_stack = new QStackedWidget(this);
	connect(m_categoryList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

	buildOptionCategories();
	if (!gameMode)
	{
		buildVersionsCategory();   // global only
		buildFolderCategory();     // front-end folders are global only
	}
	if (m_categoryList->count() > 0)
		m_categoryList->setCurrentRow(0);

	// Hover-driven description area.
	m_description = new QLabel(tr("Hover over an option to see its description."), this);
	m_description->setWordWrap(true);
	m_description->setFrameShape(QFrame::StyledPanel);
	m_description->setMinimumHeight(56);
	m_description->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	m_description->setMargin(6);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &OptionsDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &OptionsDialog::reject);

	QHBoxLayout *top = new QHBoxLayout;
	top->addWidget(m_categoryList);
	top->addWidget(m_stack, 1);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(top, 1);
	layout->addWidget(m_description);
	layout->addWidget(buttons);
}

void OptionsDialog::addCategory(const QString &title, QWidget *page)
{
	QScrollArea *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setWidget(page);
	m_stack->addWidget(scroll);
	m_categoryList->addItem(title);
}

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

	// Build one left-list category per top group, with sub-tabs inside.
	for (int t = 0; t < int(topOrder.size()); t++)
	{
		QWidget *page = new QWidget;
		QVBoxLayout *outer = new QVBoxLayout(page);
		outer->setContentsMargins(0, 0, 0, 0);

		QTabWidget *subtabs = new QTabWidget(page);
		for (const Sub &sub : buckets[t])
		{
			QWidget *subPage = new QWidget;
			QFormLayout *form = new QFormLayout(subPage);
			form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
			for (const qtui_option &opt : sub.group->options)
				addOptionRow(form, opt);

			QScrollArea *scroll = new QScrollArea;
			scroll->setWidgetResizable(true);
			scroll->setWidget(subPage);
			subtabs->addTab(scroll, sub.title);
		}
		outer->addWidget(subtabs);

		m_stack->addWidget(page);
		m_categoryList->addItem(topOrder[t]);
	}
}

void OptionsDialog::addOptionRow(QFormLayout *form, const qtui_option &opt)
{
	QString const name = QString::fromStdString(opt.name);
	QString const value = QString::fromStdString(opt.value);
	QString const help = QString::fromStdString(opt.description);
	QWidget *readWidget = nullptr;   // widget queried in accept()

	if (opt.type == QTUI_OPT_BOOLEAN)
	{
		QCheckBox *check = new QCheckBox;
		check->setChecked(value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
		form->addRow(name, check);
		readWidget = check;
	}
	else if (opt.type == QTUI_OPT_MULTIPATH)
	{
		// List of paths with add/remove.
		QWidget *row = new QWidget;
		QVBoxLayout *col = new QVBoxLayout(row);
		col->setContentsMargins(0, 0, 0, 0);

		QListWidget *list = new QListWidget(row);
		list->setMaximumHeight(96);
		for (const QString &p : value.split(QLatin1Char(';'), Qt::SkipEmptyParts))
			list->addItem(p);

		QPushButton *add = new QPushButton(tr("Add…"), row);
		QPushButton *remove = new QPushButton(tr("Remove"), row);
		connect(add, &QPushButton::clicked, this, [this, list] {
			QString const dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
			if (!dir.isEmpty())
				list->addItem(dir);
		});
		connect(remove, &QPushButton::clicked, list, [list] {
			qDeleteAll(list->selectedItems());
		});

		QHBoxLayout *btns = new QHBoxLayout;
		btns->addWidget(add);
		btns->addWidget(remove);
		btns->addStretch(1);
		col->addWidget(list);
		col->addLayout(btns);

		form->addRow(name, row);
		readWidget = list;
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
		form->addRow(name, row);
		readWidget = edit;
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
			form->addRow(name, spin);
			readWidget = spin;
		}
		else
		{
			QLineEdit *edit = new QLineEdit(value);
			edit->setValidator(new QIntValidator(edit));
			form->addRow(name, edit);
			readWidget = edit;
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
			spin->setValue(value.toDouble());
			form->addRow(name, spin);
			readWidget = spin;
		}
		else
		{
			QLineEdit *edit = new QLineEdit(value);
			edit->setValidator(new QDoubleValidator(edit));
			form->addRow(name, edit);
			readWidget = edit;
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
			form->addRow(name, combo);
			readWidget = combo;
		}
		else
		{
			QLineEdit *edit = new QLineEdit(value);
			form->addRow(name, edit);
			readWidget = edit;
		}
	}

	bool const overridden = m_overridden.contains(name);

	// Hover help: watch the editor (and show on mouse-enter).
	if (readWidget)
	{
		readWidget->setToolTip(help);
		QString helpText = help.isEmpty() ? name : QStringLiteral("%1 — %2").arg(name, help);
		if (overridden)
			helpText = tr("[set for this machine] ") + helpText;
		m_help.insert(readWidget, helpText);
		readWidget->installEventFilter(this);

		// Bold the label of an overridden option where the form created one.
		if (overridden)
		{
			if (auto *label = qobject_cast<QLabel *>(form->labelForField(readWidget)))
			{
				QFont font = label->font();
				font.setBold(true);
				label->setFont(font);
			}
		}
	}

	m_editors.push_back({ name, opt.type, readWidget, value });
}

void OptionsDialog::buildVersionsCategory()
{
	QWidget *page = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(page);

	QLabel *intro = new QLabel(
			tr("How the front-end picks the primary version of a game that has "
			   "clones (regional/revision variants).  Affects the top item, its "
			   "artwork, and what launching a group runs."), page);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	QFormLayout *form = new QFormLayout;
	m_versionMode = new QComboBox(page);
	m_versionMode->addItem(tr("Match MAME (use the parent set)"), GameListModel::MatchParent);
	m_versionMode->addItem(tr("Promote my preferred region"), GameListModel::PromoteRegion);
	form->addRow(tr("Default version:"), m_versionMode);

	m_useSystemRegion = new QCheckBox(tr("Use my system region automatically"), page);
	form->addRow(QString(), m_useSystemRegion);
	layout->addLayout(form);

	layout->addWidget(new QLabel(tr("Region priority (top = preferred; uncheck to ignore):"), page));
	m_regionList = new QListWidget(page);
	layout->addWidget(m_regionList, 1);

	QHBoxLayout *buttons = new QHBoxLayout;
	QPushButton *up = new QPushButton(tr("Move Up"), page);
	QPushButton *down = new QPushButton(tr("Move Down"), page);
	auto move = [this] (int delta) {
		int const row = m_regionList->currentRow();
		int const target = row + delta;
		if (row < 0 || target < 0 || target >= m_regionList->count())
			return;
		QListWidgetItem *item = m_regionList->takeItem(row);
		m_regionList->insertItem(target, item);
		m_regionList->setCurrentRow(target);
	};
	connect(up, &QPushButton::clicked, this, [move] { move(-1); });
	connect(down, &QPushButton::clicked, this, [move] { move(1); });
	buttons->addWidget(up);
	buttons->addWidget(down);
	buttons->addStretch();
	layout->addLayout(buttons);

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

	addCategory(tr("Versions & Regions"), page);
}

void OptionsDialog::buildFolderCategory()
{
	QWidget *page = new QWidget;
	QFormLayout *form = new QFormLayout(page);
	form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

	QLabel *intro = new QLabel(
			tr("Folders for MAME EXTRAs used by the front-end.  Image sets may "
			   "be a folder or a .zip; snapshots and artwork are configured on "
			   "the options tabs."), page);
	intro->setWordWrap(true);
	form->addRow(intro);

	// Front-end playback behaviour.
	m_videoAutoplay = new QCheckBox(tr("Auto-play videos when a system is selected"), page);
	m_videoAutoplay->setChecked(QSettings().value(QStringLiteral("artwork/videoAutoplay"), true).toBool());
	m_videoAutoplay->setToolTip(tr("When off, the video tab loads paused and you press Play yourself."));
	form->addRow(tr("Video"), m_videoAutoplay);

	for (std::size_t i = 0; i < FRONTEND_FOLDER_COUNT; i++)
	{
		const FrontendFolder &folder = FRONTEND_FOLDERS[i];
		QString const key = QString::fromLatin1(folder.key);

		QWidget *row = new QWidget(page);
		QHBoxLayout *h = new QHBoxLayout(row);
		h->setContentsMargins(0, 0, 0, 0);
		QLineEdit *edit = new QLineEdit(frontendFolderPath(key), row);
		h->addWidget(edit, 1);

		if (folder.isFile)
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
			// Image sets can be a folder or a zip archive.
			QPushButton *dir = new QPushButton(tr("Folder…"), row);
			QPushButton *zip = new QPushButton(tr("Zip…"), row);
			connect(dir, &QPushButton::clicked, this, [this, edit] {
				QString const picked = QFileDialog::getExistingDirectory(this, tr("Select Folder"));
				if (!picked.isEmpty())
					edit->setText(picked);
			});
			connect(zip, &QPushButton::clicked, this, [this, edit] {
				QString const picked = QFileDialog::getOpenFileName(this, tr("Select Zip Archive"), QString(), tr("Zip archives (*.zip *.7z);;All files (*)"));
				if (!picked.isEmpty())
					edit->setText(picked);
			});
			h->addWidget(dir);
			h->addWidget(zip);
		}

		form->addRow(QString::fromLatin1(folder.label), row);
		m_folderEditors.push_back({ key, edit });
	}

	addCategory(tr("Front-end Folders"), page);
}

bool OptionsDialog::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::Enter)
	{
		auto it = m_help.constFind(watched);
		if (it != m_help.constEnd())
			m_description->setText(it.value());
	}
	return QDialog::eventFilter(watched, event);
}

void OptionsDialog::accept()
{
	// Collect changed mame.ini options.
	std::vector<std::pair<std::string, std::string>> changes;
	for (const Editor &editor : m_editors)
	{
		// Read the value back according to the concrete editor widget.
		QString current;
		QWidget *w = editor.widget;
		if (auto *check = qobject_cast<QCheckBox *>(w))
			current = check->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
		else if (auto *combo = qobject_cast<QComboBox *>(w))
			current = combo->currentText();
		else if (auto *spin = qobject_cast<QSpinBox *>(w))
			current = QString::number(spin->value());
		else if (auto *dspin = qobject_cast<QDoubleSpinBox *>(w))
			current = QString::number(dspin->value());
		else if (auto *list = qobject_cast<QListWidget *>(w))
		{
			QStringList parts;
			for (int i = 0; i < list->count(); i++)
				parts << list->item(i)->text();
			current = parts.join(QLatin1Char(';'));
		}
		else if (auto *edit = qobject_cast<QLineEdit *>(w))
			current = edit->text();
		else
			continue;

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
	}

	// Front-end folders and playback preferences are global-only settings.
	if (m_system.isEmpty())
	{
		for (const FolderEditor &folder : m_folderEditors)
			setFrontendFolderPath(folder.key, qobject_cast<QLineEdit *>(folder.widget)->text());
		if (m_videoAutoplay)
			QSettings().setValue(QStringLiteral("artwork/videoAutoplay"), m_videoAutoplay->isChecked());

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
	}

	QDialog::accept();
}

} // namespace osd::qtui
