// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtwindow.cpp - Qt-native OSD render window for the qtui OSD
//
//  Runs entirely on the emulation worker thread (window_init/update/draw and
//  the GL context all execute there).  The QWindow it renders into is created
//  and owned by the Qt GUI thread and reaches us through QtEmbedTarget; we
//  only read its size atomics and hand its pointer to the GL context's
//  makeCurrent(), which is the one cross-thread touch Qt's threaded-OpenGL
//  model sanctions.
//
//============================================================

// MAME headers first so their macros are established before any Qt header
// (mirrors the Qt debugger module's include discipline).
#include "emu.h"

#include "modules/osdwindow.h"
#include "render.h"
#include "screen.h"

// qtui (Qt-free) headers
#include "qtnativewindow.h"
#include "qtembedtarget.h"
#include "qtwindow.h"

// Qt-aware headers last
#include "qtglcontext.h"

#include <QtGui/QWindow>


namespace osd::qtui {

qt_window_info::qt_window_info(
		running_machine &machine,
		render_module &renderprovider,
		int index,
		const std::shared_ptr<osd_monitor_info> &monitor,
		const osd_window_config &config,
		QtEmbedTarget &target) :
	osd_window_t<QWindow *>(machine, renderprovider, index, monitor, config),
	m_target(target)
{
}

qt_window_info::~qt_window_info()
{
}

int qt_window_info::window_init()
{
	// allocate the render target (and choose the starting view)
	create_target();

	if (complete_create())
	{
		osd_printf_error("qtui: failed to create Qt-native render window\n");
		return 1;
	}

	return 0;
}

int qt_window_info::complete_create()
{
	// adopt the GUI-thread QWindow as our platform surface
	set_platform_window(m_target.window);

	// build the renderer and initialise its GPU backend (drawogl obtains a Qt
	// GL context via make_gl_context() below, on this worker thread)
	renderer_create();
	if (renderer().create())
		return 1;

	return 0;
}

osd_dim qt_window_info::get_size()
{
	// read the size the GUI thread published; never touch the QWindow here
	int const w = m_target.width.load(std::memory_order_relaxed);
	int const h = m_target.height.load(std::memory_order_relaxed);
	return osd_dim((w > 0) ? w : 640, (h > 0) ? h : 480);
}

osd_gl_context *qt_window_info::make_gl_context()
{
#if USE_OPENGL
	return new qt_gl_context(platform_window());
#else
	return nullptr;
#endif
}

void qt_window_info::update()
{
	if (target() == nullptr)
		return;

	// keep the target aware of the game's minimum size (we don't auto-resize
	// the host window; the user/embedder controls that)
	int tempwidth, tempheight;
	target()->compute_minimum_size(tempwidth, tempheight);

	render_primitive_list &primlist = *renderer().get_primitives();

	// flag vector screens for the renderer
	const screen_device *const screen = screen_device_enumerator(machine().root_device()).byindex(index());
	if ((screen != nullptr) && (screen->screen_type() == SCREEN_TYPE_VECTOR))
		renderer().set_flags(osd_renderer::FLAG_HAS_VECTOR_SCREEN);
	else
		renderer().clear_flags(osd_renderer::FLAG_HAS_VECTOR_SCREEN);

	m_primlist = &primlist;

	renderer().draw(1);
}

void qt_window_info::complete_destroy()
{
	// drop the renderer (and its Qt GL context) on this worker thread; the
	// QWindow itself belongs to the GUI thread and is freed there once the
	// worker has been joined
	renderer_reset();
}


std::unique_ptr<osd_window> make_native_window(
		running_machine &machine,
		render_module &renderprovider,
		int index,
		const std::shared_ptr<osd_monitor_info> &monitor,
		const osd_window_config &config,
		QtEmbedTarget &target)
{
	auto win = std::make_unique<qt_window_info>(machine, renderprovider, index, monitor, config, target);
	if (win->window_init())
		return nullptr;
	return win;
}

} // namespace osd::qtui
