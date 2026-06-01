#!/usr/bin/env bash
# debug.sh — uruchamia Lape's Eye z pełnym logowaniem crashy
# Użycie: ./debug.sh [FOLDER]

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/build/lapes-eye"
LOG="$SCRIPT_DIR/crash.log"

# ── Sprawdź czy jest debug build ────────────────────────────────────────────
if [[ ! -f "$BINARY" ]]; then
    echo "Brak binarki. Uruchom najpierw: ./run.sh --rebuild"
    exit 1
fi

# ── Kompiluj wersję debug jeśli nie istnieje ─────────────────────────────────
DEBUG_BINARY="$SCRIPT_DIR/build-debug/lapes-eye"
if [[ ! -f "$DEBUG_BINARY" ]]; then
    echo "[debug.sh] Kompiluję wersję debug (z symbolami)..."
    rm -rf "$SCRIPT_DIR/build-debug"
    mkdir -p "$SCRIPT_DIR/build-debug"
    cd "$SCRIPT_DIR/build-debug"
    cmake "$SCRIPT_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-g -O0 -fsanitize=address,undefined" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    make -j"$(nproc)"
    cd "$SCRIPT_DIR"
    echo "[debug.sh] Wersja debug gotowa."
fi

echo "[debug.sh] Uruchamiam z AddressSanitizer i logowaniem..."
echo "[debug.sh] Log crashy: $LOG"
echo "[debug.sh] Zamknij program normalnie lub poczekaj na crash."
echo ""

# Zmienne środowiskowe dla lepszych komunikatów błędów
export ASAN_OPTIONS="abort_on_error=0:print_stacktrace=1:detect_leaks=0"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"
export QT_FATAL_WARNINGS=0
export QT_LOGGING_RULES="*.debug=true"

# Uruchom i złap output
"$DEBUG_BINARY" "${1:-}" 2>&1 | tee "$LOG"

echo ""
echo "[debug.sh] Program zakończył działanie."
echo "[debug.sh] Log zapisany w: $LOG"
echo "[debug.sh] Wyślij plik crash.log do analizy."
