// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtnativewindow.h - Qt-native OSD window factory (Qt-free)
//
//  Factory entry point so the OSD interface (qt_osd_interface, defined in the
//  Qt-free emulator.cpp) can create a Qt-native render window without pulling
//  Qt headers in.  The implementation lives in qtwindow.cpp, which is the
//  Qt-aware translation unit; this header only references MAME OSD types.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTNATIVEWINDOW_H
#define MAME_OSD_QTUI_QTNATIVEWINDOW_H

#pragma once

#include <memory>

class osd_window;
class osd_monitor_info;
class render_module;
class running_machine;
struct osd_window_config;

namespace osd::qtui {

struct QtEmbedTarget;

// Create and initialise a Qt-native render window for `target` (which carries
// the GUI-thread QWindow and its size).  Allocates the render target and the
// renderer (the OpenGL renderer obtains a Qt GL context via the provider hook).
// Returns the window, or nullptr on failure.  Called on the emulation worker
// thread from qt_osd_interface::video_init().
std::unique_ptr<osd_window> make_native_window(
		running_machine &machine,
		render_module &renderprovider,
		int index,
		const std::shared_ptr<osd_monitor_info> &monitor,
		const osd_window_config &config,
		QtEmbedTarget &target);

// Map a pointer position (surface-local pixels, with the surface's logical size)
// to the running machine's first screen container, accounting for letterbox AND
// artwork/bezel layout.  Returns true and fills nx/ny in [0,1] within the screen
// when the point is over it.  Used by the Qt-native lightgun for accurate aim.
bool map_lightgun(int x, int y, int surfaceW, int surfaceH, float &nx, float &ny);

// Natural-keyboard / UI text: push a committed character (and window focus
// changes) into the running machine's ui_input, targeting the render window.
// Called from the Qt keyboard module's poll on the emulation thread.
void ui_push_char(char32_t ch);
void ui_push_focus(bool gained);

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTNATIVEWINDOW_H
