// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtbgfxchains.h - BGFX shader-chain query/select (Qt-free)
//
//  Lets the qtui OSD enumerate and select the BGFX shader effect ("chain") for
//  the running window without pulling BGFX headers into the OSD/Qt code.  The
//  implementation lives in drawbgfx.cpp, where renderer_bgfx / chain_manager are
//  fully defined.  No-ops / returns false when the active renderer isn't BGFX.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTBGFXCHAINS_H
#define MAME_OSD_QTUI_QTBGFXCHAINS_H

#pragma once

#include <string>
#include <vector>

class osd_window;

namespace osd::qtui {

// Fill `names` with the available effect names, `current` with the active index
// for screen 0, and `screenCount` with the number of screens.  Returns true
// only when the window's renderer is BGFX and has effects.
bool bgfx_chain_info(osd_window &window, std::vector<std::string> &names, int &current, int &screenCount);

// Select effect `index` for `screen` (applies + reloads the chain).
void bgfx_select_chain(osd_window &window, int screen, int index);

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTBGFXCHAINS_H
