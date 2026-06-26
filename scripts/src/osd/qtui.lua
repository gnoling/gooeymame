-- license:BSD-3-Clause
-- copyright-holders:MAMEdev Team

---------------------------------------------------------------------------
--
--   qtui.lua
--
--   Rules for building the cross-platform Qt front-end ("qtui") OSD.
--
--   qtui is MAMEUI re-imagined as a portable Qt GUI.  It is built as an
--   integrated OSD target just like winui: a single executable that, when
--   invoked with a system on the command line, behaves exactly like SDLMAME
--   (it reuses the SDL OSD as its emulation backend), and when invoked with
--   no arguments shows the Qt browser front-end.  Launching a machine from
--   the GUI runs it in-process via emulator_info::start_frontend().
--
--   Modeled on sdl.lua -- the qtui OSD *is* the SDL OSD plus a Qt front-end
--   layer and a branching entry point (qtmain.cpp instead of sdlmain.cpp).
---------------------------------------------------------------------------

dofile("modules.lua")

-- The Qt front-end requires Qt regardless of whether the Qt debugger is
-- wanted, so force it on.  This makes osdmodulestargetconf() link the Qt
-- libraries (Qt6Core/Qt6Gui/Qt6Widgets) into the main executable and makes
-- the Qt headers available to the front-end sources.
_OPTIONS["USE_QTDEBUG"] = "1"


function maintargetosdoptions(_target,_subtarget)
	osdmodulestargetconf()

	-- The qtui front-end uses Qt Multimedia for its video/soundtrack tabs.
	-- (osdmodulestargetconf already added the Qt -L path and Core/Gui/Widgets.)
	-- Qt PDF (QtPdf + QtPdfWidgets) backs the manual viewer tab.
	if _OPTIONS["targetos"]=="windows" then
		links {
			"Qt6Multimedia.dll",
			"Qt6MultimediaWidgets.dll",
		}
		if _OPTIONS["NO_QTPDF"]~="1" then
			links {
				"Qt6Pdf.dll",
				"Qt6PdfWidgets.dll",
			}
		end
	elseif _OPTIONS["targetos"]=="macosx" then
		links {
			"QtMultimedia.framework",
			"QtMultimediaWidgets.framework",
		}
		if _OPTIONS["NO_QTPDF"]~="1" then
			links {
				"QtPdf.framework",
				"QtPdfWidgets.framework",
			}
		end
	else
		links {
			"Qt6Multimedia",
			"Qt6MultimediaWidgets",
		}
		if _OPTIONS["NO_QTPDF"]~="1" then
			links {
				"Qt6Pdf",
				"Qt6PdfWidgets",
			}
		end
	end

	-- MAME statically links its own copies of zlib, libpng, etc.  If those
	-- symbols (crc32/inflate/deflate/png_*/...) are exported in the
	-- executable's dynamic symbol table they interpose over the system
	-- libraries that Qt's runtime depends on, corrupting Qt's font/icon
	-- decoding and crashing on the first window show().  Hiding static
	-- archive symbols forces Qt to bind against the system libraries.
	if _OPTIONS["targetos"]=="linux" or _OPTIONS["targetos"]=="freebsd" or _OPTIONS["targetos"]=="netbsd" or _OPTIONS["targetos"]=="openbsd" then
		linkoptions {
			"-Wl,--exclude-libs,ALL",
		}
	end

	if _OPTIONS["USE_DISPATCH_GL"]~="1" and _OPTIONS["MESA_INSTALL_ROOT"] then
		libdirs {
			path.join(_OPTIONS["MESA_INSTALL_ROOT"],"lib"),
		}
		linkoptions {
			"-Wl,-rpath=" .. path.join(_OPTIONS["MESA_INSTALL_ROOT"],"lib"),
		}
	end

	if _OPTIONS["NO_X11"]~="1" then
		links {
			"X11",
			"Xinerama",
		}
	else
		if _OPTIONS["targetos"]=="linux" or _OPTIONS["targetos"]=="netbsd" or _OPTIONS["targetos"]=="openbsd" then
			links {
				"EGL",
			}
		end
	end

	if _OPTIONS["NO_USE_XINPUT"]~="1" then
		links {
			"Xext",
			"Xi",
		}
	end

	if BASE_TARGETOS=="unix" and _OPTIONS["targetos"]~="macosx" and _OPTIONS["targetos"]~="android" and _OPTIONS["targetos"]~="asmjs" then
		links {
			"SDL2_ttf",
		}
		local str = backtick(pkgconfigcmd() .. " --libs fontconfig")
		addlibfromstring(str)
		addoptionsfromstring(str)
	end

	if _OPTIONS["targetos"]=="windows" then
		if _OPTIONS["USE_LIBSDL"]~="1" then
			configuration { "mingw*"}
				links {
					"SDL2main",
					"SDL2",
					"imm32",
					"version",
				}
			configuration { "vs*" }
				links {
					"SDL2",
					"imm32",
					"version",
				}
			configuration { }
		else
			local str = backtick(sdlconfigcmd() .. " --libs | sed 's/ -lSDLmain//'")
			addlibfromstring(str)
			addoptionsfromstring(str)
		end
		configuration { "x32", "vs*" }
			libdirs {
				path.join(_OPTIONS["SDL_INSTALL_ROOT"],"lib","x86")
			}
		configuration { "x64", "vs*" }
			libdirs {
				path.join(_OPTIONS["SDL_INSTALL_ROOT"],"lib","x64")
			}
		configuration { }

		links {
			"dinput8",
			"psapi",
		}
	elseif _OPTIONS["targetos"]=="haiku" then
		links {
			"network",
			"bsd",
		}
	end

	configuration { "mingw*" or "vs*" }
		links {
			"psapi",
			"ole32",
		}
	configuration { }
