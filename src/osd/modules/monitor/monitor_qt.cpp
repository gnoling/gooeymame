// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
/*
 * monitor_qt.cpp - Qt-native monitor module for the qtui OSD
 *
 * Reads the desktop monitor geometry from the Qt-free QtMonitorRect store
 * (captured on the GUI thread from QScreen, see qtmonitors.h), so the Qt-native
 * OSD doesn't depend on SDL for monitor enumeration.  This translation unit is
 * itself Qt-free.
 */

#include "modules/osdmodule.h"
#include "monitor_module.h"

#if defined(OSD_QT_GL)

#include "monitor_common.h"

#include "modules/lib/osdobj_common.h"
#include "modules/osdwindow.h"

#include "osdcore.h"

#include "qtmonitors.h"

#include <algorithm>
#include <cstdio>
#include <memory>


//============================================================
//  qt_monitor_info
//============================================================

class qt_monitor_info : public osd_monitor_info
{
public:
	qt_monitor_info(monitor_module &module, std::uint64_t handle, const char *device, float aspect, const osd::qtui::QtMonitorRect &rect) :
		osd_monitor_info(module, handle, std::string(device), aspect),
		m_rect(rect)
	{
		qt_monitor_info::refresh();
	}

private:
	void refresh() override
	{
		m_pos_size = osd_rect(m_rect.x, m_rect.y, m_rect.w, m_rect.h);
		m_usuable_pos_size = m_pos_size;
		m_is_primary = m_rect.primary;
	}

	osd::qtui::QtMonitorRect m_rect;
};


//============================================================
//  qt_monitor_module
//============================================================

class qt_monitor_module : public monitor_module_base
{
public:
	qt_monitor_module() :
		monitor_module_base(OSD_MONITOR_PROVIDER, "qt")
	{
	}

	std::shared_ptr<osd_monitor_info> monitor_from_rect(const osd_rect &proposed) override
	{
		if (!m_initialized || list().empty())
			return nullptr;

		auto intersects_less = [&proposed] (std::shared_ptr<osd_monitor_info> a, std::shared_ptr<osd_monitor_info> b)
		{
			return intersection(a->usuable_position_size(), proposed) < intersection(b->usuable_position_size(), proposed);
		};
		return *std::max_element(std::begin(list()), std::end(list()), intersects_less);
	}

	std::shared_ptr<osd_monitor_info> monitor_from_window(const osd_window &window) override
	{
		if (!m_initialized || list().empty())
			return nullptr;

		// The embedded Qt window doesn't track which screen it lands on; the
		// primary monitor is a fine answer (only used for aspect/fullscreen).
		for (auto const &mon : list())
			if (mon->is_primary())
				return mon;
		return list().front();
	}

protected:
	int init_internal(const osd_options &options) override
	{
		std::vector<osd::qtui::QtMonitorRect> mons = osd::qtui::qtui_get_monitors();
		if (mons.empty())
			mons.push_back({ 0, 0, 1920, 1080, true });   // safe fallback

		std::uint64_t handle = 0;
		for (osd::qtui::QtMonitorRect const &rect : mons)
		{
			char temp[64];
			std::snprintf(temp, sizeof(temp) - 1, "%s%llu", OSDOPTION_SCREEN, static_cast<unsigned long long>(handle));

			float const aspect = rect.h ? (float(rect.w) / float(rect.h)) : 1.0f;
			auto monitor = std::make_shared<qt_monitor_info>(*this, handle, temp, aspect, rect);
			monitor->set_aspect(aspect);
			add_monitor(monitor);
			handle++;
		}
		return 0;
	}

private:
	static int intersection(const osd_rect &a, const osd_rect &b)
	{
		int const l = std::max(a.left(), b.left());
		int const r = std::min(a.right(), b.right());
		int const t = std::max(a.top(), b.top());
		int const bo = std::min(a.bottom(), b.bottom());
		if ((l < r) && (t < bo))
			return (r - l) + (bo - t);
		return 0;
	}
};

#else
MODULE_NOT_SUPPORTED(qt_monitor_module, OSD_MONITOR_PROVIDER, "qt")
#endif

MODULE_DEFINITION(MONITOR_QT, qt_monitor_module)
