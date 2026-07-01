#!/bin/sh
# Regenerate every GooeyMAME icon asset from the master gooeymame.png.
# Requires: ImageMagick (magick), icotool (icoutils), python3.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$here"
SRC=gooeymame.png
mkdir -p icons

# Per-size PNGs (Lanczos, 32-bit RGBA).
for s in 16 24 32 48 64 128 256 512 1024; do
	magick "$SRC" -filter Lanczos -resize ${s}x${s} -strip "PNG32:icons/gooeymame_${s}.png"
done

# Windows .ico (16..256; icotool stores 256 as PNG).
icotool -c -o gooeymame.ico \
	icons/gooeymame_16.png icons/gooeymame_24.png icons/gooeymame_32.png \
	icons/gooeymame_48.png icons/gooeymame_64.png icons/gooeymame_128.png icons/gooeymame_256.png

# macOS .icns (PNG-based slots, 16..1024) — assembled by hand since ImageMagick
# here lacks ICNS support.  Mirrors what `iconutil` emits from an .iconset.
python3 - <<'PY'
import struct
slots = [(b"icp4",16),(b"icp5",32),(b"icp6",64),(b"ic07",128),(b"ic08",256),
         (b"ic09",512),(b"ic10",1024),(b"ic11",32),(b"ic12",64),(b"ic13",256),(b"ic14",512)]
chunks = b""
for ostype, size in slots:
    data = open(f"icons/gooeymame_{size}.png","rb").read()
    chunks += ostype + struct.pack(">I", len(data)+8) + data
open("gooeymame.icns","wb").write(b"icns" + struct.pack(">I", len(chunks)+8) + chunks)
print("wrote gooeymame.icns")
PY

# Keep the embedded Windows resource icon in sync.
cp gooeymame.ico ../../../../scripts/resources/windows/mame/mame.ico
echo "Regenerated icons/, gooeymame.ico, gooeymame.icns (+ synced Windows mame.ico)."
