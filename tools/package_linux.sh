#!/bin/bash
# Packs a Release build into dist/TRossSeq-<version>-Linux-x86_64.tar.gz (with an install script)
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION=$(sed -nE 's/^project\(TRossSeq VERSION ([0-9.]+)\).*/\1/p' CMakeLists.txt)
ART="${ART:-build/TRossSeq_artefacts/Release}"
NAME="TRossSeq-$VERSION-Linux-$(uname -m)"
STAGE="dist/$NAME"

rm -rf "$STAGE" && mkdir -p "$STAGE"
cp -R "$ART/VST3/TRoss Seq.vst3"   "$STAGE/"
cp -R "$ART/LV2/TRoss Seq.lv2"     "$STAGE/"

cat > "$STAGE/install.sh" <<'EOS'
#!/bin/bash
# Installs TRoss Seq for the current user (no root needed).
set -e
cd "$(dirname "$0")"
mkdir -p ~/.vst3 ~/.lv2
rm -rf ~/.vst3/"TRoss Seq.vst3" ~/.lv2/"TRoss Seq.lv2"
cp -R "TRoss Seq.vst3" ~/.vst3/
cp -R "TRoss Seq.lv2"  ~/.lv2/
echo "Installed: ~/.vst3/TRoss Seq.vst3, ~/.lv2/TRoss Seq.lv2"
echo "Rescan plug-ins in your DAW (Bitwig, Reaper, Ardour...)."
EOS

cat > "$STAGE/uninstall.sh" <<'EOS'
#!/bin/bash
rm -rf ~/.vst3/"TRoss Seq.vst3" ~/.lv2/"TRoss Seq.lv2"
echo "TRoss Seq removed."
EOS
chmod +x "$STAGE/install.sh" "$STAGE/uninstall.sh"

cat > "$STAGE/README.txt" <<EOS
TRoss Seq $VERSION - 16-step MIDI drum sequencer - Ticco Ross 2026

Install:    ./install.sh      (VST3 -> ~/.vst3, LV2 -> ~/.lv2)
Uninstall:  ./uninstall.sh
Needs: ALSA, X11, FreeType (standard on desktop distributions).
EOS

tar -C dist -czf "dist/$NAME.tar.gz" "$NAME"
rm -rf "$STAGE"
echo "==> Done: dist/$NAME.tar.gz"
