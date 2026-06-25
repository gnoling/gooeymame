// license:BSD-3-Clause
// copyright-holders:MAMEdev Team
//============================================================
//
//  input_qt.cpp - Qt-native OSD input providers for the qtui OSD
//
//  Phase 13b: keyboard/mouse/lightgun providers that consume input from the
//  Qt-native render window via the (Qt-free) QtInputBus, instead of SDL.  This
//  finally removes MAME's dependence on SDL's foreign-window event delivery,
//  which never reported focus/text for a SDL_CreateWindowFrom() window.
//
//  This translation unit is itself Qt-free: the GUI thread translates QKeyEvent
//  etc. into plain-int QtInputEvent records on the bus; here we map them to MAME
//  input items.  Only compiled into the qtui OSD (OSD_QT_GL).
//
//============================================================

#include "input_module.h"
#include "modules/osdmodule.h"

#include "input_common.h"

#include "interface/inputcode.h"
#include "interface/inputdev.h"
#include "modules/lib/osdobj_common.h"

#include "qtinput.h"
#include "qtnativewindow.h"   // osd::qtui::map_lightgun()

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <utility>


namespace osd {

namespace {

//============================================================
//  Qt::Key constants
//
//  Hard-coded so this MAME translation unit needs no Qt headers.  Values match
//  Qt's stable Qt::Key enum.
//============================================================

enum : int
{
	QtKey_Escape    = 0x01000000,
	QtKey_Tab       = 0x01000001,
	QtKey_Backspace = 0x01000003,
	QtKey_Return    = 0x01000004,
	QtKey_Enter     = 0x01000005,   // keypad enter
	QtKey_Insert    = 0x01000006,
	QtKey_Delete    = 0x01000007,
	QtKey_Pause     = 0x01000008,
	QtKey_Print     = 0x01000009,
	QtKey_Home      = 0x01000010,
	QtKey_End       = 0x01000011,
	QtKey_Left      = 0x01000012,
	QtKey_Up        = 0x01000013,
	QtKey_Right     = 0x01000014,
	QtKey_Down      = 0x01000015,
	QtKey_PageUp    = 0x01000016,
	QtKey_PageDown  = 0x01000017,
	QtKey_Shift     = 0x01000020,
	QtKey_Control   = 0x01000021,
	QtKey_Meta      = 0x01000022,
	QtKey_Alt       = 0x01000023,
	QtKey_CapsLock  = 0x01000024,
	QtKey_NumLock   = 0x01000025,
	QtKey_ScrollLock= 0x01000026,
	QtKey_F1        = 0x01000030,   // F1..F20 are contiguous
	QtKey_Space     = 0x20,
	QtKey_Apostrophe= 0x27,
	QtKey_Comma     = 0x2c,
	QtKey_Minus     = 0x2d,
	QtKey_Period    = 0x2e,
	QtKey_Slash     = 0x2f,
	QtKey_0         = 0x30,
	QtKey_Semicolon = 0x3b,
	QtKey_Equal     = 0x3d,
	QtKey_A         = 0x41,
	QtKey_BracketLeft  = 0x5b,
	QtKey_Backslash    = 0x5c,
	QtKey_BracketRight = 0x5d,
	QtKey_QuoteLeft    = 0x60,   // grave/tilde
};


struct qt_key_map_entry
{
	int            qtkey;
	input_item_id  mameid;
	char const    *name;
};

// The standard PC keyboard.  Left/right modifier and keypad distinctions are
// approximated for now (Qt reports a single Shift/Control/Alt key code and
// flags the keypad via a modifier); positional refinement is a follow-up.
const qt_key_map_entry s_qt_keymap[] =
{
	{ QtKey_A + 0,  ITEM_ID_A, "A" }, { QtKey_A + 1,  ITEM_ID_B, "B" }, { QtKey_A + 2,  ITEM_ID_C, "C" },
	{ QtKey_A + 3,  ITEM_ID_D, "D" }, { QtKey_A + 4,  ITEM_ID_E, "E" }, { QtKey_A + 5,  ITEM_ID_F, "F" },
	{ QtKey_A + 6,  ITEM_ID_G, "G" }, { QtKey_A + 7,  ITEM_ID_H, "H" }, { QtKey_A + 8,  ITEM_ID_I, "I" },
	{ QtKey_A + 9,  ITEM_ID_J, "J" }, { QtKey_A + 10, ITEM_ID_K, "K" }, { QtKey_A + 11, ITEM_ID_L, "L" },
	{ QtKey_A + 12, ITEM_ID_M, "M" }, { QtKey_A + 13, ITEM_ID_N, "N" }, { QtKey_A + 14, ITEM_ID_O, "O" },
	{ QtKey_A + 15, ITEM_ID_P, "P" }, { QtKey_A + 16, ITEM_ID_Q, "Q" }, { QtKey_A + 17, ITEM_ID_R, "R" },
	{ QtKey_A + 18, ITEM_ID_S, "S" }, { QtKey_A + 19, ITEM_ID_T, "T" }, { QtKey_A + 20, ITEM_ID_U, "U" },
	{ QtKey_A + 21, ITEM_ID_V, "V" }, { QtKey_A + 22, ITEM_ID_W, "W" }, { QtKey_A + 23, ITEM_ID_X, "X" },
	{ QtKey_A + 24, ITEM_ID_Y, "Y" }, { QtKey_A + 25, ITEM_ID_Z, "Z" },

	{ QtKey_0 + 0,  ITEM_ID_0, "0" }, { QtKey_0 + 1,  ITEM_ID_1, "1" }, { QtKey_0 + 2,  ITEM_ID_2, "2" },
	{ QtKey_0 + 3,  ITEM_ID_3, "3" }, { QtKey_0 + 4,  ITEM_ID_4, "4" }, { QtKey_0 + 5,  ITEM_ID_5, "5" },
	{ QtKey_0 + 6,  ITEM_ID_6, "6" }, { QtKey_0 + 7,  ITEM_ID_7, "7" }, { QtKey_0 + 8,  ITEM_ID_8, "8" },
	{ QtKey_0 + 9,  ITEM_ID_9, "9" },

	{ QtKey_F1 + 0,  ITEM_ID_F1,  "F1" },  { QtKey_F1 + 1,  ITEM_ID_F2,  "F2" },
	{ QtKey_F1 + 2,  ITEM_ID_F3,  "F3" },  { QtKey_F1 + 3,  ITEM_ID_F4,  "F4" },
	{ QtKey_F1 + 4,  ITEM_ID_F5,  "F5" },  { QtKey_F1 + 5,  ITEM_ID_F6,  "F6" },
	{ QtKey_F1 + 6,  ITEM_ID_F7,  "F7" },  { QtKey_F1 + 7,  ITEM_ID_F8,  "F8" },
	{ QtKey_F1 + 8,  ITEM_ID_F9,  "F9" },  { QtKey_F1 + 9,  ITEM_ID_F10, "F10" },
	{ QtKey_F1 + 10, ITEM_ID_F11, "F11" }, { QtKey_F1 + 11, ITEM_ID_F12, "F12" },

	{ QtKey_Escape,     ITEM_ID_ESC,       "Esc" },
	{ QtKey_Tab,        ITEM_ID_TAB,       "Tab" },
	{ QtKey_Backspace,  ITEM_ID_BACKSPACE, "Backspace" },
	{ QtKey_Return,     ITEM_ID_ENTER,     "Enter" },
	{ QtKey_Enter,      ITEM_ID_ENTER_PAD, "Enter Pad" },
	{ QtKey_Space,      ITEM_ID_SPACE,     "Space" },
	{ QtKey_Insert,     ITEM_ID_INSERT,    "Insert" },
	{ QtKey_Delete,     ITEM_ID_DEL,       "Delete" },
	{ QtKey_Home,       ITEM_ID_HOME,      "Home" },
	{ QtKey_End,        ITEM_ID_END,       "End" },
	{ QtKey_PageUp,     ITEM_ID_PGUP,      "Page Up" },
	{ QtKey_PageDown,   ITEM_ID_PGDN,      "Page Down" },
	{ QtKey_Left,       ITEM_ID_LEFT,      "Left" },
	{ QtKey_Right,      ITEM_ID_RIGHT,     "Right" },
	{ QtKey_Up,         ITEM_ID_UP,        "Up" },
	{ QtKey_Down,       ITEM_ID_DOWN,      "Down" },
	{ QtKey_Shift,      ITEM_ID_LSHIFT,    "Shift" },
	{ QtKey_Control,    ITEM_ID_LCONTROL,  "Ctrl" },
	{ QtKey_Alt,        ITEM_ID_LALT,      "Alt" },
	{ QtKey_Meta,       ITEM_ID_LWIN,      "Meta" },
	{ QtKey_CapsLock,   ITEM_ID_CAPSLOCK,  "Caps Lock" },
	{ QtKey_NumLock,    ITEM_ID_NUMLOCK,   "Num Lock" },
	{ QtKey_ScrollLock, ITEM_ID_SCRLOCK,   "Scroll Lock" },
	{ QtKey_Pause,      ITEM_ID_PAUSE,     "Pause" },
	{ QtKey_Print,      ITEM_ID_PRTSCR,    "Print Screen" },

	{ QtKey_Minus,        ITEM_ID_MINUS,      "-" },
	{ QtKey_Equal,        ITEM_ID_EQUALS,     "=" },
	{ QtKey_BracketLeft,  ITEM_ID_OPENBRACE,  "[" },
	{ QtKey_BracketRight, ITEM_ID_CLOSEBRACE, "]" },
	{ QtKey_Backslash,    ITEM_ID_BACKSLASH,  "\\" },
	{ QtKey_Semicolon,    ITEM_ID_COLON,      ";" },
	{ QtKey_Apostrophe,   ITEM_ID_QUOTE,      "'" },
	{ QtKey_QuoteLeft,    ITEM_ID_TILDE,      "`" },
	{ QtKey_Comma,        ITEM_ID_COMMA,      "," },
	{ QtKey_Period,       ITEM_ID_STOP,       "." },
	{ QtKey_Slash,        ITEM_ID_SLASH,      "/" },

	{ 0, ITEM_ID_INVALID, nullptr }
};


//============================================================
//  qt_keyboard_device
//============================================================

class qt_keyboard_device : public device_info
{
public:
	qt_keyboard_device(std::string &&name, std::string &&id, input_module &module) :
		device_info(std::move(name), std::move(id), module)
	{
		std::fill(std::begin(m_state), std::end(m_state), 0);
	}

