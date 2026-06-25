// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtglprovider.h - Qt GL context provider interface (Qt-free)
//
//  A tiny polymorphic hook so the shared OpenGL renderer (drawogl.cpp) can
//  obtain a Qt-backed osd_gl_context without including any Qt headers.  The
//  Qt-native window (qt_window_info) inherits this interface and returns a
//  qt_gl_context; drawogl dynamic_casts the osd_window to this interface and,
//  when it matches, calls make_gl_context() instead of constructing the SDL
//  context.  This keeps Qt entirely out of drawogl.cpp while letting the
//  qtui build host both SDL and Qt-native windows in the same process.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTGLPROVIDER_H
#define MAME_OSD_QTUI_QTGLPROVIDER_H

#pragma once

class osd_gl_context;

namespace osd::qtui {

class qt_gl_context_provider
{
public:
	virtual ~qt_gl_context_provider() = default;

	// Construct (on the calling thread) an osd_gl_context bound to this
	// window's QWindow surface.  Called from renderer_ogl::create(), which
	// runs on the emulation worker thread, so the QOpenGLContext is created
	// with worker-thread affinity.  Ownership transfers to the caller.
	virtual osd_gl_context *make_gl_context() = 0;
};


// Lets the BGFX renderer obtain the QWindow's native platform handles without
// any Qt dependency of its own (mirrors the GL provider).  `ndt` is the native
// display/connection (X11 Display* / Wayland wl_display, else null), `nwh` is
// the native window handle (X11 Window / HWND / wl_surface).  Returns false if
// the handles can't be resolved.
class qt_native_handle_provider
{
public:
	virtual ~qt_native_handle_provider() = default;

	virtual bool native_handles(void *&ndt, void *&nwh, bool &wayland) const = 0;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTGLPROVIDER_H
