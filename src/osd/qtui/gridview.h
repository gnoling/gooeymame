// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gridview.h - thumbnail grid view for the machine/software lists
//
//  A QListView in icon mode that paints kThumbnailRole as a scaled image
//  with one or more caption lines beneath, sized by a runtime slider.  It is
//  driven by the same proxy model and selection model as the table view, so
//  all filters, search, selection, and launch behaviour are shared.
//
//  Also provides CheckableComboBox: a compact drop-down of checkable items
//  used to pick which fields appear in the caption.
//
//============================================================
#ifndef MAME_OSD_QTUI_GRIDVIEW_H
#define MAME_OSD_QTUI_GRIDVIEW_H

#pragma once

#include <QtCore/QList>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QListView>
#include <QtWidgets/QStyledItemDelegate>

class QEvent;
class QObject;
class QStandardItemModel;

namespace osd::qtui {

//============================================================
//  A combo box whose drop-down lists checkable items; the closed box shows a
//  summary of the checked entries.  Used for multi-select caption fields.
//============================================================
class CheckableComboBox : public QComboBox
{
	Q_OBJECT

public:
	explicit CheckableComboBox(QWidget *parent = nullptr);

	void addCheckItem(const QString &text, int id, bool checked);
	QList<int> checkedIds() const;
	void setCheckedIds(const QList<int> &ids);

signals:
	void checkedChanged();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void updateText();

	QStandardItemModel *m_model = nullptr;
};

//============================================================
//  Delegate painting one thumbnail tile (image + caption lines).
//============================================================
class GridDelegate : public QStyledItemDelegate
{
	Q_OBJECT

public:
	explicit GridDelegate(QObject *parent = nullptr);

	void setThumbnailSize(int size);
	void setCaptionColumns(const QList<int> &columns);

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
	QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
	int m_thumbSize = 128;
	QList<int> m_captionColumns{ 0 };
};

//============================================================
//  The grid view itself.
//============================================================
class GridView : public QListView
{
	Q_OBJECT

public:
	explicit GridView(QWidget *parent = nullptr);

	void setThumbnailSize(int size);                       // image edge length in pixels
	void setCaptionColumns(const QList<int> &columns);     // model columns shown as caption lines

private:
	GridDelegate *m_delegate = nullptr;
	int m_thumbSize = 128;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_GRIDVIEW_H
