// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  qtembedtarget.h - Qt-native OSD surface handoff (Qt-free)
//
//  Carries the QWindow the emulation worker thread renders into, from the
//  Qt GUI thread (which creates/owns it) to the OSD running on the worker
//  thread.  Deliberately Qt-free (QWindow is only forward-declared) so it
//  can be included by emulator.cpp, which mixes MAME + SDL headers but must
//  stay clear of Qt headers.  The real Qt includes live in qtwindow.cpp /
//  qtglcontext.h.
//
//============================================================
#ifndef MAME_OSD_QTUI_QTEMBEDTARGET_H
#define MAME_OSD_QTUI_QTEMBEDTARGET_H

#pragma once

#include <atomic>

class QWindow;

namespace osd::qtui {

// Shared surface descriptor.  The GUI thread creates the QWindow, shows it,
// fills in `window` plus the initial size, and passes the address of this
// struct into qtui_run_embedded_native().  The OSD's qt_window_info reads
// `window` (only via the sanctioned cross-thread QOpenGLContext path) and
// reads the size atomics every frame so a GUI-thread resize is picked up
// without touching the QWindow off-thread.  The struct must outlive the
// worker thread (owned by MainWindow, freed after the worker is joined).
struct QtEmbedTarget
{
	QWindow            *window = nullptr;
	std::atomic<int>    width{ 0 };
	std::atomic<int>    height{ 0 };
};

} // namespace osd::qtui

#endif // MAME_OSD_QTUI_QTEMBEDTARGET_H
