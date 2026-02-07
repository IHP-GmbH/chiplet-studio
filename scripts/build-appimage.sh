#!/bin/bash
# build-appimage.sh - Build a portable AppImage for Chiplet Studio
#
# This script builds the application inside Docker and creates an AppImage
# that can run on any Linux system without additional dependencies.
#
# Usage: ./scripts/build-appimage.sh
# Output: dist/Chiplet_Studio-x86_64.AppImage

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== Building Chiplet Studio AppImage ==="

# Ensure dist directory exists
mkdir -p "$PROJECT_DIR/dist"

# Build the AppImage inside Docker
docker run --rm \
    -v "$PROJECT_DIR:/workspace" \
    --user root \
    chiplet-studio-build \
    bash -c '
set -e

echo ">>> Installing AppImage tools..."
apt-get update -qq
apt-get install -y -qq wget file libfuse2 patchelf > /dev/null

# Download linuxdeploy and appimagetool
cd /tmp
wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
chmod +x linuxdeploy-x86_64.AppImage linuxdeploy-plugin-qt-x86_64.AppImage

# Extract AppImages (needed inside Docker - FUSE not available)
./linuxdeploy-x86_64.AppImage --appimage-extract > /dev/null
mv squashfs-root linuxdeploy
./linuxdeploy-plugin-qt-x86_64.AppImage --appimage-extract > /dev/null
mv squashfs-root linuxdeploy-plugin-qt

echo ">>> Creating AppDir structure..."
cd /workspace
rm -rf AppDir
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/lib
mkdir -p AppDir/usr/share/applications
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps
mkdir -p AppDir/usr/plugins/platforms

# Copy main binary
cp build/chiplet-studio AppDir/usr/bin/

# Copy KLayout libraries
cp -a extern/klayout/bin-release/*.so* AppDir/usr/lib/ 2>/dev/null || true
cp -a extern/klayout/bin-release/db_plugins/*.so* AppDir/usr/lib/ 2>/dev/null || true
cp -a extern/klayout/bin-release/lay_plugins/*.so* AppDir/usr/lib/ 2>/dev/null || true

# Copy yaml-cpp from system
cp -a /usr/lib/x86_64-linux-gnu/libyaml-cpp.so* AppDir/usr/lib/

echo ">>> Creating desktop file..."
cat > AppDir/usr/share/applications/chiplet-studio.desktop << EOF
[Desktop Entry]
Type=Application
Name=Chiplet Studio
Comment=3D Chiplet Assembly Design Tool
Exec=chiplet-studio
Icon=chiplet-studio
Categories=Development;Engineering;
Terminal=false
EOF

# Copy desktop file to root (required by AppImage)
cp AppDir/usr/share/applications/chiplet-studio.desktop AppDir/

echo ">>> Creating icon..."
# Create a simple SVG icon
cat > AppDir/usr/share/icons/hicolor/256x256/apps/chiplet-studio.svg << EOF
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">
  <rect x="20" y="20" width="216" height="216" rx="20" fill="#2563eb"/>
  <rect x="50" y="80" width="60" height="40" fill="#fff"/>
  <rect x="146" y="80" width="60" height="40" fill="#fff"/>
  <rect x="50" y="136" width="60" height="40" fill="#fff"/>
  <rect x="146" y="136" width="60" height="40" fill="#fff"/>
  <rect x="98" y="60" width="60" height="136" fill="#60a5fa" opacity="0.5"/>
</svg>
EOF
cp AppDir/usr/share/icons/hicolor/256x256/apps/chiplet-studio.svg AppDir/chiplet-studio.svg

echo ">>> Creating AppRun script..."
cat > AppDir/AppRun << "APPRUN"
#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${HERE}/usr/plugins:${QT_PLUGIN_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS}"
exec "${HERE}/usr/bin/chiplet-studio" "$@"
APPRUN
chmod +x AppDir/AppRun

echo ">>> Running linuxdeploy to bundle dependencies..."
export QMAKE=/usr/bin/qmake6
export PATH="/tmp/linuxdeploy-plugin-qt/usr/bin:$PATH"
export LD_LIBRARY_PATH="/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins:$LD_LIBRARY_PATH"

# Use linuxdeploy to collect Qt and system dependencies
/tmp/linuxdeploy/usr/bin/linuxdeploy \
    --appdir AppDir \
    --executable build/chiplet-studio \
    --desktop-file AppDir/chiplet-studio.desktop \
    --icon-file AppDir/chiplet-studio.svg \
    --plugin qt \
    --output appimage 2>&1 || {
        echo "linuxdeploy failed, trying manual packaging..."

        # Download appimagetool directly
        wget -q https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O /tmp/appimagetool
        chmod +x /tmp/appimagetool
        /tmp/appimagetool --appimage-extract > /dev/null
        mv squashfs-root /tmp/appimagetool-extracted

        # Create AppImage manually
        ARCH=x86_64 /tmp/appimagetool-extracted/AppRun AppDir dist/Chiplet_Studio-x86_64.AppImage
    }

# Move result to dist
mv Chiplet_Studio*.AppImage dist/ 2>/dev/null || true

echo ">>> AppImage created!"
ls -la dist/*.AppImage 2>/dev/null || echo "AppImage may be in workspace root"
'

# Fix permissions
chmod +x "$PROJECT_DIR/dist"/*.AppImage 2>/dev/null || true

echo ""
echo "=== Build complete ==="
ls -la "$PROJECT_DIR/dist/"*.AppImage 2>/dev/null && echo "Run with: ./dist/Chiplet_Studio-x86_64.AppImage"
