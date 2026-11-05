#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD="${MEXNYA_BUILD_DIR:-build/release}"
DIST="${MEXNYA_DIST_DIR:-dist}"
IDENTITY="${MEXNYA_CODESIGN_ID:--}"
VERSION="$(sed -n 's/^project(MEXNya VERSION \([^ ]*\) .*/\1/p' CMakeLists.txt)"
VERSION="${VERSION:-0.1.0}"

if [ ! -x "$BUILD/MexnyaEditor" ] || [ ! -x "$BUILD/samples/hello/hello" ]; then
    cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD" -j"$(sysctl -n hw.ncpu)"
fi

bundle() {
    local app="$1" exe="$2" name="$3" id="$4"
    mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
    cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$name</string>
    <key>CFBundleDisplayName</key><string>$name</string>
    <key>CFBundleIdentifier</key><string>$id</string>
    <key>CFBundleExecutable</key><string>$exe</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
    codesign --force --deep --sign "$IDENTITY" "$app"
    codesign --verify --deep --strict "$app"
}

rm -rf "$DIST/MEXNya.app" "$DIST/hello.app"
mkdir -p "$DIST"

APP="$DIST/MEXNya.app"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BUILD/MexnyaEditor" "$APP/Contents/MacOS/MexnyaEditor"
cp -R engine/resources "$APP/Contents/Resources/engine-resources"
bundle "$APP" "MexnyaEditor" "MEXNya Editor" "dev.mexnya.editor"

HELLO="$DIST/hello.app"
mkdir -p "$HELLO/Contents/MacOS" "$HELLO/Contents/Resources"
cp "$BUILD/samples/hello/hello" "$HELLO/Contents/MacOS/hello"
cp -R "$BUILD/samples/hello/engine-resources" "$HELLO/Contents/Resources/engine-resources"
cp -R "$BUILD/samples/hello/assets" "$HELLO/Contents/Resources/assets"
cp -R "$BUILD/samples/hello/mexnya" "$HELLO/Contents/Resources/mexnya"
bundle "$HELLO" "hello" "hello" "dev.mexnya.hello"

echo "packaged $APP"
echo "packaged $HELLO"