end


function sdlconfigcmd()
	if _OPTIONS["targetos"]=="asmjs" then
		return "sdl2-config"
	elseif not _OPTIONS["SDL_INSTALL_ROOT"] then
		return pkgconfigcmd() .. " sdl2"
	else
		return path.join(_OPTIONS["SDL_INSTALL_ROOT"],"bin","sdl2") .. "-config"
	end
end


-- Locate Qt's Meta Object Compiler (moc); mirrors the logic in
-- qtdebuggerbuild() in modules.lua so the front-end headers can be moc'd.
function qtui_find_moc()
	local MOC = ""
	if (os.is("windows")) then
		local qt_host_libexecs
		if _OPTIONS["QT_HOME"]~=nil then
			qt_host_libexecs = backtick(_OPTIONS["QT_HOME"] .. "/bin/qmake -query QT_HOST_LIBEXECS")
		else
			qt_host_libexecs = backtick("qmake6 -query QT_HOST_LIBEXECS")
		end
		MOC = qt_host_libexecs .. "/moc"
	else
		if _OPTIONS["QT_HOME"]~=nil then
			local MOCTST = backtick(_OPTIONS["QT_HOME"] .. "/bin/moc --version 2>/dev/null")
			if MOCTST=='' then
				local qt_host_libexecs = backtick(_OPTIONS["QT_HOME"] .. "/bin/qmake -query QT_HOST_LIBEXECS")
				if not string.starts(qt_host_libexecs,"/") then
					qt_host_libexecs = _OPTIONS["QT_HOME"] .. "/libexec"
				end
				MOC = qt_host_libexecs .. "/moc"
			else
				MOC = _OPTIONS["QT_HOME"] .. "/bin/moc"
			end
		else
			local qt_host_libexecs = backtick("qmake6 -query QT_HOST_LIBEXECS")
			MOC = qt_host_libexecs .. "/moc"
		end
	end
	return MOC
end


newoption {
	trigger = "MESA_INSTALL_ROOT",
	description = "link against specific GL-Library - also adds rpath to executable (overridden by USE_DISPATCH_GL)",
}

