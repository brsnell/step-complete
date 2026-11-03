#!/usr/bin/env bash
# Build StepComplete-<version>-x86_64.AppImage from an existing CMake build tree.
#
#   packaging/linux/build-appimage.sh <build-dir> <output-dir>
#
# Environment:
#   SC_LIB_DIRS       colon-separated extra library dirs (e.g. vcpkg_installed/x64-linux-dynamic/lib)
#                     so linuxdeploy can resolve and bundle OCCT.
#   QMAKE             qmake of the Qt to bundle (default: qmake6 / qmake from PATH).
#   LINUXDEPLOY_DIR   where the linuxdeploy AppImages are cached (default: <output-dir>/.work/tools).
set -euo pipefail

build_dir=$(realpath "${1:?usage: build-appimage.sh <build-dir> <output-dir>}")
mkdir -p "${2:?usage: build-appimage.sh <build-dir> <output-dir>}"
out_dir=$(realpath "$2")
work_dir=$out_dir/.work
tools_dir=${LINUXDEPLOY_DIR:-$work_dir/tools}

# Pinned release tags (immutable), not the moving "continuous" builds.
LINUXDEPLOY_TAG=1-alpha-20250213-2
PLUGIN_QT_TAG=1-alpha-20250213-1
base=https://github.com/linuxdeploy

mkdir -p "$tools_dir"
fetch() { # url file
  if [ ! -x "$tools_dir/$2" ]; then
    curl -fsSL --retry 3 -o "$tools_dir/$2.part" "$1"
    chmod +x "$tools_dir/$2.part"
    mv "$tools_dir/$2.part" "$tools_dir/$2"
  fi
}
fetch "$base/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" \
  linuxdeploy-x86_64.AppImage
fetch "$base/linuxdeploy-plugin-qt/releases/download/$PLUGIN_QT_TAG/linuxdeploy-plugin-qt-x86_64.AppImage" \
  linuxdeploy-plugin-qt-x86_64.AppImage

# Containers and CI runners may lack FUSE: run the AppImage tools extracted.
export APPIMAGE_EXTRACT_AND_RUN=1
# linuxdeploy finds plugins named linuxdeploy-plugin-*.AppImage in PATH / next to itself.
export PATH="$tools_dir:$PATH"

if [ -z "${QMAKE:-}" ]; then
  QMAKE=$(command -v qmake6 || command -v qmake) || { echo "qmake not found; set QMAKE" >&2; exit 1; }
fi
export QMAKE
export LD_LIBRARY_PATH="${SC_LIB_DIRS:+$SC_LIB_DIRS:}${LD_LIBRARY_PATH:-}"

version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")
[ -n "$version" ] || { echo "cannot determine version from $build_dir/CMakeCache.txt" >&2; exit 1; }

appdir=$work_dir/AppDir
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr

export DEPLOY_PLATFORM_THEMES=0
export NO_STRIP=1 # old strip in linuxdeploy chokes on newer toolchains' .relr sections
export LINUXDEPLOY_OUTPUT_VERSION="$version"
cd "$work_dir"
rm -f ./*.AppImage

# Pass 1: bundle libraries (OCCT, Qt, ...) and Qt plugins.
linuxdeploy="$tools_dir/linuxdeploy-x86_64.AppImage"
"$linuxdeploy" --appdir "$appdir" \
  --executable "$appdir/usr/bin/StepComplete" \
  --desktop-file "$appdir/usr/share/applications/org.stepcomplete.StepComplete.desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/stepcomplete.png" \
  --plugin qt

# Prune what the app never loads: it needs the xcb platform, its GL integration and
# JPEG export (PNG is built into QtGui).
if [ -d "$appdir/usr/plugins" ]; then
  for d in iconengines tls networkinformation generic printsupport sqldrivers styles \
           platformthemes platforminputcontexts qmltooling position multimedia; do
    rm -rf "${appdir:?}/usr/plugins/$d"
  done
  find "$appdir/usr/plugins/imageformats" -type f ! -name 'libqjpeg.so' -delete 2>/dev/null || true
fi
rm -f "$appdir"/usr/lib/libQt6Svg*.so* "$appdir"/usr/lib/libQt6Network*.so* \
      "$appdir"/usr/lib/libQt6Qml*.so* "$appdir"/usr/lib/libQt6Quick*.so* \
      "$appdir"/usr/lib/libQt6PrintSupport*.so*
rm -rf "$appdir/usr/translations"

# Pass 2: pack (no Qt plugin, so nothing pruned is re-added).
"$linuxdeploy" --appdir "$appdir" --output appimage

# linuxdeploy names the file after the desktop entry ("STEP_Complete-..."): use a stable name.
final="StepComplete-$version-x86_64.AppImage"
mv -f ./*-x86_64.AppImage "$out_dir/$final"
rm -rf "$appdir"
ls -la "$out_dir/$final"