	virtual void poll(bool relative_reset) override
	{
		for (auto const &e : osd::qtui::QtInputBus::instance().takeKeyboard())
		{
			switch (e.type)
			{
			case osd::qtui::QtInputType::KeyPress:
			case osd::qtui::QtInputType::KeyRelease:
			{
				auto const it = m_index.find(e.key);
				if (it != m_index.end())
					m_state[it->second] = (e.type == osd::qtui::QtInputType::KeyPress) ? 0x80 : 0x00;
				break;
			}
			case osd::qtui::QtInputType::Char:
				// natural keyboard / UI text entry
				if (e.codepoint)
					osd::qtui::ui_push_char(char32_t(e.codepoint));
				break;
			case osd::qtui::QtInputType::FocusGained:
				osd::qtui::ui_push_focus(true);
				break;
			case osd::qtui::QtInputType::FocusLost:
				osd::qtui::ui_push_focus(false);
				break;
			default:
				break;
			}
		}
	}

	virtual void reset() override
	{
		std::fill(std::begin(m_state), std::end(m_state), 0);
	}

	virtual void configure(input_device &device) override
	{
		for (int i = 0; s_qt_keymap[i].mameid != ITEM_ID_INVALID; i++)
		{
			device.add_item(
					s_qt_keymap[i].name,
					std::string_view(),
					s_qt_keymap[i].mameid,
					generic_button_get_state<s32>,
					&m_state[i]);
			m_index[s_qt_keymap[i].qtkey] = i;
		}
	}

private:
	static inline constexpr int MAX_QT_KEYS = 128;   // > number of keymap entries

