// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  input_sdlgame.cpp - SDL game-controller joystick provider for the qtui OSD
//
//  The qtui OSD is otherwise SDL-free (video/keyboard/mouse/lightgun/monitor
//  are Qt-native).  MAME has no native Linux joystick module — upstream Linux
//  MAME (SDLMAME) uses SDL for joysticks — so on non-Windows the qtui OSD uses
//  SDL's game-controller support for gamepads.  (Windows uses the native
//  winhybrid provider, so this module is built only for OSD_QT_GL && !_WIN32.)
//
//  Unlike MAME's input_sdl game-controller module, this one is NOT coupled to
//  the SDL OSD: it doesn't use the sdl_event_manager / sdl_options, and is
//  templated on osd_common_t (like the Qt input modules).  It initialises ONLY
//  SDL's joystick/gamecontroller subsystem (never video), so it can't bring back
//  the SDL foreign-window focus/text/grab problems that motivated removing SDL.
//
//  It polls controller state each frame (SDL_GameControllerGetAxis/Button) and
//  handles hotplug + reconnect by GUID/serial (so a wireless pad that sleeps or
//  a controller that is briefly unplugged re-binds to the SAME logical device,
//  keeping its player/input assignments), all via SDL_PumpEvents + the device
//  list — no SDL event-queue subscription needed.
//
//============================================================

#if defined(OSD_QT_GL) && !defined(_WIN32)

#include "input_module.h"
#include "modules/osdmodule.h"

#include "input_common.h"

#include "interface/inputcode.h"
#include "interface/inputdev.h"
#include "modules/lib/osdobj_common.h"

#include "osdcore.h"   // osd_printf_*

#include <SDL2/SDL.h>

#include <cstring>
#include <string>
#include <vector>


namespace osd {

namespace {

//============================================================
//  control mappings (mirror MAME's input_sdl game-controller layout)
//============================================================

struct sdlgame_axis_map { SDL_GameControllerAxis sdl; input_item_id item; char const *name; bool trigger; };
const sdlgame_axis_map s_axes[] = {
	{ SDL_CONTROLLER_AXIS_LEFTX,        ITEM_ID_XAXIS,   "LSX", false },
	{ SDL_CONTROLLER_AXIS_LEFTY,        ITEM_ID_YAXIS,   "LSY", false },
	{ SDL_CONTROLLER_AXIS_RIGHTX,       ITEM_ID_ZAXIS,   "RSX", false },
	{ SDL_CONTROLLER_AXIS_RIGHTY,       ITEM_ID_RZAXIS,  "RSY", false },
	{ SDL_CONTROLLER_AXIS_TRIGGERLEFT,  ITEM_ID_SLIDER1, "LT",  true  },
	{ SDL_CONTROLLER_AXIS_TRIGGERRIGHT, ITEM_ID_SLIDER2, "RT",  true  },
};

// face/shoulder/stick/menu buttons → ITEM_ID_BUTTON1.. (in this order)
struct sdlgame_button_map { SDL_GameControllerButton sdl; char const *name; };
const sdlgame_button_map s_buttons[] = {
	{ SDL_CONTROLLER_BUTTON_A,             "A" },
	{ SDL_CONTROLLER_BUTTON_B,             "B" },
	{ SDL_CONTROLLER_BUTTON_X,             "X" },
	{ SDL_CONTROLLER_BUTTON_Y,             "Y" },
	{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  "LB" },
	{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "RB" },
	{ SDL_CONTROLLER_BUTTON_BACK,          "Back" },
	{ SDL_CONTROLLER_BUTTON_START,         "Start" },
	{ SDL_CONTROLLER_BUTTON_GUIDE,         "Guide" },
	{ SDL_CONTROLLER_BUTTON_LEFTSTICK,     "LSB" },
	{ SDL_CONTROLLER_BUTTON_RIGHTSTICK,    "RSB" },
};

// d-pad → hat items
struct sdlgame_hat_map { SDL_GameControllerButton sdl; input_item_id item; char const *name; };
const sdlgame_hat_map s_hats[] = {
	{ SDL_CONTROLLER_BUTTON_DPAD_UP,    ITEM_ID_HAT1UP,    "D-pad Up" },
	{ SDL_CONTROLLER_BUTTON_DPAD_DOWN,  ITEM_ID_HAT1DOWN,  "D-pad Down" },
	{ SDL_CONTROLLER_BUTTON_DPAD_LEFT,  ITEM_ID_HAT1LEFT,  "D-pad Left" },
	{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT, ITEM_ID_HAT1RIGHT, "D-pad Right" },
};


//============================================================
//  sdlgame_device
//============================================================

class sdlgame_device : public device_info
{
public:
	sdlgame_device(
			std::string &&name,
			std::string &&id,
			input_module &module,
			SDL_GameController *ctrl,
			SDL_JoystickID instance,
			std::string &&guid,
			std::string &&serial) :
		device_info(std::move(name), std::move(id), module),
		m_ctrl(ctrl),
		m_instance(instance),
		m_guid(std::move(guid)),
		m_serial(std::move(serial))
	{
		std::memset(&m_state, 0, sizeof(m_state));
	}

