// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  optionsdialog.h - editor for mame.ini options and front-end settings
//
//  A sectioned category list on the left (front-end pages, then the
//  emulator's mame.ini groups), the selected page on the right, and a
//  search box that filters every option row across all pages.  Each
//  mame.ini row shows its description inline and a reset-to-default
//  button when it differs from MAME's default.  Writes mame.ini (or the
//  per-machine ini) and QSettings on OK.
//
//============================================================
#ifndef MAME_OSD_QTUI_OPTIONSDIALOG_H
#define MAME_OSD_QTUI_OPTIONSDIALOG_H

#pragma once

#include <QtWidgets/QDialog>

#include <QtCore/QSet>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QWidget;

struct qtui_option;   // global (declared in emulator.h)

namespace osd::qtui {

class OptionsDialog : public QDialog
{
	Q_OBJECT

public:
	// Global mame.ini editor.
	explicit OptionsDialog(QWidget *parent = nullptr);
	// Per-machine properties editor (writes <inipath>/<system>.ini).
	// `description` is the machine's friendly name, shown in the title.
	explicit OptionsDialog(const QString &system, const QString &description, QWidget *parent = nullptr);

	// Whether OK saved a change to rompath/hashpath — the options that decide
	// ROM availability, so the caller knows to invalidate its audit caches.
	bool romSearchPathsChanged() const { return m_romPathsChanged; }

public slots:
	void done(int result) override;

private slots:
	void accept() override;

private:
	// A page in the stack, plus what the search box needs to decide whether
	// the page still has anything to show.
	struct Category
	{
		QListWidgetItem *item;   // entry in the left list
		int stackIndex;
		QString keywords;        // matched when the page registers no rows
		bool hasRows;            // true once any row is registered for it
	};

	// One filterable row: a QFormLayout row (label + field) that may sit on a
	// sub-tab of a page.
	struct SearchRow
	{
		int category;
		QTabWidget *tabs;        // nullptr if the page has no sub-tabs
		int tab;
		QFormLayout *form;
		QWidget *field;          // widget passed to QFormLayout::addRow
		QString text;            // lower-cased haystack
	};

	// A mame.ini option editor.
	struct Editor
	{
		QString name;            // mame.ini option name
		int type;                // qtui_option_type
		QWidget *widget;         // QCheckBox / QComboBox / QSpinBox / QDoubleSpinBox / QLineEdit / QListWidget
		QString original;        // value at load time
		QString defaultValue;
		bool hasDefault;
		QToolButton *reset;      // shown while the value differs from the default
	};

	struct FolderEditor
	{
		QString key;
		QString label;
		QWidget *widget;         // QListWidget (SearchPath kinds) or QLineEdit (Dir/File)
	};

	struct ArtScaleEditor
	{
		QString key;
		QComboBox *combo;
		QCheckBox *integer;
	};

	void buildUi();
	void addSectionHeader(const QString &title);
	int addCategory(const QString &title, QWidget *page, const QString &keywords = QString());
	void registerRow(int category, QTabWidget *tabs, int tab, QFormLayout *form, QWidget *field, const QString &text);
	void applyFilter(const QString &text);
	void selectCategory(int stackIndex);
	void restoreState();

	void buildOptionCategories();
	void buildGameplayCategory();
	void buildVersionsCategory();
	void buildGridArtCategory();
	void buildIconSourcesCategory();
	void buildArtScaleCategory();
	void buildFolderCategory();

	void addOptionRow(QFormLayout *form, const qtui_option &opt, int category, QTabWidget *tabs, int tab, const QString &context);
	QString editorValue(const Editor &editor) const;
	void setEditorValue(const Editor &editor, const QString &value);
	bool valueEquals(const Editor &editor, const QString &a, const QString &b) const;
	void refreshReset(int index);

	// Shared building blocks.
	QLabel *makeIntro(const QString &text, QWidget *parent);
	QLabel *makeMuted(const QString &text, QWidget *parent);
	QWidget *makeOrderedList(QWidget *parent, QListWidget *&list);
	QListWidget *makeSearchPathEditor(QWidget *parent, const QStringList &initial, bool allowArchives, QWidget *&container);
	static QStringList searchPathEntries(const QListWidget *list);
	void autoFillFolders();

	QLineEdit *m_search = nullptr;
	QListWidget *m_categoryList = nullptr;
	QStackedWidget *m_stack = nullptr;
	std::vector<Category> m_categories;
	std::vector<SearchRow> m_rows;

	QCheckBox *m_videoAutoplay = nullptr;
	QCheckBox *m_noNagInfo = nullptr;         // skip system-information screen on launch
	QCheckBox *m_noNagWarnings = nullptr;     // skip warning screen on launch
	QCheckBox *m_noNagLoading = nullptr;      // skip loading/initialising messages on launch
	QComboBox *m_versionMode = nullptr;       // Versions & Regions page
	QCheckBox *m_useSystemRegion = nullptr;
	QListWidget *m_regionList = nullptr;      // checkable, reorderable priority
	QListWidget *m_gridArtList = nullptr;     // Grid Artwork page: fallback order
	QCheckBox *m_gridArtFamily = nullptr;     // try related sets (parent/regions)

	// Row Icons page: independent icon-source priority per list, plus toggles.
	QListWidget *m_iconMachineList = nullptr;
	QCheckBox *m_iconMachinePrefer = nullptr;  // prefer item's own art over parent
	QCheckBox *m_iconMachineFamily = nullptr;  // also try region/family siblings
	QListWidget *m_iconSoftwareList = nullptr;
	QCheckBox *m_iconSoftwarePrefer = nullptr; // prefer item's own art over host
	QCheckBox *m_iconSoftwareFamily = nullptr;

	QString m_system;                   // empty = global mame.ini; else per-machine
	QString m_systemDescription;        // machine friendly name (per-machine mode)
	bool m_romPathsChanged = false;     // OK saved a rompath/hashpath change
	QSet<QString> m_overridden;         // option names set by the machine's ini
	std::vector<Editor> m_editors;
	std::vector<FolderEditor> m_folderEditors;
	std::vector<ArtScaleEditor> m_artScaleEditors;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_OPTIONSDIALOG_H
