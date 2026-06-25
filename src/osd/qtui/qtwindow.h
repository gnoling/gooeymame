// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtwindow.h - Qt-native OSD render window for the qtui OSD
//
//  An osd_window backed by a QWindow (created/owned on the Qt GUI thread)
//  rather than an SDL_Window.  Models sdl_window_info, minus the SDL event /
//  cursor / fullscreen machinery that the Qt front-end handles itself.  It
//  also implements qt_gl_context_provider so the shared OpenGL renderer can
//  obtain a Qt GL context without any Qt dependency of its own.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTWINDOW_H
#define MAME_OSD_QTUI_QTWINDOW_H

#pragma once

#include "modules/osdwindow.h"

#include "qtglprovider.h"

#include <memory>

class QWindow;

namespace osd::qtui {

struct QtEmbedTarget;

class qt_window_info : public osd_window_t<QWindow *>, public qt_gl_context_provider, public qt_native_handle_provider
{
public:
	qt_window_info(
			running_machine &machine,
			render_module &renderprovider,
			int index,
			const std::shared_ptr<osd_monitor_info> &monitor,
			const osd_window_config &config,
			QtEmbedTarget &target);

	~qt_window_info();

	// returns 0 on success, else 1
	int window_init();

	// osd_window
	void update() override;
	void complete_destroy() override;
	osd_dim get_size() override;

	// the Qt front-end owns pointer state on the host widget; no-ops here
	void capture_pointer() override { }
	void release_pointer() override { }
	void show_pointer() override { }
	void hide_pointer() override { }

	// qt_gl_context_provider
	osd_gl_context *make_gl_context() override;

	// qt_native_handle_provider (for the BGFX renderer)
	bool native_handles(void *&ndt, void *&nwh, bool &wayland) const override;

private:
	int complete_create();

	QtEmbedTarget &m_target;
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTWINDOW_H
