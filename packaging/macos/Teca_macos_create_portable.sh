#!/bin/bash
# Portable Teca for macOS (fork, 2026-10-30): a self-contained Teca.app and a
# DMG, built on this Mac from a Release build of qt_quick.
#
# The app carries Qt, KF6 and OpenCV (macdeployqt), the programs the AI
# analysis runs (ffmpeg, whisper-cli, poppler, tesseract, exiv2) and the
# models found on this Mac (speech, voice activity, faces, OCR languages).
# Ollama stays a separate program: the app reaches it at localhost.
#
# Usage: packaging/macos/Teca_macos_create_portable.sh [--no-speech-model]
#   --no-speech-model  leave out the speech model (about 550 MB); Teca then
#                      looks for one in ~/whisper-models
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/qt_quick/build/Release"
OUT="$ROOT/qt_quick/build/Portable"
KF6="$HOME/kf6"
SPEECH_MODEL=yes
[ "${1:-}" = "--no-speech-model" ] && SPEECH_MODEL=no

VERSION=$(sed -n 's/^set(KATALOG_VERSION_STRING "\(.*\)")/\1/p' "$ROOT/qt_quick/CMakeLists.txt")
STAMP=$(date +%Y%m%d)

echo "== Release build"
cmake -S "$ROOT" -B "$BUILD" -GNinja -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_QUICK=ON -DBUILD_QT_WIDGETS=OFF \
      -DBUILD_TESTS=OFF -DCMAKE_PREFIX_PATH="$KF6;/opt/homebrew" > /dev/null
ninja -C "$BUILD" > /dev/null

echo "== Qt, KF6 and OpenCV (macdeployqt)"
rm -rf "$OUT" && mkdir -p "$OUT"
cp -R "$BUILD/qt_quick/Teca.app" "$OUT/"
APP="$OUT/Teca.app"
/opt/homebrew/bin/macdeployqt "$APP" -qmldir="$ROOT/qt_quick" -qmlimport="$KF6/lib/qml" > "$OUT/macdeployqt.log" 2>&1

# Kirigami's Fusion theme (as in .github/workflows/Katalog_3_Build_macOS.yml)
KIRIGAMI_DIR="$APP/Contents/Resources/qml/org/kde/kirigami"
for style in Fusion TecaStyle; do
    mkdir -p "$KIRIGAMI_DIR/styles/$style"
    cp "$ROOT/packaging/Theme.qml" "$KIRIGAMI_DIR/styles/$style/Theme.qml"
done

echo "== Programs, models, references and signature"
MODELS=()
for m in "$HOME"/whisper-models/ggml-silero-*.bin "$HOME"/teca-models/face_*.onnx "$HOME/teca-models/tessdata"; do
    [ -e "$m" ] && MODELS+=("$m")
done
if [ "$SPEECH_MODEL" = yes ]; then
    for m in "$HOME"/whisper-models/ggml-*.bin; do
        case "$(basename "$m")" in ggml-silero-*) ;; *) [ -e "$m" ] && MODELS+=("$m") ;; esac
    done
fi
python3 "$ROOT/packaging/macos/bundle_tools.py" "$APP" \
    --tools ffmpeg ffprobe whisper-cli pdftotext pdftoppm tesseract exiv2 \
    --backends /opt/homebrew/opt/ggml/libexec \
    --models "${MODELS[@]}"
codesign --verify --deep --strict "$APP"

echo "== DMG"
STAGE="$OUT/dmg"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
DMG="$OUT/Teca-$VERSION-macOS-arm64-$STAMP.dmg"
hdiutil create -volname "Teca" -srcfolder "$STAGE" -ov -format ULMO "$DMG" > /dev/null
rm -rf "$STAGE"
du -sh "$APP" "$DMG"
