#!/bin/bash
# build-portable.sh - Build a self-contained, relocatable Chiplet Studio bundle
# that runs with NO Docker, NO root, and NO system dependencies on the target.
#
# Outputs (under dist/):
#   chiplet-studio-portable.tar.gz   PRIMARY - extract anywhere, run ./AppRun
#   Chiplet_Studio-x86_64.AppImage   optional single-file (--appimage)
#
# Bundles: the app + KLayout libs/plugins + Qt6 + libstdc++/libgcc +
#          Python 3.10 stdlib + chiplet_studio module + configs/pdks data +
#          (default) a Mesa llvmpipe software-GL stack for machines without a GPU.
#
# GL strategy: the launcher probes the host for an OpenGL 3.3 Core context and
# falls back to the bundled software renderer when the host can't provide one.
#
# Usage:
#   ./scripts/build-portable.sh            # full bundle (with software GL)
#   ./scripts/build-portable.sh --lean     # smaller bundle, host GL only (~65 MB)
#   ./scripts/build-portable.sh --appimage # also emit an AppImage
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

WITH_SOFTWARE_GL=1
MAKE_APPIMAGE=0
for arg in "$@"; do
    case "$arg" in
        --lean)     WITH_SOFTWARE_GL=0 ;;
        --appimage) MAKE_APPIMAGE=1 ;;
        *) echo "Unknown option: $arg"; exit 1 ;;
    esac
done

if [ ! -f "$PROJECT_DIR/build/chiplet-studio" ]; then
    echo "Error: build/chiplet-studio not found. Run ./scripts/build-docker.sh first."
    exit 1
fi

mkdir -p "$PROJECT_DIR/dist"

