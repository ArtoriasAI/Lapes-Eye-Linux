#!/usr/bin/env bash
# build.sh — Lape's Eye
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Lape's Eye — budowanie ==="

echo "[1/4] Sprawdzam zależności..."
sudo pacman -S --needed --noconfirm \
    cmake qt6-base libraw exiv2 pkgconf base-devel

# WAŻNE: usuń stary build — CMake cache może być zdezaktualizowany
echo "[2/4] Czyszczę katalog build..."
rm -rf "$SCRIPT_DIR/build"
mkdir -p "$SCRIPT_DIR/build"
cd "$SCRIPT_DIR/build"

echo "[3/4] CMake configure..."
cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "[4/4] Kompilacja ($(nproc) wątki)..."
make -j"$(nproc)"

echo ""
echo "✓ Gotowe! Uruchom: ./run.sh"