	s32                          m_state[MAX_QT_KEYS];
	std::unordered_map<int, int> m_index;   // Qt::Key value -> state index
};


//============================================================
//  qt_keyboard_module
//============================================================

class qt_keyboard_module : public input_module_impl<qt_keyboard_device, osd_common_t>
{
public:
	qt_keyboard_module() :
		input_module_impl<qt_keyboard_device, osd_common_t>(OSD_KEYBOARDINPUT_PROVIDER, "qt")
	{
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_impl<qt_keyboard_device, osd_common_t>::input_init(machine);

		create_device<qt_keyboard_device>(
				DEVICE_CLASS_KEYBOARD,
				"Qt keyboard",
				"Qt keyboard");
	}
};

//============================================================
//  qt_mouse_device (relative)
//============================================================

class qt_mouse_device : public device_info
{
public:
	qt_mouse_device(std::string &&name, std::string &&id, input_module &module) :
		device_info(std::move(name), std::move(id), module)
	{
		std::memset(&m_mouse, 0, sizeof(m_mouse));
	}

	virtual void poll(bool relative_reset) override
	{
		for (auto const &e : osd::qtui::QtInputBus::instance().takeMouse())
		{
			switch (e.type)
			{
			case osd::qtui::QtInputType::MouseMove:
				m_x += e.dx * input_device::RELATIVE_PER_PIXEL;
				m_y += e.dy * input_device::RELATIVE_PER_PIXEL;
				break;
			case osd::qtui::QtInputType::MouseButton:
				if (e.button >= 0 && e.button < MAX_BUTTONS)
					m_mouse.buttons[e.button] = e.value ? 0x80 : 0x00;
				break;
			case osd::qtui::QtInputType::MouseWheel:
				m_v += e.value * input_device::RELATIVE_PER_PIXEL;
				break;
			default:
				break;
			}
		}

		if (relative_reset)
		{
			m_mouse.lX = std::exchange(m_x, 0);
			m_mouse.lY = std::exchange(m_y, 0);
			m_mouse.lV = std::exchange(m_v, 0);
		}
	}

