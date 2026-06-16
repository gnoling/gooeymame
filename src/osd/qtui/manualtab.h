// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  manualtab.h - PDF manual viewer tab for the artwork panel
//
//  Displays a manual PDF (manuals / manuals_SL, keyed like the image sets)
//  using Qt PDF.  The bytes are loaded off the UI thread and handed in via
//  setPdf(); a QBuffer over the data backs the document for its lifetime.
//
//  Plain QWidget subclass (no Q_OBJECT): all wiring uses lambda connections.
//
//============================================================
#ifndef MAME_OSD_QTUI_MANUALTAB_H
#define MAME_OSD_QTUI_MANUALTAB_H

#pragma once

#include <QtWidgets/QWidget>

#include <QtCore/QByteArray>
#include <QtCore/QString>

#include <memory>

class QLabel;
class QStackedLayout;

QT_BEGIN_NAMESPACE
class QBuffer;
class QPdfDocument;
class QPdfView;
QT_END_NAMESPACE

namespace osd::qtui {

class ManualTab : public QWidget
{
public:
	explicit ManualTab(QWidget *parent = nullptr);
	~ManualTab() override;

	// Show the given PDF bytes (empty shows the "No manual" placeholder).
	void setPdf(const QByteArray &bytes);
	// Show a status message instead of a document (and release any loaded one).
	void setMessage(const QString &message);

private:
	QPdfDocument *m_doc = nullptr;
	QPdfView *m_view = nullptr;
	QLabel *m_status = nullptr;
	QStackedLayout *m_stack = nullptr;
	std::unique_ptr<QBuffer> m_buffer;
	QByteArray m_data;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_MANUALTAB_H
