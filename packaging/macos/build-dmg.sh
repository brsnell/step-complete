#!/usr/bin/env bash
# Build StepComplete-<version>-macos-<arch>.dmg from an existing CMake build tree.
#
#   packaging/macos/build-dmg.sh <build-dir> <output-dir>
#
# Environment:
#   SC_ARCH                 arm64 | x64 (default: derived from CMAKE_OSX_ARCHITECTURES, else uname -m)
#   SC_VCPKG_LIB_DIR        OCCT/FreeType dylib dir, <vcpkg_installed>/<triplet>/lib (default: auto-detect
#                           from <build-dir>/vcpkg_installed/*/lib)
#   MACDEPLOYQT             macdeployqt to use (default: from PATH)
# Signing (all optional; without MACOS_SIGN_IDENTITY the app is ad-hoc signed, which arm64 needs to run):
#   MACOS_SIGN_IDENTITY     e.g. "Developer ID Application: Name (TEAMID)"
# Notarization (only with MACOS_SIGN_IDENTITY and all three of):
#   MACOS_NOTARY_APPLE_ID, MACOS_NOTARY_TEAM_ID, MACOS_NOTARY_PASSWORD (app-specific password)
set -euo pipefail

build_dir=$(cd "${1:?usage: build-dmg.sh <build-dir> <output-dir>}" && pwd)
mkdir -p "${2:?usage: build-dmg.sh <build-dir> <output-dir>}"
out_dir=$(cd "$2" && pwd)

version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")
[ -n "$version" ] || { echo "cannot determine version from CMakeCache.txt" >&2; exit 1; }

arch=${SC_ARCH:-}
if [ -z "$arch" ]; then
  cmake_arch=$(sed -n 's/^CMAKE_OSX_ARCHITECTURES:[A-Z]*=//p' "$build_dir/CMakeCache.txt")
  case "${cmake_arch:-$(uname -m)}" in
    arm64) arch=arm64 ;;
    x86_64) arch=x64 ;;
    *) echo "unsupported architecture: ${cmake_arch:-$(uname -m)}" >&2; exit 1 ;;
  esac
fi

staging="$out_dir/staging-$arch"
rm -rf "$staging"
mkdir -p "$staging"
cmake --install "$build_dir" --prefix "$staging"
app="$staging/StepComplete.app"
[ -d "$app" ] || { echo "install did not produce $app" >&2; exit 1; }

libdir=${SC_VCPKG_LIB_DIR:-}
if [ -z "$libdir" ]; then
  libdir=$(find "$build_dir/vcpkg_installed" -maxdepth 2 -type d -name lib 2>/dev/null | grep -v debug | head -1 || true)
fi
libpath_args=()
if [ -n "$libdir" ]; then
  echo "Bundling OCCT from $libdir"
  libpath_args=("-libpath=$libdir")
fi

# Copy Qt frameworks/plugins and everything linked through @rpath into the bundle.
# Without -dmg: we build the disk image ourselves. Without -codesign: signing is done below.
"${MACDEPLOYQT:-macdeployqt}" "$app" "${libpath_args[@]}" -verbose=1

# Drop Qt plugins we never use (macdeployqt has no exclusion option) and the frameworks only
# they pulled in: the app reads JPEG/PNG at most and draws its own icons.
plugins="$app/Contents/PlugIns"
find "$plugins/imageformats" -type f ! -name 'libqjpeg*' -delete 2>/dev/null || true
rm -rf "$plugins/iconengines" "$plugins/virtualkeyboard" "$plugins/platforminputcontexts"
for fw in QtSvg QtPdf QtVirtualKeyboard QtQuick QtQml QtQmlModels QtNetwork; do
  rm -rf "$app/Contents/Frameworks/$fw.framework"
done

# Qt's official macOS binaries are universal (arm64 + x86_64); each DMG targets one architecture,
# so keep only that slice. This roughly halves the Qt frameworks.
lipo_arch=$([ "$arch" = x64 ] && echo x86_64 || echo arm64)
while IFS= read -r f; do
  archs=$(lipo -archs "$f" 2>/dev/null || true)
  case "$archs" in
    *" "*) lipo -thin "$lipo_arch" -output "$f.thin" "$f" && mv "$f.thin" "$f" ;;
  esac
done < <(find "$app/Contents/MacOS" "$app/Contents/Frameworks" "$app/Contents/PlugIns" -type f \
           ! -name '*.plist' ! -name '*.prl' ! -path '*/Resources/*' ! -path '*/Headers/*')

# OCCT is not a Qt dependency: make sure its dylibs really ended up in the bundle.
if ! ls "$app"/Contents/Frameworks/libTKernel*.dylib >/dev/null 2>&1; then
  echo "macdeployqt did not bundle OCCT; copying from $libdir" >&2
  [ -n "$libdir" ] || { echo "set SC_VCPKG_LIB_DIR" >&2; exit 1; }
  mkdir -p "$app/Contents/Frameworks"
  cp -a "$libdir"/libTK*.dylib "$libdir"/libfreetype*.dylib "$app/Contents/Frameworks/" 2>/dev/null || true
  exe="$app/Contents/MacOS/StepComplete"
  for f in "$app"/Contents/Frameworks/libTK*.dylib; do
    install_name_tool -id "@rpath/$(basename "$f")" "$f"
  done
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$exe" 2>/dev/null || true
fi

# Sign. Ad-hoc ("-") when no identity is configured; hardened runtime + timestamp for Developer ID.
if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
  echo "Signing with Developer ID identity"
  codesign --force --deep --timestamp --options runtime -s "$MACOS_SIGN_IDENTITY" "$app"
else
  echo "No MACOS_SIGN_IDENTITY: ad-hoc signing (Gatekeeper will require right-click > Open)"
  codesign --force --deep -s - "$app"
fi
codesign --verify --deep --strict "$app"

notarize=0
if [ -n "${MACOS_SIGN_IDENTITY:-}" ] && [ -n "${MACOS_NOTARY_APPLE_ID:-}" ] \
   && [ -n "${MACOS_NOTARY_TEAM_ID:-}" ] && [ -n "${MACOS_NOTARY_PASSWORD:-}" ]; then
  notarize=1
fi

notarize_submit() { # file
  xcrun notarytool submit "$1" --wait \
    --apple-id "$MACOS_NOTARY_APPLE_ID" --team-id "$MACOS_NOTARY_TEAM_ID" \
    --password "$MACOS_NOTARY_PASSWORD"
}

if [ "$notarize" = 1 ]; then
  zip="$staging/notarize.zip"
  ditto -c -k --keepParent "$app" "$zip"
  notarize_submit "$zip"
  rm -f "$zip"
  xcrun stapler staple "$app"
else
  echo "Notarization skipped (credentials not set)"
fi

# DMG: app + /Applications symlink.
ln -sfn /Applications "$staging/Applications"
dmg="$out_dir/StepComplete-$version-macos-$arch.dmg"
rm -f "$dmg"
hdiutil create -volname "STEP Complete" -srcfolder "$staging" -ov -format UDZO "$dmg"

if [ -n "${MACOS_SIGN_IDENTITY:-}" ]; then
  codesign --force --timestamp -s "$MACOS_SIGN_IDENTITY" "$dmg"
  if [ "$notarize" = 1 ]; then
    notarize_submit "$dmg"
    xcrun stapler staple "$dmg"
  fi
fi

rm -rf "$staging"
ls -la "$dmg"
