# Packaging

Release packages are built by `.github/workflows/release.yml` (tag `v*`, or run it manually
with *workflow_dispatch* to get the packages as workflow artifacts without a release). Locally:

All platforms start from a Release build with the `release` preset (vcpkg OCCT, Qt 6.8 in
`CMAKE_PREFIX_PATH`, no tests):

```
cmake --preset release -DVCPKG_TARGET_TRIPLET=<triplet>
cmake --build --preset release
```

`cmake --install <build-dir> --prefix <dir>` installs the executable, the license texts
(`LICENSE`, `THIRD_PARTY_NOTICES.md`, `resources/licenses/*.txt`, `OFL.txt`) and, on Linux, the
desktop file, AppStream metainfo, icons and MIME definitions. The scripts below wrap that.

## Linux AppImage (x86_64)

```
SC_LIB_DIRS=$PWD/build/release/vcpkg_installed/x64-linux-dynamic/lib \
  packaging/linux/build-appimage.sh build/release dist
```

Output: `dist/StepComplete-<version>-x86_64.AppImage`. Needs `qmake` of the Qt to bundle in `PATH`
(or `QMAKE=`). Downloads pinned linuxdeploy / linuxdeploy-plugin-qt releases on first use. Build on
Ubuntu 22.04 (glibc 2.35) so the image runs on older distributions. Without FUSE:
`./StepComplete-*.AppImage --appimage-extract-and-run`.

## Windows (installer + portable zip)

From a shell with the MSVC environment (`vcvars64`) and Qt's `bin` in `PATH`; Inno Setup 6
must be installed (it is on GitHub's `windows-2022` runner):

```
packaging\windows\build-package.ps1 -BuildDir build\release -OutputDir dist
```

Output: `StepComplete-<version>-windows-x64-setup.exe` (per-user by default, no admin; Start menu
shortcut; optional default-handler association; "Open with" entries are always registered
without replacing existing defaults; the uninstaller removes everything) and
`StepComplete-<version>-windows-x64-portable.zip`. The Visual C++ runtime DLLs are copied
app-local (no vc_redist installer needed).

## macOS (DMG)

```
SC_ARCH=arm64 packaging/macos/build-dmg.sh build/release dist     # or SC_ARCH=x64
```

Output: `StepComplete-<version>-macos-arm64.dmg` / `-x64.dmg`. `SC_VCPKG_LIB_DIR` points at
`vcpkg_installed/<triplet>/lib` (auto-detected under the build directory). The Intel DMG is
cross-built on an Apple Silicon machine.

## Signing and notarization (all optional)

Without secrets everything is built **unsigned**, and the workflow still succeeds.

| Platform | Secrets (repository Actions secrets) | Effect |
|---|---|---|
| macOS | `MACOS_CERT_P12_BASE64`, `MACOS_CERT_PASSWORD` (Developer ID Application certificate, base64 of the .p12), `MACOS_SIGN_IDENTITY` (e.g. `Developer ID Application: Name (TEAMID)`) | Imported into a temporary keychain; app and DMG are signed with the hardened runtime |
| macOS | plus `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD` (app-specific password) | App and DMG are notarized (`notarytool --wait`) and stapled |
| Windows | `WINDOWS_CERT_PFX_BASE64` (base64 of the .pfx), `WINDOWS_CERT_PASSWORD` | `StepComplete.exe` and the installer are signed with `signtool` (SHA-256, timestamped) |

## What users see without signing

- **Windows:** SmartScreen shows "Windows protected your PC" for the installer and the exe.
  Choose *More info* then *Run anyway*. Reputation builds over time or with a signing certificate.
- **macOS:** the app is only ad-hoc signed (arm64 binaries must carry at least that to run).
  Gatekeeper blocks the first launch: right-click the app, choose *Open*, then *Open* again
  (or *System Settings > Privacy & Security > Open Anyway* on newer macOS).
- **Linux:** nothing is signed; make the AppImage executable (`chmod +x`) and run it.
