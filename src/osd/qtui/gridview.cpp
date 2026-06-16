// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  gridview.cpp - thumbnail grid view for the machine/software lists
//
//============================================================

#include "gridview.h"

#include "thumbnailloader.h"   // kThumbnailRole

#include <QtCore/QEvent>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QLineEdit>


namespace osd::qtui {

namespace {

constexpr int kMargin = 6;       // padding inside each tile
constexpr int kCaptionGap = 3;   // gap between image and caption
constexpr int kIdRole = Qt::UserRole + 1;   // CheckableComboBox item id

} // anonymous namespace

//============================================================
//  CheckableComboBox
//============================================================

CheckableComboBox::CheckableComboBox(QWidget *parent) :
	QComboBox(parent)
{
	m_model = new QStandardItemModel(this);
	setModel(m_model);

	// An editable-but-read-only line edit lets us show a custom summary string.
	setEditable(true);
	lineEdit()->setReadOnly(true);
	lineEdit()->installEventFilter(this);
	view()->viewport()->installEventFilter(this);
}

void CheckableComboBox::addCheckItem(const QString &text, int id, bool checked)
{
	auto *item = new QStandardItem(text);
	item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
	item->setData(checked ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
	item->setData(id, kIdRole);
	m_model->appendRow(item);
	updateText();
}

QList<int> CheckableComboBox::checkedIds() const
{
	QList<int> ids;
	for (int i = 0; i < m_model->rowCount(); i++)
	{
		QStandardItem *item = m_model->item(i);
		if (item->data(Qt::CheckStateRole).toInt() == Qt::Checked)
			ids << item->data(kIdRole).toInt();
	}
	return ids;
}

void CheckableComboBox::setCheckedIds(const QList<int> &ids)
{
	for (int i = 0; i < m_model->rowCount(); i++)
	{
		QStandardItem *item = m_model->item(i);
		bool const on = ids.contains(item->data(kIdRole).toInt());
		item->setData(on ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
	}
	updateText();
	emit checkedChanged();
}

bool CheckableComboBox::eventFilter(QObject *object, QEvent *event)
{
	// Open the popup when the (read-only) line edit is clicked.
	if (object == lineEdit() && event->type() == QEvent::MouseButtonRelease)
	{
		showPopup();
		return true;
	}

	// Toggle an item's check state on click without closing the popup.
	if (object == view()->viewport() && event->type() == QEvent::MouseButtonRelease)
	{
		auto *me = static_cast<QMouseEvent *>(event);
		QModelIndex const index = view()->indexAt(me->pos());
		if (index.isValid())
		{
			QStandardItem *item = m_model->itemFromIndex(index);
			item->setData(item->data(Qt::CheckStateRole).toInt() == Qt::Checked
					? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
			updateText();
			emit checkedChanged();
		}
		return true;   // keep the popup open
	}

	return QComboBox::eventFilter(object, event);
}

void CheckableComboBox::updateText()
{
	QStringList parts;
	for (int i = 0; i < m_model->rowCount(); i++)
	{
		QStandardItem *item = m_model->item(i);
		if (item->data(Qt::CheckStateRole).toInt() == Qt::Checked)
			parts << item->text();
	}
	lineEdit()->setText(parts.isEmpty() ? tr("(none)") : parts.join(QStringLiteral(", ")));
}

//============================================================
//  GridDelegate
//============================================================

GridDelegate::GridDelegate(QObject *parent) :
	QStyledItemDelegate(parent)
{
}

void GridDelegate::setThumbnailSize(int size)
{
	m_thumbSize = qMax(32, size);
}

void GridDelegate::setCaptionColumns(const QList<int> &columns)
{
	m_captionColumns = columns;
}

void GridDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	painter->save();

	bool const selected = option.state & QStyle::State_Selected;
	if (selected)
		painter->fillRect(option.rect, option.palette.highlight());

	QRect const inner = option.rect.adjusted(kMargin, kMargin, -kMargin, -kMargin);
	QRect const imageRect(inner.left(), inner.top(), inner.width(), m_thumbSize);

	QPixmap const pixmap = index.data(kThumbnailRole).value<QPixmap>();
	if (!pixmap.isNull())
	{
		QPixmap const scaled = pixmap.scaled(imageRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
		painter->drawPixmap(imageRect.center().x() - scaled.width() / 2,
				imageRect.center().y() - scaled.height() / 2, scaled);
	}
	else
	{
		// Placeholder frame until (or if) art arrives.
		painter->setPen(option.palette.color(QPalette::Mid));
		painter->drawRect(imageRect.adjusted(0, 0, -1, -1));
	}

	painter->setPen(selected ? option.palette.highlightedText().color() : option.palette.text().color());
	int const lineHeight = option.fontMetrics.height();
	int y = imageRect.bottom() + kCaptionGap;
	for (int column : m_captionColumns)
	{
		if (y + lineHeight > inner.bottom() + 1)
			break;
		QString const text = index.siblingAtColumn(column).data(Qt::DisplayRole).toString();
		QString const elided = option.fontMetrics.elidedText(text, Qt::ElideRight, inner.width());
		painter->drawText(QRect(inner.left(), y, inner.width(), lineHeight), Qt::AlignHCenter | Qt::AlignTop, elided);
		y += lineHeight;
	}

	painter->restore();
}

QSize GridDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const
{
	int const lines = m_captionColumns.size();
	int const captionHeight = lines > 0 ? lines * option.fontMetrics.height() + kCaptionGap + 2 : 0;
	return QSize(m_thumbSize + 2 * kMargin, m_thumbSize + captionHeight + 2 * kMargin);
}

//============================================================
//  GridView
//============================================================

GridView::GridView(QWidget *parent) :
	QListView(parent)
{
	m_delegate = new GridDelegate(this);
	setItemDelegate(m_delegate);

	setViewMode(QListView::IconMode);
	setResizeMode(QListView::Adjust);
	setMovement(QListView::Static);
	setUniformItemSizes(true);
	setWordWrap(false);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setSelectionBehavior(QAbstractItemView::SelectItems);
	setSpacing(4);

	setThumbnailSize(m_thumbSize);
}

void GridView::setThumbnailSize(int size)
{
	m_thumbSize = qMax(32, size);
	m_delegate->setThumbnailSize(m_thumbSize);
	setIconSize(QSize(m_thumbSize, m_thumbSize));
	// The delegate's sizeHint changed; force a relayout.
	scheduleDelayedItemsLayout();
}

void GridView::setCaptionColumns(const QList<int> &columns)
{
	m_delegate->setCaptionColumns(columns);
	scheduleDelayedItemsLayout();   // caption line count may change the tile height
}

} // namespace osd::qtui