	virtual ~sdlgame_device()
	{
		if (m_ctrl)
			SDL_GameControllerClose(m_ctrl);
	}

	SDL_JoystickID instance() const { return m_instance; }
	std::string const &guid() const { return m_guid; }
	std::string const &serial() const { return m_serial; }
	bool connected() const { return m_ctrl != nullptr; }

	// reconnect: re-point this logical device at a freshly-opened SDL handle.
	void attach(SDL_GameController *ctrl, SDL_JoystickID instance)
	{
		if (m_ctrl)
			SDL_GameControllerClose(m_ctrl);
		m_ctrl = ctrl;
		m_instance = instance;
	}

	// disconnect: drop the SDL handle but keep the logical device (so its config
	// survives a temporary disconnect); inputs read as neutral until reattached.
	void detach()
	{
		if (m_ctrl)
		{
			SDL_GameControllerClose(m_ctrl);
			m_ctrl = nullptr;
		}
		m_instance = -1;
		std::memset(&m_state, 0, sizeof(m_state));
	}

	virtual void poll(bool relative_reset) override
	{
		if (!m_ctrl)
			return;

		for (auto const &a : s_axes)
		{
			Sint16 const v = SDL_GameControllerGetAxis(m_ctrl, a.sdl);
			s32 const n = normalize_absolute_axis(v, -32'767, 32'767);
			m_state.axes[a.sdl] = a.trigger ? -n : n;   // MAME wants negative for triggers
		}
		for (auto const &b : s_buttons)
			m_state.buttons[b.sdl] = SDL_GameControllerGetButton(m_ctrl, b.sdl) ? 0x80 : 0x00;
		for (auto const &h : s_hats)
			m_state.buttons[h.sdl] = SDL_GameControllerGetButton(m_ctrl, h.sdl) ? 0x80 : 0x00;
	}

	virtual void reset() override
	{
		std::memset(&m_state, 0, sizeof(m_state));
	}

	virtual void configure(input_device &device) override
	{
		for (auto const &a : s_axes)
			device.add_item(a.name, std::string_view(), a.item, generic_axis_get_state<s32>, &m_state.axes[a.sdl]);

		input_item_id button_item = ITEM_ID_BUTTON1;
		for (auto const &b : s_buttons)
			device.add_item(b.name, std::string_view(), button_item++, generic_button_get_state<s32>, &m_state.buttons[b.sdl]);

		for (auto const &h : s_hats)
			device.add_item(h.name, std::string_view(), h.item, generic_button_get_state<s32>, &m_state.buttons[h.sdl]);
	}

private:
	struct controller_state
	{
		s32 axes[SDL_CONTROLLER_AXIS_MAX];
		s32 buttons[SDL_CONTROLLER_BUTTON_MAX];
	};

	controller_state    m_state;
	SDL_GameController  *m_ctrl;
	SDL_JoystickID       m_instance;
	std::string          m_guid;
	std::string          m_serial;
};


//============================================================
//  sdlgame_module
//============================================================

class sdlgame_module : public input_module_impl<sdlgame_device, osd_common_t>
{
public:
	sdlgame_module() :
		input_module_impl<sdlgame_device, osd_common_t>(OSD_JOYSTICKINPUT_PROVIDER, "sdlgame")
	{
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_impl<sdlgame_device, osd_common_t>::input_init(machine);

		// We provide our own main() (qtmain.cpp) and don't link SDLmain, so tell
		// SDL not to expect its startup shim before we touch any SDL subsystem.
		SDL_SetMainReady();

		if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
		{
			osd_printf_warning("sdlgame: could not initialise SDL game controller subsystem: %s\n", SDL_GetError());
			return;
		}
		m_initialized = true;

		// open every already-connected game controller
		for (int i = 0; i < SDL_NumJoysticks(); i++)
			if (SDL_IsGameController(i))
				open_index(i);
	}