newoption {
	trigger = "SDL_INI_PATH",
	description = "Default search path for .ini files",
}

newoption {
	trigger = "NO_X11",
	description = "Disable use of X11",
	allowed = {
		{ "0",  "Enable X11"  },
		{ "1",  "Disable X11" },
	},
}

if not _OPTIONS["NO_X11"] then
	if _OPTIONS["targetos"]=="windows" or _OPTIONS["targetos"]=="macosx" or _OPTIONS["targetos"]=="haiku" or _OPTIONS["targetos"]=="asmjs" or _OPTIONS["targetos"]=="android" then
		_OPTIONS["NO_X11"] = "1"
	else
		_OPTIONS["NO_X11"] = "0"
	end
end

newoption {
	trigger = "NO_QTPDF",
	description = "Disable the Qt PDF manual viewer (for platforms without Qt PDF, e.g. MSYS2 MinGW64)",
	allowed = {
		{ "0",  "Enable Qt PDF"  },
		{ "1",  "Disable Qt PDF" },
	},
}

if not _OPTIONS["NO_QTPDF"] then
	-- Default the Qt PDF manual viewer on where the QtPdf headers are present,
	-- off otherwise.  Linux/macOS ship Qt PDF; on Windows it depends on the MSYS2
	-- environment: the UCRT64 packages include qt6-pdf, the legacy MinGW64 ones do
	-- not.  Auto-detect via qmake6 so the right default is picked either way.
	if _OPTIONS["targetos"]=="windows" then
		local qt_headers = backtick("qmake6 -query QT_INSTALL_HEADERS")
		if qt_headers and qt_headers~="" and os.isdir(qt_headers .. "/QtPdf") then
			_OPTIONS["NO_QTPDF"] = "0"
		else
			_OPTIONS["NO_QTPDF"] = "1"
		end
	else
		_OPTIONS["NO_QTPDF"] = "0"
	end
end

newoption {
	trigger = "NO_USE_XINPUT",
	description = "Disable use of Xinput",
	allowed = {
		{ "0",  "Enable Xinput"  },
		{ "1",  "Disable Xinput" },
	},
}

if not _OPTIONS["NO_USE_XINPUT"] then
	if _OPTIONS["targetos"]=="windows" or _OPTIONS["targetos"]=="macosx" or _OPTIONS["targetos"]=="haiku" or _OPTIONS["targetos"]=="asmjs" or _OPTIONS["targetos"]=="android" then
		_OPTIONS["NO_USE_XINPUT"] = "1"
	else
		_OPTIONS["NO_USE_XINPUT"] = "0"
	end
end

newoption {
	trigger = "NO_USE_XINPUT_WII_LIGHTGUN_HACK",
	description = "Disable use of Xinput Wii Lightgun Hack",
	allowed = {
		{ "0",  "Enable Xinput Wii Lightgun Hack"  },
		{ "1",  "Disable Xinput Wii Lightgun Hack" },
	},
}

if not _OPTIONS["NO_USE_XINPUT_WII_LIGHTGUN_HACK"] then
	_OPTIONS["NO_USE_XINPUT_WII_LIGHTGUN_HACK"] = "1"
end

newoption {
	trigger = "SDL2_MULTIAPI",
	description = "Use couriersud's multi-keyboard patch for SDL 2.1? (this API was removed prior to the 2.0 release)",
	allowed = {
		{ "0",  "Use single-keyboard API"  },
		{ "1",  "Use multi-keyboard API"   },
	},
}

if not _OPTIONS["SDL2_MULTIAPI"] then
	_OPTIONS["SDL2_MULTIAPI"] = "0"
end

newoption {
	trigger = "SDL_INSTALL_ROOT",
	description = "Equivalent to the ./configure --prefix=<path>",
}

newoption {
	trigger = "SDL_FRAMEWORK_PATH",
	description = "Location of SDL framework for custom OS X installations",
}