	virtual void reset() override
	{
		std::memset(&m_mouse, 0, sizeof(m_mouse));
		m_x = m_y = m_v = 0;
	}

	virtual void configure(input_device &device) override
	{
		device.add_item("X", std::string_view(), ITEM_ID_XAXIS, generic_axis_get_state<s32>, &m_mouse.lX);
		device.add_item("Y", std::string_view(), ITEM_ID_YAXIS, generic_axis_get_state<s32>, &m_mouse.lY);
		device.add_item("Scroll V", std::string_view(), ITEM_ID_ZAXIS, generic_axis_get_state<s32>, &m_mouse.lV);
		for (int b = 0; b < 3; b++)
			device.add_item(
					default_button_name(b),
					std::string_view(),
					input_item_id(ITEM_ID_BUTTON1 + b),
					generic_button_get_state<s32>,
					&m_mouse.buttons[b]);
	}

private:
	struct mouse_state { s32 lX, lY, lV; s32 buttons[MAX_BUTTONS]; };
	mouse_state m_mouse;
	s32 m_x = 0, m_y = 0, m_v = 0;
};


//============================================================
//  qt_lightgun_device (absolute)
//============================================================

class qt_lightgun_device : public device_info
{
public:
	qt_lightgun_device(std::string &&name, std::string &&id, input_module &module) :
		device_info(std::move(name), std::move(id), module)
	{
		std::memset(&m_mouse, 0, sizeof(m_mouse));
	}

