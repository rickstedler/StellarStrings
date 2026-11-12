#!/bin/bash
# Baut "StellarStrings-<version>.pkg" aus den fertigen Release-Bundles.
# Aufruf (aus dem Projektordner, nach dem Release-Build):  installer/make_installer.sh 1.0.0
set -euo pipefail

VERSION="${1:-1.0.0}"
ARTEFACTS="build/StellarStrings_artefacts/Release"
AU="$ARTEFACTS/AU/Stellar Strings.component"
VST3="$ARTEFACTS/VST3/Stellar Strings.vst3"
WORK="build/installer"
OUT="${2:-StellarStrings-$VERSION.pkg}"

rm -rf "$WORK"
mkdir -p "$WORK/au/Library/Audio/Plug-Ins/Components" \
         "$WORK/vst3/Library/Audio/Plug-Ins/VST3" \
         "$WORK/scripts" "$WORK/pkgs"

ditto "$AU"   "$WORK/au/Library/Audio/Plug-Ins/Components/Stellar Strings.component"
ditto "$VST3" "$WORK/vst3/Library/Audio/Plug-Ins/VST3/Stellar Strings.vst3"

# Ad-hoc-Signatur (nötig auf Apple Silicon, falls keine Developer-ID vorhanden ist)
codesign --force --deep --sign - "$WORK/au/Library/Audio/Plug-Ins/Components/Stellar Strings.component"
codesign --force --deep --sign - "$WORK/vst3/Library/Audio/Plug-Ins/VST3/Stellar Strings.vst3"

# Nach der Installation den AU-Cache neu aufbauen lassen
cat > "$WORK/scripts/postinstall" <<'POST'
#!/bin/bash
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
POST
chmod +x "$WORK/scripts/postinstall"

build_component () {   # $1 = name, $2 = identifier, $3 = mit Skript?
    local root="$WORK/$1" plist="$WORK/$1.plist"
    pkgbuild --analyze --root "$root" "$plist"
    # Nicht relocatable: immer nach /Library/Audio/Plug-Ins installieren
    plutil -replace 0.BundleIsRelocatable -bool NO "$plist"
    local extra=()
    [ "${3:-}" = "scripts" ] && extra=(--scripts "$WORK/scripts")
    pkgbuild --root "$root" --component-plist "$plist" --identifier "$2" \
             --version "$VERSION" --install-location / ${extra[@]+"${extra[@]}"} "$WORK/pkgs/$1.pkg"
}

build_component au   com.stellaraudio.stellarstrings.au scripts
build_component vst3 com.stellaraudio.stellarstrings.vst3

cat > "$WORK/distribution.xml" <<DIST
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Stellar Strings $VERSION</title>
    <welcome mime-type="text/plain"><![CDATA[Stellar Strings – Retro-String-Ensemble mit Stereo-Ensemble-Chorus, Ping-Pong-Delay und Space Reverb.

Installiert das Audio-Unit-Plugin (Logic Pro, GarageBand, MainStage …) und optional das VST3-Plugin nach /Library/Audio/Plug-Ins.]]></welcome>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="au"/>
        <line choice="vst3"/>
    </choices-outline>
    <choice id="au" title="Audio Unit (AU)" description="Für Logic Pro, GarageBand, MainStage und andere AU-Hosts.">
        <pkg-ref id="com.stellaraudio.stellarstrings.au"/>
    </choice>
    <choice id="vst3" title="VST3" description="Für Ableton Live, Cubase, Bitwig, Reaper und andere VST3-Hosts.">
        <pkg-ref id="com.stellaraudio.stellarstrings.vst3"/>
    </choice>
    <pkg-ref id="com.stellaraudio.stellarstrings.au" version="$VERSION">au.pkg</pkg-ref>
    <pkg-ref id="com.stellaraudio.stellarstrings.vst3" version="$VERSION">vst3.pkg</pkg-ref>
</installer-gui-script>
DIST

productbuild --distribution "$WORK/distribution.xml" --package-path "$WORK/pkgs" "$OUT"
echo "Installer erstellt: $OUT"
