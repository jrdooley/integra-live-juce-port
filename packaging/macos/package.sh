#!/bin/bash
# Signs "Integra Live.app", wraps it in a DMG and (optionally) notarises it.
#
#   packaging/macos/package.sh <path/to/Integra Live.app> <output.dmg>
#
# Environment:
#   MACOS_SIGNING_IDENTITY  codesign identity, e.g. "Developer ID Application: Name (TEAMID)".
#                           Unset or "-": ad-hoc signature (runs locally; Gatekeeper will
#                           ask the user to approve it on first launch).
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD
#                           all three set (and a real identity): notarise with notarytool
#                           and staple the ticket to the DMG.
set -euo pipefail

app=$1
dmg=$2
here=$(cd "$(dirname "$0")" && pwd)
identity=${MACOS_SIGNING_IDENTITY:--}

# Hardened runtime + entitlements are required for notarisation; secure timestamps
# only exist for real identities.
sign_options=(--force --options runtime --entitlements "$here/IntegraLive.entitlements")
if [ "$identity" != "-" ]; then
    sign_options+=(--timestamp)
fi

echo "Signing $app with identity: $identity"
codesign "${sign_options[@]}" --sign "$identity" "$app"
codesign --verify --strict --verbose=2 "$app"

staging=$(mktemp -d)
trap 'rm -rf "$staging"' EXIT
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"
rm -f "$dmg"
hdiutil create -volname "Integra Live" -srcfolder "$staging" -fs HFS+ -format UDZO "$dmg"

if [ "$identity" != "-" ]; then
    codesign --force --timestamp --sign "$identity" "$dmg"
fi

if [ "$identity" != "-" ] && [ -n "${APPLE_ID:-}" ] && [ -n "${APPLE_TEAM_ID:-}" ] && [ -n "${APPLE_APP_PASSWORD:-}" ]; then
    echo "Notarising $dmg"
    xcrun notarytool submit "$dmg" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" --wait
    xcrun stapler staple "$dmg"
    spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
else
    echo "Not notarised (needs a Developer ID identity and APPLE_ID/APPLE_TEAM_ID/APPLE_APP_PASSWORD)"
fi

echo "Created $dmg"