	virtual void poll(bool relative_reset) override
	{
		for (auto const &e : osd::qtui::QtInputBus::instance().takeLightgun())
		{
			switch (e.type)
			{
			case osd::qtui::QtInputType::MouseMove:
				m_x = e.x;
				m_y = e.y;
				if (e.surfaceW > 1) m_w = e.surfaceW;
				if (e.surfaceH > 1) m_h = e.surfaceH;
				break;
			case osd::qtui::QtInputType::MouseButton:
				if (e.button >= 0 && e.button < MAX_BUTTONS)
					m_mouse.buttons[e.button] = e.value ? 0x80 : 0x00;
				if (e.surfaceW > 1) m_w = e.surfaceW;
				if (e.surfaceH > 1) m_h = e.surfaceH;
				break;
			default:
				break;
			}
		}

		// Map to the screen container (accounts for letterbox + artwork/bezel),
		// not the whole window.  Hold the last on-screen position when the
		// cursor wanders into the surrounding artwork/border.
		float nx, ny;
		if (osd::qtui::map_lightgun(m_x, m_y, m_w, m_h, nx, ny))
		{
			m_mouse.lX = normalize_absolute_axis(nx, 0.0, 1.0);
			m_mouse.lY = normalize_absolute_axis(ny, 0.0, 1.0);
		}
	}

	virtual void reset() override
	{
		std::memset(&m_mouse, 0, sizeof(m_mouse));
		m_x = m_y = 0;
	}

	virtual void configure(input_device &device) override
	{
		device.add_item("X", std::string_view(), ITEM_ID_XAXIS, generic_axis_get_state<s32>, &m_mouse.lX);
		device.add_item("Y", std::string_view(), ITEM_ID_YAXIS, generic_axis_get_state<s32>, &m_mouse.lY);
		for (int b = 0; b < 3; b++)
			device.add_item(
					default_button_name(b),
					std::string_view(),
					input_item_id(ITEM_ID_BUTTON1 + b),
					generic_button_get_state<s32>,
					&m_mouse.buttons[b]);
	}

private:
	struct mouse_state { s32 lX, lY; s32 buttons[MAX_BUTTONS]; };
	mouse_state m_mouse;
	s32 m_x = 0, m_y = 0;
	int m_w = 0, m_h = 0;
};


//============================================================
//  modules
//============================================================

class qt_mouse_module : public input_module_impl<qt_mouse_device, osd_common_t>
{
public:
	qt_mouse_module() :
		input_module_impl<qt_mouse_device, osd_common_t>(OSD_MOUSEINPUT_PROVIDER, "qt")
	{
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_impl<qt_mouse_device, osd_common_t>::input_init(machine);
		create_device<qt_mouse_device>(DEVICE_CLASS_MOUSE, "Qt mouse", "Qt mouse");
	}
};


class qt_lightgun_module : public input_module_impl<qt_lightgun_device, osd_common_t>
{
public:
	qt_lightgun_module() :
		input_module_impl<qt_lightgun_device, osd_common_t>(OSD_LIGHTGUNINPUT_PROVIDER, "qt")
	{
	}

	virtual void input_init(running_machine &machine) override
	{
		input_module_impl<qt_lightgun_device, osd_common_t>::input_init(machine);
		create_device<qt_lightgun_device>(DEVICE_CLASS_LIGHTGUN, "Qt lightgun", "Qt lightgun");
	}
};

} // anonymous namespace

} // namespace osd


MODULE_DEFINITION(KEYBOARDINPUT_QT, osd::qt_keyboard_module)
MODULE_DEFINITION(MOUSEINPUT_QT, osd::qt_mouse_module)
MODULE_DEFINITION(LIGHTGUNINPUT_QT, osd::qt_lightgun_module)