# Capture commit SHAs on the host (git is available here, not necessarily in the
# container) for the corresponding-source notice shipped with the binary, as
# required by GPL-3.0-or-later.
GIT_COMMIT=$(git -C "$PROJECT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)
KLAYOUT_COMMIT=$(git -C "$PROJECT_DIR/extern/klayout" rev-parse HEAD 2>/dev/null || echo unknown)
GDS3D_COMMIT=$(git -C "$PROJECT_DIR/extern/GDS3D" rev-parse HEAD 2>/dev/null || echo unknown)

docker run --rm --user root \
    -e WITH_SOFTWARE_GL="$WITH_SOFTWARE_GL" \
    -e MAKE_APPIMAGE="$MAKE_APPIMAGE" \
    -e GIT_COMMIT="$GIT_COMMIT" \
    -e KLAYOUT_COMMIT="$KLAYOUT_COMMIT" \
    -e GDS3D_COMMIT="$GDS3D_COMMIT" \
    -v "$PROJECT_DIR:/workspace" \
    chiplet-studio-build bash -euo pipefail -c '
WS=/workspace
APPDIR=$WS/AppDir
KL=$WS/extern/klayout/bin-release

echo ">>> Installing packaging tools..."
apt-get update -qq >/dev/null
apt-get install -y -qq wget file patchelf gcc libgl-dev libx11-dev libgl1-mesa-dri >/dev/null

echo ">>> Fetching linuxdeploy (+ qt plugin)..."
cd /tmp
for t in linuxdeploy linuxdeploy-plugin-qt; do
    [ -d "/tmp/$t" ] && continue
    wget -q "https://github.com/linuxdeploy/$t/releases/download/continuous/$t-x86_64.AppImage"
    chmod +x "$t-x86_64.AppImage"
    "./$t-x86_64.AppImage" --appimage-extract >/dev/null
    mv squashfs-root "$t"
    # Remove the .AppImage so linuxdeploy cannot pick the FUSE-needing plugin
    # over the extracted one (FUSE is unavailable in Docker).
    rm -f "/tmp/$t-x86_64.AppImage"
done

echo ">>> Building clean AppDir..."
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/plugins" \
         "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
         "$APPDIR/usr/lib/python-modules" "$APPDIR/usr/share/chiplet-studio"

cp "$WS/build/chiplet-studio" "$APPDIR/usr/bin/"

echo ">>> Copying KLayout libraries + plugins..."
cp -a "$KL"/*.so* "$APPDIR/usr/lib/" 2>/dev/null || true
for sub in db_plugins lay_plugins; do
    if [ -d "$KL/$sub" ]; then
        mkdir -p "$APPDIR/usr/lib/$sub"
        cp -a "$KL/$sub"/*.so* "$APPDIR/usr/lib/$sub/" 2>/dev/null || true
    fi
done

echo ">>> Desktop file + icon..."
cat > "$APPDIR/usr/share/applications/chiplet-studio.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Chiplet Studio
Comment=3D Chiplet Assembly Design Tool
Exec=chiplet-studio
Icon=chiplet-studio
Categories=Development;Engineering;
Terminal=false
EOF
cp "$APPDIR/usr/share/applications/chiplet-studio.desktop" "$APPDIR/chiplet-studio.desktop"
cat > "$APPDIR/usr/share/icons/hicolor/scalable/apps/chiplet-studio.svg" <<EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><rect x="20" y="20" width="216" height="216" rx="20" fill="#2563eb"/><rect x="50" y="80" width="60" height="40" fill="#fff"/><rect x="146" y="80" width="60" height="40" fill="#fff"/><rect x="50" y="136" width="60" height="40" fill="#fff"/><rect x="146" y="136" width="60" height="40" fill="#fff"/><rect x="98" y="60" width="60" height="136" fill="#60a5fa" opacity=".5"/></svg>
EOF
cp "$APPDIR/usr/share/icons/hicolor/scalable/apps/chiplet-studio.svg" "$APPDIR/chiplet-studio.svg"

echo ">>> Running linuxdeploy to bundle Qt + transitive deps..."
export QMAKE=/usr/bin/qmake6
export PATH="/tmp/linuxdeploy-plugin-qt/usr/bin:$PATH"
export LD_LIBRARY_PATH="$KL:$KL/db_plugins:$KL/lay_plugins:${LD_LIBRARY_PATH:-}"
cd "$WS"   # run from a dir without the plugin .AppImage in it
/tmp/linuxdeploy/usr/bin/linuxdeploy \
    --appdir "$APPDIR" \
    --executable "$WS/build/chiplet-studio" \
    --desktop-file "$APPDIR/chiplet-studio.desktop" \
    --icon-file "$APPDIR/chiplet-studio.svg" \
    --plugin qt >/tmp/linuxdeploy.log 2>&1 || { echo "linuxdeploy failed:"; tail -30 /tmp/linuxdeploy.log; exit 1; }

echo ">>> Force-bundling libstdc++ / libgcc (linuxdeploy excludes them)..."
cp -aL /usr/lib/x86_64-linux-gnu/libstdc++.so.6 "$APPDIR/usr/lib/" 2>/dev/null || true
cp -aL /lib/x86_64-linux-gnu/libgcc_s.so.1       "$APPDIR/usr/lib/" 2>/dev/null || true

echo ">>> Bundling Python 3.10 stdlib (trimmed) + chiplet_studio module..."
PYSRC=/usr/lib/python3.10
PYDST="$APPDIR/usr/lib/python3.10"
mkdir -p "$PYDST"
cp -a "$PYSRC"/. "$PYDST"/
# Trim heavy bits the embedded interpreter does not need
( cd "$PYDST" && rm -rf test tests idlelib turtledemo tkinter lib2to3 ensurepip \
    distutils/tests sqlite3/test unittest/test config-3.10-x86_64-linux-gnu \
    $(find . -type d -name __pycache__) )
find "$PYDST" -name "*.pyc" -delete 2>/dev/null || true
cp -a "$WS/build/python/chiplet_studio."*.so "$APPDIR/usr/lib/python-modules/" 2>/dev/null || \
    echo "  WARN: chiplet_studio module not found (scripting will be limited)"

echo ">>> Bundling runtime data (configs + pdks)..."
cp -a "$WS/configs" "$APPDIR/usr/share/chiplet-studio/configs"
cp -a "$WS/pdks"    "$APPDIR/usr/share/chiplet-studio/pdks"

echo ">>> Bundling license texts + corresponding-source notice (GPL compliance)..."
cp "$WS/LICENSE" "$APPDIR/LICENSE"
cp "$WS/THIRD-PARTY-LICENSES.md" "$APPDIR/THIRD-PARTY-LICENSES.md"
cat > "$APPDIR/SOURCE.txt" <<EOF
Chiplet Studio - corresponding source and licenses
===================================================

Chiplet Studio is free software licensed under GPL-3.0-or-later.
See LICENSE for the full license text and THIRD-PARTY-LICENSES.md for the
licenses of all bundled and linked third-party components.

Corresponding source (per GPL-3.0-or-later, section 6):

  Chiplet Studio        https://github.com/IHP-GmbH/chiplet-studio
  (GPL-3.0-or-later)    commit ${GIT_COMMIT}

  KLayout               https://github.com/KLayout/klayout
  (GPL-3.0-or-later)    commit ${KLAYOUT_COMMIT}

  GDS3D / libgdsto3d    https://github.com/trilomix/GDS3D
  (LGPL-2.1-or-later)   commit ${GDS3D_COMMIT}

Qt, CPython, Mesa, LLVM, glvnd, FreeType, HarfBuzz, Fontconfig and the other
runtime libraries are bundled unmodified from their upstream releases (Ubuntu
22.04 packages); their sources are available from those upstream projects and
distributions. See THIRD-PARTY-LICENSES.md for details.

Copyright (C) 2026 IHP GmbH.
EOF

echo ">>> Bundling a base font (DejaVu) for hosts without system fonts..."
apt-get install -y -qq fonts-dejavu-core >/dev/null 2>&1 || true
mkdir -p "$APPDIR/usr/share/fonts/truetype/dejavu"
cp -a /usr/share/fonts/truetype/dejavu/. "$APPDIR/usr/share/fonts/truetype/dejavu/" 2>/dev/null || \
    find /usr/share/fonts -name "DejaVu*.ttf" -exec cp {} "$APPDIR/usr/share/fonts/truetype/dejavu/" \; 2>/dev/null || true

echo ">>> Compiling gl_probe helper..."
gcc -O2 -o "$APPDIR/usr/bin/gl_probe" "$WS/scripts/gl_probe.c" -lGL -lX11 \
    || echo "  WARN: gl_probe failed to build (auto GL detection will assume software)"

if [ "${WITH_SOFTWARE_GL}" = "1" ]; then
    echo ">>> Bundling Mesa llvmpipe software-GL stack..."
    MESA="$APPDIR/usr/lib/mesa-software"
    mkdir -p "$MESA/dri"
    GL=/usr/lib/x86_64-linux-gnu
    # glvnd dispatch + Mesa vendor + DRI driver
    for f in libGL.so.1 libGLX.so.0 libGLdispatch.so.0 libOpenGL.so.0 \
             libGLX_mesa.so.0 libglapi.so.0 libGLX_indirect.so.0; do
        [ -e "$GL/$f" ] && cp -aL "$GL/$f" "$MESA/" 2>/dev/null || true
    done
    cp -aL "$GL/dri/swrast_dri.so" "$MESA/dri/"
    # ldd closure of the driver (libLLVM, libdrm, libz, ...), excluding glibc core
    excl="ld-linux|libc\.so|libm\.so|libdl\.so|libpthread|librt\.so|libresolv|linux-vdso"
    for seed in "$MESA/dri/swrast_dri.so" "$MESA/libGLX_mesa.so.0"; do
        ldd "$seed" 2>/dev/null | awk "/=> \//{print \$3}" | while read -r lib; do
            base=$(basename "$lib")
            echo "$base" | grep -qE "$excl" && continue
            [ -e "$MESA/$base" ] && continue
            cp -aL "$lib" "$MESA/" 2>/dev/null || true
        done
    done
    echo "    mesa-software size: $(du -sh "$MESA" | cut -f1)"
fi

echo ">>> Completing dependency closure (re-bundling libs linuxdeploy excluded)..."
# linuxdeploy drops libs it assumes the host provides (libOpenGL, libEGL,
# libfontconfig, libharfbuzz, glvnd, ...). A no-root target may not have them,
# so pull the full closure into usr/lib -- everything EXCEPT the glibc/loader
# core, which must always come from the host kernel/loader.
GLIBC_EXCL="^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libnsl|libutil|libpython3.*)\.so"
for pass in 1 2 3 4 5 6; do
    added=0
    deps=$(for f in "$APPDIR"/usr/bin/chiplet-studio "$APPDIR"/usr/bin/gl_probe \
                    "$APPDIR"/usr/lib/*.so* "$APPDIR"/usr/lib/db_plugins/*.so* \
                    "$APPDIR"/usr/lib/lay_plugins/*.so* "$APPDIR"/usr/plugins/*/*.so*; do
               [ -f "$f" ] && ldd "$f" 2>/dev/null | awk "/=> \//{print \$3}"
           done | sort -u)
    while IFS= read -r dep; do
        [ -n "$dep" ] || continue
        base=$(basename "$dep")
        echo "$base" | grep -qE "$GLIBC_EXCL" && continue
        [ -e "$APPDIR/usr/lib/$base" ] && continue
        cp -aL "$dep" "$APPDIR/usr/lib/" 2>/dev/null && added=1
    done <<< "$deps"
    [ "$added" = "0" ] && { echo "    closure complete after pass $pass"; break; }
