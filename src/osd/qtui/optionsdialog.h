// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  optionsdialog.h - editor for mame.ini options and front-end folders
//
//  Category list on the left, the selected category's options on the right,
//  and a hover-driven description area at the bottom.  Writes mame.ini and
//  QSettings on OK.
//
//============================================================
#ifndef MAME_OSD_QTUI_OPTIONSDIALOG_H
#define MAME_OSD_QTUI_OPTIONSDIALOG_H

#pragma once

#include <QtWidgets/QDialog>

#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtCore/QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QEvent;
class QFormLayout;
class QLabel;
class QListWidget;
class QObject;
class QStackedWidget;
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

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void accept() override;

private:
	void buildUi();   // shared by both constructors
	void addCategory(const QString &title, QWidget *page);
	void buildOptionCategories();
	void buildFolderCategory();
	void buildVersionsCategory();
	void buildGridArtCategory();
	void addOptionRow(QFormLayout *form, const qtui_option &opt);

	struct Editor
	{
		QString name;       // mame.ini option name
		int type;           // qtui_option_type
		QWidget *widget;    // QCheckBox / QLineEdit / QListWidget (multipath)
		QString original;   // value at load time
	};

	struct FolderEditor
	{
		QString key;
		QWidget *widget;    // QLineEdit
	};

	QListWidget *m_categoryList = nullptr;
	QStackedWidget *m_stack = nullptr;
	QLabel *m_description = nullptr;
	QCheckBox *m_videoAutoplay = nullptr;
	QComboBox *m_versionMode = nullptr;       // Versions & Regions page
	QCheckBox *m_useSystemRegion = nullptr;
	QListWidget *m_regionList = nullptr;      // checkable, reorderable priority
	QListWidget *m_gridArtList = nullptr;     // Grid Artwork page: fallback order
	QCheckBox *m_gridArtFamily = nullptr;     // try related sets (parent/regions)
	QString m_system;                   // empty = global mame.ini; else per-machine
	QString m_systemDescription;        // machine friendly name (per-machine mode)
	QSet<QString> m_overridden;         // option names set by the machine's ini
	QHash<QObject *, QString> m_help;   // editor widget -> description
	std::vector<Editor> m_editors;
	std::vector<FolderEditor> m_folderEditors;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_OPTIONSDIALOG_H
