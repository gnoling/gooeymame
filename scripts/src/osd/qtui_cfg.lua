-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   qtui_cfg.lua
--
--   Per-object compile configuration for the qtui (Qt front-end) OSD.
--
--   The qtui OSD renders through a Qt-native backend (qt_osd_interface :
--   osd_common_t) and links NO SDL.  This file deliberately does NOT
--   dofile('sdl_cfg.lua') — instead it replicates sdl_cfg.lua's *non-SDL*
--   compile configuration (the module build machinery, the sdlprefix.h
--   platform macros, X11/XInput, fontconfig, ALSA, the bx compat headers, the
--   Qt include path) but omits the SDL bits: OSD_SDL, SDLMAME_SDL2 and the SDL
--   cflags.  Dropping OSD_SDL makes the shared SDL render/input/sound/monitor/
--   font modules compile as MODULE_NOT_SUPPORTED stubs, so nothing references
--   libSDL2.  (sdl_cfg.lua is shared with the standalone OSD=sdl target and
--   must stay untouched, and GENie has no way to remove a define after the
--   fact — hence the replication.)
---------------------------------------------------------------------------

dofile('modules.lua')

-- sdlprefix.h provides the SDLMAME_WIN32/DARWIN/LINUX/BSD/HAIKU/… platform
-- macros that the (non-SDL) shared OSD code keys off.  These are just the
-- historical "this is a unix/win build" indicators; they link no SDL.
forcedincludes {
	MAME_DIR .. "src/osd/sdl/sdlprefix.h"
}

if _OPTIONS["USE_TAPTUN"]=="1" or _OPTIONS["USE_PCAP"]=="1" then
	defines {
		"USE_NETWORK",
	}
	if _OPTIONS["USE_TAPTUN"]=="1" then
		defines {
			"OSD_NET_USE_TAPTUN",
		}
	end
	if _OPTIONS["USE_PCAP"]=="1" then
		defines {
			"OSD_NET_USE_PCAP",
		}
	end
end

if _OPTIONS["NO_OPENGL"]~="1" and _OPTIONS["USE_DISPATCH_GL"]~="1" and _OPTIONS["MESA_INSTALL_ROOT"] then
	includedirs {
		path.join(_OPTIONS["MESA_INSTALL_ROOT"],"include"),
	}
end

if _OPTIONS["NO_X11"]=="1" then
	defines {
		"SDLMAME_NO_X11",
	}
else
	defines {
		"SDLMAME_X11",
	}
	includedirs {
		"/usr/X11/include",
		"/usr/X11R6/include",
		"/usr/openwin/include",
	}
end

if _OPTIONS["NO_USE_XINPUT"]=="1" then
	defines {
		"USE_XINPUT=0",
	}
else
	defines {
		"USE_XINPUT=1",
		"USE_XINPUT_DEBUG=0",
	}
end

if _OPTIONS["NO_USE_XINPUT_WII_LIGHTGUN_HACK"]=="1" then
	defines {
		"USE_XINPUT_WII_LIGHTGUN_HACK=0",
	}
else
	defines {
		"USE_XINPUT_WII_LIGHTGUN_HACK=1",
	}
end

if _OPTIONS["NO_USE_MIDI"]~="1" and _OPTIONS["targetos"]=="linux" then
	buildoptions {
		backtick(pkgconfigcmd() .. " --cflags alsa"),
	}
end

-- NOTE: SDLMAME_SDL2 / SDL2_MULTIAPI / OSD_SDL and the SDL cflags
-- (sdlconfigcmd --cflags) are intentionally NOT defined here — that is what
-- makes this a non-SDL build.

if BASE_TARGETOS=="unix" then
	defines {
		"SDLMAME_UNIX",
	}
	-- fontconfig (desktop unix, non-macOS/android) — MAME's font handling
	-- expects it.  Plus SDL2 headers for the SDL game-controller joystick module
	-- (input_sdlgame.cpp) — the only SDL we use on Linux (gamecontroller only,
	-- no video/window).
	if _OPTIONS["targetos"]~="macosx" and _OPTIONS["targetos"]~="android" and _OPTIONS["targetos"]~="asmjs" then
		buildoptions {
			backtick(pkgconfigcmd() .. " --cflags fontconfig"),
			backtick(pkgconfigcmd() .. " --cflags sdl2"),
		}
	end