done

echo ">>> Writing AppRun launcher..."
# linuxdeploy leaves AppRun as a symlink to the executable; remove it first so
# the heredoc creates a real file instead of following the symlink and
# clobbering the binary.
rm -f "$APPDIR/AppRun"
cat > "$APPDIR/AppRun" <<"APPRUN"
#!/bin/bash
# Chiplet Studio portable launcher. Self-contained: no system deps required.
HERE="$(dirname "$(readlink -f "$0")")"

export LD_LIBRARY_PATH="$HERE/usr/lib:$HERE/usr/lib/db_plugins:$HERE/usr/lib/lay_plugins:${LD_LIBRARY_PATH:-}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/usr/plugins/platforms"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"

# Fontconfig: synthesize a config pointing at the bundled font so text renders
# on hosts that ship no /etc/fonts and no fonts. Written to a writable cache dir
# because the bundle itself may be read-only (e.g. an AppImage mount).
FC_DIR="${XDG_CACHE_HOME:-${HOME:-/tmp}/.cache}/chiplet-studio/fontconfig"
if mkdir -p "$FC_DIR/cache" 2>/dev/null; then
    cat > "$FC_DIR/fonts.conf" <<FC
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>$HERE/usr/share/fonts</dir>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <cachedir>$FC_DIR/cache</cachedir>
</fontconfig>
FC
    export FONTCONFIG_FILE="$FC_DIR/fonts.conf"
