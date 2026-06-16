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
#include <QtCore/QString>

#include <vector>

class QCheckBox;
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
	explicit OptionsDialog(QWidget *parent = nullptr);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void accept() override;

private:
	void addCategory(const QString &title, QWidget *page);
	void buildOptionCategories();
	void buildFolderCategory();
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
	QHash<QObject *, QString> m_help;   // editor widget -> description
	std::vector<Editor> m_editors;
	std::vector<FolderEditor> m_folderEditors;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_OPTIONSDIALOG_H