if not _OPTIONS["SDL_FRAMEWORK_PATH"] then
	_OPTIONS["SDL_FRAMEWORK_PATH"] = "/Library/Frameworks/"
end

newoption {
	trigger = "USE_LIBSDL",
	description = "Use SDL library on OS (rather than framework/dll)",
	allowed = {
		{ "0",  "Use framework/dll"  },
		{ "1",  "Use library" },
	},
}

if not _OPTIONS["USE_LIBSDL"] then
	_OPTIONS["USE_LIBSDL"] = "0"
end


BASE_TARGETOS       = "unix"
SDLOS_TARGETOS      = "unix"
if _OPTIONS["targetos"]=="windows" then
	BASE_TARGETOS       = "win32"
	SDLOS_TARGETOS      = "win32"
elseif _OPTIONS["targetos"]=="macosx" then
	SDLOS_TARGETOS      = "macosx"
end

if BASE_TARGETOS=="unix" then
	if _OPTIONS["targetos"]=="macosx" then
		local os_version = str_to_version(backtick("sw_vers -productVersion"))

		links {
			"Cocoa.framework",
		}
		linkoptions {
			"-framework QuartzCore",
			"-framework OpenGL",
			"-framework IOKit",
			"-rpath " .. _OPTIONS["SDL_FRAMEWORK_PATH"],
		}

		if os_version>=101100 then
			linkoptions {
				"-weak_framework Metal",
			}
		end
		if _OPTIONS["USE_LIBSDL"]~="1" then
			linkoptions {
				"-F" .. _OPTIONS["SDL_FRAMEWORK_PATH"],
			}
			links {
				"SDL2.framework",
			}
		else
			local str = backtick(sdlconfigcmd() .. " --libs --static | sed 's/-lSDLmain//'")
			addlibfromstring(str)
			addoptionsfromstring(str)
		end
	else
		if _OPTIONS["NO_X11"]=="1" then
			-- the Qt front-end still needs X11/Wayland via Qt itself, but the
			-- SDL backend X11 video path is disabled here as in sdl.lua
		else
			libdirs {
				"/usr/X11/lib",
				"/usr/X11R6/lib",
				"/usr/openwin/lib",
			}
		end
		local str = backtick(sdlconfigcmd() .. " --libs")
		addlibfromstring(str)
		addoptionsfromstring(str)

		if _OPTIONS["targetos"]~="haiku" and _OPTIONS["targetos"]~="android" then
			links {
				"m",
				"pthread",
			}
			if _OPTIONS["targetos"]=="solaris" then
				links {
					"socket",
					"nsl",
				}
			elseif _OPTIONS["targetos"]~="asmjs" then
				links {
					"util",
				}
			end
		end
	end
end