end

if _OPTIONS["targetos"]=="windows" then
	configuration { "mingw* or vs*" }
		defines {
			"UNICODE",
			"_UNICODE",
			"_WIN32_WINNT=0x0600",
			"WIN32_LEAN_AND_MEAN",
			"NOMINMAX",
		}

	configuration { }

elseif _OPTIONS["targetos"]=="linux" then
	if _OPTIONS["QT_HOME"]~=nil then
		buildoptions {
			"-I" .. backtick(_OPTIONS["QT_HOME"] .. "/bin/qmake -query QT_INSTALL_HEADERS"),
		}
	else
		buildoptions {
			"-I$(shell qmake6 -query QT_INSTALL_HEADERS)",
		}
	end
elseif _OPTIONS["targetos"]=="macosx" then
	defines {
		"SDLMAME_MACOSX",
		"SDLMAME_DARWIN",
	}
elseif _OPTIONS["targetos"]=="freebsd" then
	buildoptions {
		-- /usr/local/include is not considered a system include director on FreeBSD.  GL.h resides there and throws warnings
		"-isystem /usr/local/include",
	}
end

configuration { "osx*" }
	includedirs {
		MAME_DIR .. "3rdparty/bx/include/compat/osx",
	}

configuration { "freebsd" }
	includedirs {
		MAME_DIR .. "3rdparty/bx/include/compat/freebsd",
	}

configuration { "netbsd" }
	includedirs {
		MAME_DIR .. "3rdparty/bx/include/compat/freebsd",
	}

configuration { }


-- qtui front-end include paths.
includedirs {
	MAME_DIR .. "src/osd/qtui",
	MAME_DIR .. "src/frontend",
	MAME_DIR .. "src/frontend/mame",
}

-- Phase 13: enable the Qt-native GL/BGFX branches in the shared renderers
-- (drawogl.cpp / drawbgfx.cpp).  Only the qtui build defines this, so the SDL
-- build is unaffected; the branch uses the Qt-native window/context instead of
-- SDL and keeps the renderer TUs free of both Qt and SDL headers.
defines {
	"OSD_QT_GL",
}

-- On Windows/MinGW, qtdebuggerbuild() only adds the Qt headers under a
-- configuration("mingw*") filter that does not match the gmake-mingw64-gcc
-- build, so the front-end sources fail to find <QtCore/...>.  Add the Qt
-- include path here (resolved at build time via qmake6) for every qtui
-- project.  Linux/macOS get it from qtdebuggerbuild()'s other branches.
if _OPTIONS["targetos"]=="windows" then
	buildoptions {
		"-I$(shell qmake6 -query QT_INSTALL_HEADERS)",
	}
end

-- Drop the Qt PDF manual viewer where Qt PDF is unavailable (e.g. MSYS2's
-- MinGW64, which has no qt6-pdf package).
if _OPTIONS["NO_QTPDF"]=="1" then
	defines {
		"QTUI_NO_PDF",
	}
end

-- All code linked into the qtui executable that references Qt's exported
-- data symbols must be position-independent, so those references go through
-- the GOT.  Built without -fPIC, the linker instead emits copy relocations
-- that duplicate symbols such as QWidget::staticMetaObject and the typeinfo
-- for QObject/QWidget into the executable.  That breaks C++ RTTI/metaobject
-- identity for dlopened Qt style plugins (e.g. adwaita-qt), which then crash
-- on the first window show().  MAME's qtdbg project already does this.
if _OPTIONS["targetos"]=="linux" or _OPTIONS["targetos"]=="freebsd" or _OPTIONS["targetos"]=="netbsd" or _OPTIONS["targetos"]=="openbsd" then
	buildoptions {
		"-fPIC",
	}
end