fi

# Embedded Python interpreter: point at the bundled stdlib + module.
export PYTHONHOME="$HERE/usr"
export PYTHONPATH="$HERE/usr/lib/python-modules:${PYTHONPATH:-}"
export PYTHONDONTWRITEBYTECODE=1

# --- OpenGL: prefer host GL; fall back to bundled llvmpipe software renderer ---
activate_software_gl() {
    if [ -d "$HERE/usr/lib/mesa-software" ]; then
        export LD_LIBRARY_PATH="$HERE/usr/lib/mesa-software:$LD_LIBRARY_PATH"
        export LIBGL_DRIVERS_PATH="$HERE/usr/lib/mesa-software/dri"
        export __GLX_VENDOR_LIBRARY_NAME=mesa
        export GALLIUM_DRIVER=llvmpipe
        export LIBGL_ALWAYS_SOFTWARE=1
        return 0
    fi
    return 1
}

case "${CHIPLET_GL:-auto}" in
    software) activate_software_gl || echo "chiplet-studio: no bundled software GL available" >&2 ;;
    hardware) : ;;  # use host GL untouched
    *)        # auto: probe the host for a 3.3 core context, using the HOST GL
              # stack (clean env) so the verdict reflects the real hardware path.
        if [ -x "$HERE/usr/bin/gl_probe" ] && \
           env -i DISPLAY="${DISPLAY:-}" XAUTHORITY="${XAUTHORITY:-}" HOME="${HOME:-/tmp}" \
               "$HERE/usr/bin/gl_probe" >/dev/null 2>&1; then
            : # host GL is good enough
        else
            if activate_software_gl; then
                echo "chiplet-studio: host OpenGL 3.3 unavailable -> using bundled software renderer (llvmpipe)" >&2
            fi
        fi ;;
esac

exec "$HERE/usr/bin/chiplet-studio" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
ln -sf AppRun "$APPDIR/run.sh"

echo ">>> Packaging tarball..."
cd "$WS"
rm -rf /tmp/pkg && mkdir -p /tmp/pkg/chiplet-studio-portable
cp -a "$APPDIR"/. /tmp/pkg/chiplet-studio-portable/
cat > /tmp/pkg/chiplet-studio-portable/README.txt <<EOF
Chiplet Studio - portable build
Run:   ./AppRun  [optional-file.chiplet]
No Docker, root or system install required.

License: GPL-3.0-or-later. See LICENSE, THIRD-PARTY-LICENSES.md and SOURCE.txt
(the latter lists the corresponding source for this binary).

OpenGL: by default the launcher uses your GPU if it provides OpenGL 3.3,
otherwise it transparently falls back to a bundled software renderer.
Force a mode with the CHIPLET_GL environment variable:
  CHIPLET_GL=hardware ./AppRun     # always use the host GPU
  CHIPLET_GL=software ./AppRun     # always use the bundled software renderer
EOF
tar -C /tmp/pkg -czf "$WS/dist/chiplet-studio-portable.tar.gz" chiplet-studio-portable
echo "    tarball: $(du -h "$WS/dist/chiplet-studio-portable.tar.gz" | cut -f1)"

if [ "${MAKE_APPIMAGE}" = "1" ]; then
    echo ">>> Building AppImage (FUSE-less)..."
    cd /tmp
    [ -d /tmp/appimagetool ] || {
        wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O /tmp/ait
        chmod +x /tmp/ait; /tmp/ait --appimage-extract >/dev/null; mv squashfs-root /tmp/appimagetool
    }
    ARCH=x86_64 /tmp/appimagetool/AppRun "$APPDIR" "$WS/dist/Chiplet_Studio-x86_64.AppImage" >/tmp/ait.log 2>&1 \
        || { echo "appimagetool failed:"; tail -20 /tmp/ait.log; }
    [ -f "$WS/dist/Chiplet_Studio-x86_64.AppImage" ] && echo "    appimage: $(du -h "$WS/dist/Chiplet_Studio-x86_64.AppImage" | cut -f1)"
fi

chown -R $(stat -c %u:%g "$WS") "$APPDIR" "$WS/dist" 2>/dev/null || true
echo ">>> DONE"
'

echo ""
echo "=== Build complete ==="
ls -la "$PROJECT_DIR/dist/"
