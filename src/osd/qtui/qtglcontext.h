// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtglcontext.h - Qt-native OpenGL context for the qtui OSD
//
//  Mirrors src/osd/modules/render/sdlglcontext.h, but binds MAME's OpenGL
//  renderer (drawogl) to a QOpenGLContext targeting a QWindow instead of an
//  SDL_Window.  Construction (and every make_current/swap/get_proc_address)
//  happens on the emulation worker thread: renderer_ogl::create() runs there,
//  so the QOpenGLContext takes worker-thread affinity from birth.  The QWindow
//  itself is created/shown on the Qt GUI thread; makeCurrent(window) is the one
//  sanctioned cross-thread touch (Qt's threaded-OpenGL render model).
//
//============================================================
#ifndef MAME_OSD_QTUI_QTGLCONTEXT_H
#define MAME_OSD_QTUI_QTGLCONTEXT_H

#pragma once

#include "modules/opengl/osd_opengl.h"

#if USE_OPENGL

#include <string>

#include <QtGui/QOpenGLContext>
#include <QtGui/QSurfaceFormat>
#include <QtGui/QWindow>


namespace osd::qtui {

class qt_gl_context : public osd_gl_context
{
public:
	qt_gl_context(QWindow *window) : m_window(window)
	{
		m_context = new QOpenGLContext;
		m_context->setFormat(window->format());
		if (!m_context->create())
		{
			m_error = "Failed to create QOpenGLContext";
			return;
		}
		// drawogl issues GL calls (loadgl_functions/initialize_gl) right after
		// create() expects a current context (SDL_GL_CreateContext makes it
		// current as a side effect), so make it current here too.
		if (!m_context->makeCurrent(m_window))
			m_error = "Failed to make QOpenGLContext current (window not exposed?)";
	}

	virtual ~qt_gl_context() override
	{
		if (m_context)
		{
			if (QOpenGLContext::currentContext() == m_context)
				m_context->doneCurrent();
			delete m_context;
		}
	}

	virtual explicit operator bool() const override
	{
		return m_context && m_context->isValid() && m_error.empty();
	}

	virtual void make_current() override
	{
		m_context->makeCurrent(m_window);
	}

	virtual bool set_swap_interval(const int swap) override
	{
		// Qt only honours the swap interval through the QSurfaceFormat at
		// context-creation time; the window is created with it already set, so
		// report success when the requested value matches what we created with.
		return m_context && (m_context->format().swapInterval() == swap);
	}

	virtual const char *last_error_message() override
	{
		return m_error.empty() ? nullptr : m_error.c_str();
	}

	virtual void *get_proc_address(const char *proc) override
	{
		return reinterpret_cast<void *>(m_context->getProcAddress(proc));
	}

	virtual void swap_buffer() override
	{
		m_context->swapBuffers(m_window);
	}

private:
	QOpenGLContext *m_context = nullptr;
	QWindow *const  m_window;
	std::string     m_error;
};

} // namespace osd::qtui

#endif // USE_OPENGL

#endif // MAME_OSD_QTUI_QTGLCONTEXT_H
