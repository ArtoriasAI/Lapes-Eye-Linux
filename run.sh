#!/usr/bin/env bash
# run.sh — Lape's Eye
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/build/lapes-eye"
DESKTOP_DIR="$HOME/.local/share/applications"
DESKTOP_DST="$DESKTOP_DIR/lapes-eye.desktop"

for arg in "$@"; do
    case "$arg" in
        --help|-h)
            echo "Użycie: ./run.sh [FOLDER] [--rebuild]"
            echo "  FOLDER     Katalog do otwarcia przy starcie"
            echo "  --rebuild  Wymuś pełną rekompilację (czyści build/)"
            exit 0 ;;
        --rebuild) REBUILD=1 ;;
        *) [[ -d "$arg" ]] && FOLDER_ARG="$arg" ;;
    esac
done

# Kompiluj jeśli --rebuild lub brak binarki
if [[ "${REBUILD:-0}" -eq 1 ]] || [[ ! -f "$BINARY" ]]; then
    echo "[run.sh] Kompiluję od zera (czyszczę stary build)..."
    rm -rf "$SCRIPT_DIR/build"
    mkdir -p "$SCRIPT_DIR/build"
    cd "$SCRIPT_DIR/build"
    cmake "$SCRIPT_DIR" -DCMAKE_BUILD_TYPE=Release --log-level=WARNING
    # cmake --build zamiast make — działa poprawnie ze spacjami i nawiasami w ścieżce
    cmake --build . --parallel "$(nproc)"
    cd "$SCRIPT_DIR"
    echo "[run.sh] Kompilacja zakończona."
fi

if [[ ! -f "$BINARY" ]]; then
    echo "Błąd: brak binarki $BINARY. Uruchom: ./build.sh"
    exit 1
fi

# Instaluj/aktualizuj plik .desktop z pełną ścieżką do binarki i ikony
mkdir -p "$DESKTOP_DIR"
ICON_PATH="$SCRIPT_DIR/resources/icons/lapes-eye-256.png"

# Zainstaluj ikony w ~/.local/share/icons/hicolor (wymagane przez KDE dla paska zadań)
for SIZE in 16 22 24 32 48 64 128 256 512; do
    ICON_SRC="$SCRIPT_DIR/resources/icons/lapes-eye-${SIZE}.png"
    ICON_DST="$HOME/.local/share/icons/hicolor/${SIZE}x${SIZE}/apps"
    if [ -f "$ICON_SRC" ]; then
        mkdir -p "$ICON_DST"
        cp "$ICON_SRC" "$ICON_DST/lapes-eye.png"
    fi
done
gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" 2>/dev/null || true

cat > "$DESKTOP_DST" << DESKTOP
[Desktop Entry]
Name=Lape's Eye
GenericName=Media Browser
Comment=Przeglądarka mediów dla Lape Photo Editor
Exec="$BINARY" %f
Icon=lapes-eye
Type=Application
Categories=Graphics;Photography;
MimeType=image/jpeg;image/png;image/tiff;image/webp;image/x-raw;
StartupNotify=true
StartupWMClass=lapes-eye
X-GNOME-UsesNotifications=false
DESKTOP

# Odśwież bazę danych desktopów
update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true

echo "[run.sh] Uruchamiam Lape's Eye..."
if [[ -n "${FOLDER_ARG:-}" ]]; then
    exec "$BINARY" "$FOLDER_ARG"
else
    exec "$BINARY"
fi