	virtual void exit() override
	{
		m_devices.clear();   // non-owning; the framework frees the device objects
		input_module_impl<sdlgame_device, osd_common_t>::exit();
		if (m_initialized)
		{
			SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
			m_initialized = false;
		}
	}

	// Run once per frame (before per-device polling), even when unfocused, so
	// hotplug/reconnect is handled while the game is running.
	virtual void before_poll() override
	{
		input_module_impl<sdlgame_device, osd_common_t>::before_poll();
		if (!m_initialized)
			return;

		// refresh SDL's device list + controller state
		SDL_PumpEvents();

		// drop any controllers that have gone away (keep the logical device)
		for (auto *const dev : m_devices)
			if (dev->connected() && !SDL_GameControllerGetAttached(SDL_GameControllerFromInstanceID(dev->instance())))
			{
				osd_printf_verbose("sdlgame: controller disconnected [%s]\n", dev->name());
				dev->detach();
			}

		// pick up newly-connected (or reconnected) controllers
		for (int i = 0; i < SDL_NumJoysticks(); i++)
		{
			if (!SDL_IsGameController(i))
				continue;
			SDL_JoystickID const instance = SDL_JoystickGetDeviceInstanceID(i);
			if (already_open(instance))
				continue;
			open_index(i);
		}
	}

private:
	bool already_open(SDL_JoystickID instance) const
	{
		for (auto const *const dev : m_devices)
			if (dev->connected() && dev->instance() == instance)
				return true;
		return false;
	}

	static std::string guid_string(SDL_JoystickGUID const &guid)
	{
		char buf[64] = { 0 };
		SDL_JoystickGetGUIDString(guid, buf, sizeof(buf));
		return std::string(buf);
	}

	// open the controller at joystick index `i`; reconnect to a matching
	// disconnected logical device when possible, else create a new one.
	void open_index(int i)
	{
		SDL_GameController *const ctrl = SDL_GameControllerOpen(i);
		if (!ctrl)
		{
			osd_printf_warning("sdlgame: could not open game controller %d: %s\n", i, SDL_GetError());
			return;
		}

		SDL_JoystickID const instance = SDL_JoystickGetDeviceInstanceID(i);
		std::string guid = guid_string(SDL_JoystickGetDeviceGUID(i));
		char const *const serialp = SDL_GameControllerGetSerial(ctrl);
		std::string serial = serialp ? serialp : "";

		// try to re-attach to a disconnected device with the same identity, so a
		// slept/unplugged controller keeps its player + input assignments
		if (sdlgame_device *const match = find_reconnect(guid, serial))
		{
			osd_printf_verbose("sdlgame: controller reconnected [%s]\n", match->name());
			match->attach(ctrl, instance);
			return;
		}

		char const *const namep = SDL_GameControllerName(ctrl);
		std::string name = (namep && *namep) ? namep : "Game Controller";
		std::string id = guid.empty() ? name : (name + " " + guid);

		sdlgame_device &dev = create_device<sdlgame_device>(
				DEVICE_CLASS_JOYSTICK,
				std::move(name),
				std::move(id),
				ctrl,
				instance,
				std::move(guid),
				std::move(serial));
		m_devices.push_back(&dev);
		osd_printf_verbose("sdlgame: controller added [%s]\n", dev.name());
	}

	sdlgame_device *find_reconnect(std::string const &guid, std::string const &serial)
	{
		sdlgame_device *guid_only = nullptr;
		for (auto *const dev : m_devices)
		{
			if (dev->connected() || dev->guid() != guid)
				continue;
			// exact match on serial when both have one
			if (!serial.empty() && !dev->serial().empty())
			{
				if (dev->serial() == serial)
					return dev;
				continue;
			}
			if (!guid_only)
				guid_only = dev;   // fall back to first GUID match
		}
		return guid_only;
	}

	bool m_initialized = false;
	std::vector<sdlgame_device *> m_devices;   // non-owning; framework owns the objects
};

} // anonymous namespace

} // namespace osd


MODULE_DEFINITION(JOYSTICKINPUT_QTSDLGAME, osd::sdlgame_module)

#else // defined(OSD_QT_GL) && !defined(_WIN32)

#include "input_module.h"
#include "modules/osdmodule.h"

namespace osd { namespace { MODULE_NOT_SUPPORTED(sdlgame_module, OSD_JOYSTICKINPUT_PROVIDER, "sdlgame") } }

MODULE_DEFINITION(JOYSTICKINPUT_QTSDLGAME, osd::sdlgame_module)

#endif // defined(OSD_QT_GL) && !defined(_WIN32)
