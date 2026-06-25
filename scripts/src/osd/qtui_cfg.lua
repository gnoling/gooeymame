-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   qtui_cfg.lua
--
--   Per-object compile configuration for the qtui (Qt front-end) OSD.
--   The qtui OSD reuses the SDL OSD as its emulation backend, so it shares
--   the SDL compile configuration and simply adds the Qt front-end include
--   paths on top.
---------------------------------------------------------------------------

dofile('sdl_cfg.lua')

includedirs {
	MAME_DIR .. "src/osd/qtui",
	MAME_DIR .. "src/frontend",
	MAME_DIR .. "src/frontend/mame",
}

-- Phase 13: enable the Qt-native GL context branch in the shared OpenGL
-- renderer (drawogl.cpp).  Only the qtui build defines this, so the SDL build
-- is unaffected; the branch falls back to the SDL context for SDL windows.
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