-------------------------------------------------------------------------------
-- Qt debugger project (re-uses the standard mechanism as other OSD layers)
-------------------------------------------------------------------------------
project ("qtdbg_" .. _OPTIONS["osd"])
	uuid (os.uuid("qtdbg_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("qtui_cfg.lua")
	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices", -- accessing imagedev from debugger
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
	}
	configuration { "linux-* or freebsd" }
		buildoptions {
			"-fPIC",
		}
	configuration { }

	qtdebuggerbuild()


-------------------------------------------------------------------------------
-- OSD core library: SDL emulation backend + Qt front-end
-------------------------------------------------------------------------------
project ("osd_" .. _OPTIONS["osd"])
	uuid (os.uuid("osd_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	dofile("qtui_cfg.lua")
	osdmodulesbuild()

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/devices", -- accessing imagedev from debugger
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/modules/file",
		MAME_DIR .. "src/osd/modules/render",
		MAME_DIR .. "3rdparty",
		MAME_DIR .. "3rdparty/zlib",
		MAME_DIR .. "src/osd/sdl",
		MAME_DIR .. "src/osd/qtui",
		MAME_DIR .. "src/frontend",
		MAME_DIR .. "src/frontend/mame",
		MAME_DIR .. "generated/mame",
		MAME_DIR .. "generated/mame/" .. _OPTIONS["subtarget"],
	}

	files {
		-- SDL OSD emulation backend (note: sdlmain.cpp is intentionally
		-- omitted -- qtmain.cpp provides the single program entry point)
		MAME_DIR .. "src/osd/osdepend.h",
		MAME_DIR .. "src/osd/modules/osdwindow.cpp",
		MAME_DIR .. "src/osd/modules/osdwindow.h",
		MAME_DIR .. "src/osd/sdl/osdsdl.cpp",
		MAME_DIR .. "src/osd/sdl/osdsdl.h",
		MAME_DIR .. "src/osd/sdl/sdlopts.cpp",
		MAME_DIR .. "src/osd/sdl/sdlopts.h",
		MAME_DIR .. "src/osd/sdl/sdlprefix.h",
		MAME_DIR .. "src/osd/sdl/video.cpp",
		MAME_DIR .. "src/osd/sdl/window.cpp",
		MAME_DIR .. "src/osd/sdl/window.h",

		-- Qt front-end
		MAME_DIR .. "src/osd/qtui/qtmain.cpp",
		MAME_DIR .. "src/osd/qtui/emulator.cpp",
		MAME_DIR .. "src/osd/qtui/emulator.h",
		MAME_DIR .. "src/osd/qtui/mainwindow.cpp",
		MAME_DIR .. "src/osd/qtui/mainwindow.h",
		MAME_DIR .. "src/osd/qtui/gamelistmodel.cpp",
		MAME_DIR .. "src/osd/qtui/gamelistmodel.h",
		MAME_DIR .. "src/osd/qtui/gamelistproxy.cpp",
		MAME_DIR .. "src/osd/qtui/gamelistproxy.h",
		MAME_DIR .. "src/osd/qtui/foldertree.cpp",
		MAME_DIR .. "src/osd/qtui/foldertree.h",
		MAME_DIR .. "src/osd/qtui/softwaremodel.cpp",
		MAME_DIR .. "src/osd/qtui/softwaremodel.h",
		MAME_DIR .. "src/osd/qtui/softwareproxy.cpp",
		MAME_DIR .. "src/osd/qtui/softwareproxy.h",
		MAME_DIR .. "src/osd/qtui/softwareloader.cpp",
		MAME_DIR .. "src/osd/qtui/softwareloader.h",
		MAME_DIR .. "src/osd/qtui/auditmanager.cpp",
		MAME_DIR .. "src/osd/qtui/auditmanager.h",
		MAME_DIR .. "src/osd/qtui/softwareauditmanager.cpp",
		MAME_DIR .. "src/osd/qtui/softwareauditmanager.h",
		MAME_DIR .. "src/osd/qtui/optionsdialog.cpp",
		MAME_DIR .. "src/osd/qtui/optionsdialog.h",
		MAME_DIR .. "src/osd/qtui/frontendpaths.cpp",
		MAME_DIR .. "src/osd/qtui/frontendpaths.h",
		MAME_DIR .. "src/osd/qtui/artworkpanel.cpp",
		MAME_DIR .. "src/osd/qtui/artworkpanel.h",
		MAME_DIR .. "src/osd/qtui/artloader.cpp",
		MAME_DIR .. "src/osd/qtui/artloader.h",
		MAME_DIR .. "src/osd/qtui/infoloader.cpp",
		MAME_DIR .. "src/osd/qtui/infoloader.h",
		MAME_DIR .. "src/osd/qtui/mediatabs.cpp",
		MAME_DIR .. "src/osd/qtui/mediatabs.h",
		MAME_DIR .. "src/osd/qtui/iconloader.cpp",
		MAME_DIR .. "src/osd/qtui/thumbnailloader.cpp",
		MAME_DIR .. "src/osd/qtui/thumbnailloader.h",
		MAME_DIR .. "src/osd/qtui/gridview.cpp",
		MAME_DIR .. "src/osd/qtui/gridview.h",
		MAME_DIR .. "src/osd/qtui/regions.cpp",
		MAME_DIR .. "src/osd/qtui/regions.h",
		MAME_DIR .. "src/osd/qtui/familytreemodel.cpp",
		MAME_DIR .. "src/osd/qtui/familytreemodel.h",
		MAME_DIR .. "src/osd/qtui/embedsession.h",
		MAME_DIR .. "src/osd/qtui/iconloader.h",

		-- Qt-native OSD (Phase 13): QWindow-backed render window + GL context.
		-- No Q_OBJECT in these, so no moc rules are needed.
		MAME_DIR .. "src/osd/qtui/qtwindow.cpp",
		MAME_DIR .. "src/osd/qtui/qtwindow.h",
		MAME_DIR .. "src/osd/qtui/qtglcontext.h",
		MAME_DIR .. "src/osd/qtui/qtglprovider.h",
		MAME_DIR .. "src/osd/qtui/qtnativewindow.h",
		MAME_DIR .. "src/osd/qtui/qtembedtarget.h",

		-- Qt-native input (Phase 13b): Qt-free event bus + MAME-side providers.
		MAME_DIR .. "src/osd/qtui/qtinput.cpp",
		MAME_DIR .. "src/osd/qtui/qtinput.h",
		MAME_DIR .. "src/osd/modules/input/input_qt.cpp",

		-- BGFX shader-effect query/select bridge (impl in drawbgfx.cpp).
		MAME_DIR .. "src/osd/qtui/qtbgfxchains.h",

		-- Qt-native monitor module (Phase 13c): QScreen geometry, no SDL.
		MAME_DIR .. "src/osd/qtui/qtmonitors.cpp",
		MAME_DIR .. "src/osd/qtui/qtmonitors.h",
		MAME_DIR .. "src/osd/modules/monitor/monitor_qt.cpp",
		GEN_DIR .. "osd/qtui/mainwindow.moc.cpp",
		GEN_DIR .. "osd/qtui/gamelistmodel.moc.cpp",
		GEN_DIR .. "osd/qtui/gamelistproxy.moc.cpp",
		GEN_DIR .. "osd/qtui/foldertree.moc.cpp",
		GEN_DIR .. "osd/qtui/softwaremodel.moc.cpp",
		GEN_DIR .. "osd/qtui/softwareproxy.moc.cpp",
		GEN_DIR .. "osd/qtui/softwareloader.moc.cpp",
		GEN_DIR .. "osd/qtui/auditmanager.moc.cpp",
		GEN_DIR .. "osd/qtui/softwareauditmanager.moc.cpp",
		GEN_DIR .. "osd/qtui/optionsdialog.moc.cpp",
		GEN_DIR .. "osd/qtui/artworkpanel.moc.cpp",
		GEN_DIR .. "osd/qtui/artloader.moc.cpp",
		GEN_DIR .. "osd/qtui/infoloader.moc.cpp",
		GEN_DIR .. "osd/qtui/iconloader.moc.cpp",
		GEN_DIR .. "osd/qtui/thumbnailloader.moc.cpp",
		GEN_DIR .. "osd/qtui/gridview.moc.cpp",
		GEN_DIR .. "osd/qtui/familytreemodel.moc.cpp",
	}

	-- The PDF manual viewer is only built where Qt PDF is available.
	if _OPTIONS["NO_QTPDF"]~="1" then
		files {
			MAME_DIR .. "src/osd/qtui/manualtab.cpp",
			MAME_DIR .. "src/osd/qtui/manualtab.h",
		}
	end

	local MOC = qtui_find_moc()
	custombuildtask {
		{ MAME_DIR .. "src/osd/qtui/mainwindow.h", GEN_DIR .. "osd/qtui/mainwindow.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/gamelistmodel.h", GEN_DIR .. "osd/qtui/gamelistmodel.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/gamelistproxy.h", GEN_DIR .. "osd/qtui/gamelistproxy.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/foldertree.h", GEN_DIR .. "osd/qtui/foldertree.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/softwaremodel.h", GEN_DIR .. "osd/qtui/softwaremodel.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/softwareproxy.h", GEN_DIR .. "osd/qtui/softwareproxy.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/softwareloader.h", GEN_DIR .. "osd/qtui/softwareloader.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/auditmanager.h", GEN_DIR .. "osd/qtui/auditmanager.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/softwareauditmanager.h", GEN_DIR .. "osd/qtui/softwareauditmanager.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/optionsdialog.h", GEN_DIR .. "osd/qtui/optionsdialog.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/artworkpanel.h", GEN_DIR .. "osd/qtui/artworkpanel.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/artloader.h", GEN_DIR .. "osd/qtui/artloader.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/infoloader.h", GEN_DIR .. "osd/qtui/infoloader.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/iconloader.h", GEN_DIR .. "osd/qtui/iconloader.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/thumbnailloader.h", GEN_DIR .. "osd/qtui/thumbnailloader.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/gridview.h", GEN_DIR .. "osd/qtui/gridview.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
		{ MAME_DIR .. "src/osd/qtui/familytreemodel.h", GEN_DIR .. "osd/qtui/familytreemodel.moc.cpp", { }, { MOC .. " $(MOCINCPATH) $(<) -o $(@)" } },
	}


-------------------------------------------------------------------------------
-- OSD bootstrap / core utilities library
-------------------------------------------------------------------------------
project ("ocore_" .. _OPTIONS["osd"])
	uuid (os.uuid("ocore_" .. _OPTIONS["osd"]))
	kind (LIBTYPE)

	removeflags {
		"SingleOutputDir",
	}

	dofile("qtui_cfg.lua")

	includedirs {
		MAME_DIR .. "src/emu",
		MAME_DIR .. "src/osd",
		MAME_DIR .. "src/lib",
		MAME_DIR .. "src/lib/util",
		MAME_DIR .. "src/osd/sdl",
	}

	files {
		MAME_DIR .. "src/osd/osdcore.cpp",
		MAME_DIR .. "src/osd/osdcore.h",
		MAME_DIR .. "src/osd/osdfile.h",
		MAME_DIR .. "src/osd/strconv.cpp",
		MAME_DIR .. "src/osd/strconv.h",
		MAME_DIR .. "src/osd/osdsync.cpp",
		MAME_DIR .. "src/osd/osdsync.h",
		MAME_DIR .. "src/osd/modules/osdmodule.cpp",
		MAME_DIR .. "src/osd/modules/osdmodule.h",
		MAME_DIR .. "src/osd/modules/lib/osdlib_" .. SDLOS_TARGETOS .. ".cpp",
		MAME_DIR .. "src/osd/modules/lib/osdlib.h",
	}

	if BASE_TARGETOS=="unix" then
		files {
			MAME_DIR .. "src/osd/modules/file/posixdir.cpp",
			MAME_DIR .. "src/osd/modules/file/posixfile.cpp",
			MAME_DIR .. "src/osd/modules/file/posixfile.h",
			MAME_DIR .. "src/osd/modules/file/posixptty.cpp",
			MAME_DIR .. "src/osd/modules/file/posixsocket.cpp",
		}
	elseif BASE_TARGETOS=="win32" then
		includedirs {
			MAME_DIR .. "src/osd/windows",
		}
		files {
			MAME_DIR .. "src/osd/modules/file/windir.cpp",
			MAME_DIR .. "src/osd/modules/file/winfile.cpp",
			MAME_DIR .. "src/osd/modules/file/winfile.h",
			MAME_DIR .. "src/osd/modules/file/winptty.cpp",
			MAME_DIR .. "src/osd/modules/file/winsocket.cpp",
			MAME_DIR .. "src/osd/windows/winutil.cpp",
			MAME_DIR .. "src/osd/windows/winutil.h",
		}
	else
		files {
			MAME_DIR .. "src/osd/modules/file/stdfile.cpp",
		}
	end
