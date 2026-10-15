#!/bin/bash
# Builds dist/TRossSeq-<version>.pkg from an existing Release build.
#
#   ./tools/make_pkg.sh                       unsigned installer (free, recipients get a Gatekeeper warning)
#
#   DEV_ID_APP="Developer ID Application: Your Name (TEAMID)" \
#   DEV_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)" \
#   NOTARY_PROFILE="trossseq-notary" \
#   ./tools/make_pkg.sh                       signed + notarized (needs an Apple Developer account)
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -nE 's/^project\(TRossSeq VERSION ([0-9.]+)\).*/\1/p' CMakeLists.txt)
ART="build/TRossSeq_artefacts/Release"
OUT="dist"
WORK="$OUT/work"
ID="com.ticcoross.trossseq"
DEV_ID_APP="${DEV_ID_APP:-}"
DEV_ID_INSTALLER="${DEV_ID_INSTALLER:-}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"

for f in "$ART/AU/TRoss Seq.component" "$ART/VST3/TRoss Seq.vst3"; do
    [ -e "$f" ] || { echo "Missing $f - run the Release build first."; exit 1; }
done

echo "==> Packaging TRoss Seq $VERSION"
rm -rf "$OUT"
mkdir -p "$WORK/resources"

# Sign the finished bundles in the build folder (so the zips made from them are signed too).
# The build adds files to the VST3 after linking (moduleinfo.json), which breaks the linker's
# automatic signature; macOS then refuses to load it and hosts like Live skip the VST3.
# Without a Developer ID they get an ad-hoc signature ("-"), which Macs accept for local plug-ins.
for f in "$ART/AU/TRoss Seq.component" "$ART/VST3/TRoss Seq.vst3"; do
    if [ -n "$DEV_ID_APP" ]; then
        echo "==> Signing $(basename "$f") with $DEV_ID_APP"
        codesign --force --deep --options runtime --timestamp --sign "$DEV_ID_APP" "$f"
    else
        echo "==> Ad-hoc signing $(basename "$f")"
        codesign --force --deep --sign - "$f"
    fi
    codesign --verify --deep --strict --verbose=2 "$f"
done

# name | source | install folder | package id suffix
stage() {
    local name="$1" src="$2" dest="$3"
    local root="$WORK/$name/root"
    mkdir -p "$root$dest"
    ditto "$src" "$root$dest/$(basename "$src")"

    # stop Installer from "relocating" the bundle to wherever an old copy happens to be
    pkgbuild --analyze --root "$root" "$WORK/$name.plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$name.plist"

    pkgbuild --root "$root" --component-plist "$WORK/$name.plist" \
             --identifier "$ID.$name" --version "$VERSION" --install-location / \
             "$WORK/$name.pkg" >/dev/null
}

stage au   "$ART/AU/TRoss Seq.component"   "/Library/Audio/Plug-Ins/Components"
stage vst3 "$ART/VST3/TRoss Seq.vst3"      "/Library/Audio/Plug-Ins/VST3"

cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family:-apple-system,Helvetica;font-size:13px">
<h2>TRoss Seq $VERSION</h2>
<p>16-step MIDI drum sequencer &middot; Ticco Ross 2026</p>
<p>This installs the Audio Unit and the VST3 plug-in.
Click <b>Customize</b> on the next screen to skip either of them.</p>
<p>After installing, rescan plug-ins in your DAW
(Ableton Live: Settings &rarr; Plug-Ins, hold Option and click Rescan).</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>TRoss Seq $VERSION</title>
    <organization>com.ticcoross</organization>
    <welcome file="welcome.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
    </choices-outline>
    <choice id="au" title="Audio Unit" description="TRoss Seq.component for Logic, Ableton Live, GarageBand...">
        <pkg-ref id="$ID.au"/>
    </choice>
    <choice id="vst3" title="VST3" description="TRoss Seq.vst3 for VST3 hosts">
        <pkg-ref id="$ID.vst3"/>
    </choice>
    <pkg-ref id="$ID.au"   version="$VERSION" onConclusion="none">au.pkg</pkg-ref>
    <pkg-ref id="$ID.vst3" version="$VERSION" onConclusion="none">vst3.pkg</pkg-ref>
</installer-gui-script>
EOF

PKG="$OUT/TRossSeq-$VERSION.pkg"
SIGN_ARGS=()
[ -n "$DEV_ID_INSTALLER" ] && SIGN_ARGS=(--sign "$DEV_ID_INSTALLER" --timestamp)

productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK" "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" "$PKG"

if [ -n "$NOTARY_PROFILE" ]; then
    [ -n "$DEV_ID_INSTALLER" ] || { echo "Notarizing needs DEV_ID_INSTALLER as well."; exit 1; }
    echo "==> Notarizing (this can take a few minutes)"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

rm -rf "$WORK"
echo "==> Done: $PKG"
[ -z "$DEV_ID_INSTALLER" ] && echo "    (unsigned: other Macs will show a Gatekeeper warning on first open)"
exit 0
