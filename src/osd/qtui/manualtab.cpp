// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  manualtab.cpp - PDF manual viewer tab for the artwork panel
//
//============================================================

#include "manualtab.h"

#include <QtCore/QBuffer>
#include <QtPdf/QPdfDocument>
#include <QtPdfWidgets/QPdfView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QStackedLayout>


namespace osd::qtui {

ManualTab::ManualTab(QWidget *parent) :
	QWidget(parent)
{
	m_doc = new QPdfDocument(this);

	m_view = new QPdfView(this);
	m_view->setDocument(m_doc);
	m_view->setPageMode(QPdfView::PageMode::MultiPage);
	m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);

	m_status = new QLabel(tr("No manual"), this);
	m_status->setAlignment(Qt::AlignCenter);

	m_stack = new QStackedLayout(this);
	m_stack->setContentsMargins(0, 0, 0, 0);
	m_stack->addWidget(m_status);
	m_stack->addWidget(m_view);
	m_stack->setCurrentWidget(m_status);
}

ManualTab::~ManualTab() = default;

void ManualTab::setMessage(const QString &message)
{
	m_doc->close();
	m_buffer.reset();
	m_data.clear();
	m_status->setText(message);
	m_stack->setCurrentWidget(m_status);
}

void ManualTab::setPdf(const QByteArray &bytes)
{
	if (bytes.isEmpty())
	{
		setMessage(tr("No manual"));
		return;
	}

	// Release any previous document before swapping the backing buffer.
	m_doc->close();
	m_buffer.reset();

	m_data = bytes;
	m_buffer = std::make_unique<QBuffer>(&m_data);
	m_buffer->open(QIODevice::ReadOnly);
	m_doc->load(m_buffer.get());

	if (m_doc->status() == QPdfDocument::Status::Ready)
	{
		m_stack->setCurrentWidget(m_view);
	}
	else
	{
		m_buffer.reset();
		m_data.clear();
		setMessage(tr("Could not open manual."));
	}
}

} // namespace osd::qtui
