// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  embedhost.cpp - native host widget for embedded MAME gameplay
//
//============================================================

#include "embedhost.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>


namespace osd::qtui {

//============================================================
//  NativeSurface - a bare native window MAME renders into.
//
//  A real X subwindow (WA_NativeWindow) with its own XID, which SDL's
//  SDL_CreateWindowFrom() adopts.  We deliberately do NOT use WA_PaintOnScreen:
//  it made Qt mis-size the early-created window and recreate (invalidate) the
//  XID on resize, so the embedded video appeared tiny and vanished when the
//  window was resized.  WA_OpaquePaintEvent + an empty paintEvent + no system
//  background stop Qt from clearing the surface MAME owns, while the XID stays
//  stable across resizes.  winId() is taken lazily (at launch, after layout)
//  rather than in the constructor so the window is created at its real size.
//============================================================
class NativeSurface : public QWidget
{
public:
	explicit NativeSurface(QWidget *parent) : QWidget(parent)
	{
		setAttribute(Qt::WA_NativeWindow);
		// NB: do NOT set WA_DontCreateNativeAncestors — when the host is nested
		// (e.g. inside the artwork pane) Qt must promote the intermediate
		// parents to native windows, otherwise the surface is mis-clipped and
		// renders nothing.  A top-level embed window happens to work either way.
		setAttribute(Qt::WA_NoSystemBackground);
		setAttribute(Qt::WA_OpaquePaintEvent);
		setAutoFillBackground(false);
		setFocusPolicy(Qt::StrongFocus);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setMinimumSize(320, 240);
	}

protected:
	void paintEvent(QPaintEvent *) override { /* MAME owns the pixels */ }
};


EmbedHost::EmbedHost(QWidget *parent) :
	QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	m_surface = new NativeSurface(this);
	layout->addWidget(m_surface);

	m_overlay = new QLabel(m_surface);
	m_overlay->setAlignment(Qt::AlignCenter);
	m_overlay->setStyleSheet(QStringLiteral(
			"QLabel { color: white; background: #202020; font-size: 16px; }"));
	m_overlay->setText(tr("Launching…"));
	m_overlay->setGeometry(m_surface->rect());
	m_overlay->show();
}

EmbedHost::~EmbedHost()
{
	// Tear the child down WITHOUT emitting finished(): during application
	// shutdown the receiver (MainWindow) may already be partly destroyed, and
	// invoking its slot then asserts ("object is not of the correct type").
	if (m_process)
	{
		m_process->disconnect();
		if (m_process->state() != QProcess::NotRunning)
		{
			m_process->terminate();
			if (!m_process->waitForFinished(3000))
				m_process->kill();
		}
	}
}

unsigned long long EmbedHost::surfaceId() const
{
	return static_cast<unsigned long long>(m_surface->winId());
}

void EmbedHost::setStatus(const QString &text)
{
	m_overlay->setText(text);
}

void EmbedHost::showOverlay(bool visible)
{
	if (visible)
		m_overlay->setGeometry(m_surface->rect());
	m_overlay->setVisible(visible);
	if (visible)
		m_overlay->raise();
}

void EmbedHost::startChildProcess(const QStringList &baseArgs, bool wantsGl)
{
	if (isChildRunning())
		return;

	showOverlay(true);
	setStatus(tr("Launching…"));

	m_process = new QProcess(this);
	m_process->setProcessChannelMode(QProcess::ForwardedChannels);

	// Tell SDL whether to make our window OpenGL-capable (see the matching
	// SDL_SetHint() call in qtui_run_embedded()).  Only set it for GL-based
	// renderers: the flag breaks the software renderer, and combining it with
	// the Vulkan flag fails outright, so the Vulkan hint is never set.
	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert(QStringLiteral("SDL_VIDEO_FOREIGN_WINDOW_OPENGL"),
			wantsGl ? QStringLiteral("1") : QStringLiteral("0"));
	m_process->setProcessEnvironment(env);

	QStringList args = baseArgs;
	args << QStringLiteral("-attach_window") << QString::number(surfaceId());

	connect(m_process, &QProcess::started, this, [this] {
		// Hide the overlay shortly after start so MAME's first frames show.
		showOverlay(false);
		m_surface->setFocus();
	});
	connect(m_process,
			QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
			this,
			[this] (int code, QProcess::ExitStatus) {
				m_process->deleteLater();
				m_process = nullptr;
				emit finished(code);
			});
	connect(m_process, &QProcess::errorOccurred, this, [this] (QProcess::ProcessError) {
		setStatus(tr("Failed to launch MAME."));
	});

	m_process->start(QCoreApplication::applicationFilePath(), args);
}

bool EmbedHost::isChildRunning() const
{
	return m_process && m_process->state() != QProcess::NotRunning;
}

void EmbedHost::stopChild()
{
	if (!isChildRunning())
		return;
	m_process->terminate();
	if (!m_process->waitForFinished(3000))
		m_process->kill();
}

} // namespace osd::qtui
