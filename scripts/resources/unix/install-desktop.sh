#!/bin/sh
# Install the GooeyMAME icon + desktop entry into the freedesktop hicolor theme.
#
# Usage:
#   ./install-desktop.sh [--system] [BINARY_PATH]
#
#   (no args)      install for the current user   (~/.local/share)
#   --system       install system-wide            (/usr/share; needs root)
#   BINARY_PATH    absolute path to the built `mame` binary; written into the
#                  desktop entry's Exec= line (default: leaves it as "mame",
#                  i.e. resolved from PATH)
#
# Icons are installed at every packaged size; run `gtk-update-icon-cache` /
# `update-desktop-database` if your desktop doesn't pick them up automatically.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
res_icons="$here/../../../src/osd/qtui/res/icons"

prefix="$HOME/.local/share"
binary="mame"
for arg in "$@"; do
	case "$arg" in
		--system) prefix="/usr/share" ;;
		*)        binary="$arg" ;;
	esac
done

icondir="$prefix/icons/hicolor"
appdir="$prefix/applications"
mkdir -p "$appdir"

for sz in 16 24 32 48 64 128 256 512; do
	dst="$icondir/${sz}x${sz}/apps"
	mkdir -p "$dst"
	cp "$res_icons/gooeymame_${sz}.png" "$dst/gooeymame.png"
done

# Desktop entry, with Exec pointed at the requested binary.
sed "s|^Exec=mame$|Exec=$binary|" "$here/gooeymame.desktop" > "$appdir/gooeymame.desktop"

echo "Installed GooeyMAME icons under $icondir and $appdir/gooeymame.desktop"
command -v gtk-update-icon-cache >/dev/null 2>&1 && gtk-update-icon-cache -q -t "$icondir" || true
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$appdir" || true
