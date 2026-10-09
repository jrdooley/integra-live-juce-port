# 07 — macOS packaging and signing

The macOS CI job builds a **self-contained universal app** (Apple Silicon and Intel,
macOS 11 or later), signs it, packages it as `IntegraLive-macOS.dmg` and uploads that as
the `integra-live-macos-universal` artifact.

## Self-contained build

The earlier macOS job linked Homebrew's dylibs (`/opt/homebrew/...`), so the bundle only
ran on a Mac with the same Homebrew packages. Release builds now use vcpkg, as on Windows:

* `vcpkg.json` dependencies apply to `windows | osx`.
* `cmake/triplets/universal-osx-static.cmake` is an overlay triplet: static libraries,
  `VCPKG_OSX_ARCHITECTURES "arm64;x86_64"`, deployment target 11.0, release-only.
* With the vcpkg toolchain, `cmake/IntegraDependencies.cmake` skips pkg-config, so
  Homebrew copies can't be picked up by accident.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/triplets" \
  -DVCPKG_TARGET_TRIPLET=universal-osx-static \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build
packaging/macos/package.sh "build/app/IntegraLive_artefacts/Release/Integra Live.app" IntegraLive-macOS.dmg
```

A quick development build with Homebrew packages (see README) still works. Its app just
isn't redistributable.

CI checks the result with `lipo -verify_arch arm64 x86_64` and fails if `otool -L` shows
any library outside `/System/Library` and `/usr/lib`.

libsndfile is built without its external codecs (`default-features: false`), the same as on
Windows. WAV, AIFF and the other built-in formats work; FLAC, Ogg and MP3 do not. Enabling
the `external-libs` feature would add them at the cost of a longer dependency build.

## Bundle settings (app/CMakeLists.txt)

* `MICROPHONE_PERMISSION_ENABLED` and its text set `NSMicrophoneUsageDescription`.
  Without it, macOS terminates an app that opens audio input.
* `HARDENED_RUNTIME_ENABLED` with the `com.apple.security.device.audio-input` entitlement.
  The hardened runtime is required for notarisation, and audio input is blocked under it
  without the entitlement. `packaging/macos/IntegraLive.entitlements` holds the same
  entitlement for `codesign`.

## Signing and notarisation (packaging/macos/package.sh)

The script signs the app with the hardened runtime and entitlements, verifies the
signature, and builds a compressed DMG with an `Applications` link. With a real identity
it also signs the DMG and, if Apple ID credentials are present, notarises the DMG with
`notarytool` and staples the ticket.

| Repository secret | Used for |
|---|---|
| `MACOS_CERTIFICATE` | base64 of the "Developer ID Application" certificate + private key exported as `.p12` (`base64 -i cert.p12 \| pbcopy`) |
| `MACOS_CERTIFICATE_PASSWORD` | password of that `.p12` |
| `MACOS_SIGNING_IDENTITY` | e.g. `Developer ID Application: Your Name (TEAMID)` |
| `APPLE_ID` | Apple ID email of the developer account |
| `APPLE_TEAM_ID` | 10-character team ID |
| `APPLE_APP_PASSWORD` | app-specific password for that Apple ID (appleid.apple.com) |

These require a paid Apple Developer Program membership. **Without the secrets, the app is
ad-hoc signed.** It runs, but Gatekeeper blocks it the first time it's opened after
download:

* macOS 15 and later: open it once, then **System Settings → Privacy & Security → Open
  Anyway**.
* macOS 14 and earlier: Control-click the app → **Open** → **Open**.
* Or remove the quarantine flag: `xattr -dr com.apple.quarantine "/Applications/Integra Live.app"`.

The first time audio input is opened, macOS asks for microphone access.
