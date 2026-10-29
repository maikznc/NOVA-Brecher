#!/bin/bash
# NOVA BRECHER - build on your own Mac (VST3 + AU, Apple Silicon + Intel)
# Usage: open Terminal, drag this file in, press Enter.
set -e
cd "$(dirname "$0")"

echo "== NOVA BRECHER build =="

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode Command Line Tools fehlen. Installation startet - danach dieses Script nochmal starten."
    xcode-select --install
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    if command -v brew >/dev/null 2>&1; then
        echo "CMake wird installiert ..."
        brew install cmake
    else
        echo "CMake fehlt. Entweder Homebrew installieren (https://brew.sh) und 'brew install cmake',"
        echo "oder CMake von https://cmake.org/download herunterladen."
        exit 1
    fi
fi

cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DBM_COPY_PLUGIN=ON
cmake --build build --config Release -j "$(sysctl -n hw.ncpu)"

# make macOS rescan the Audio Units
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo ""
echo "Fertig. Installiert nach:"
echo "  ~/Library/Audio/Plug-Ins/VST3/NOVA BRECHER.vst3"
echo "  ~/Library/Audio/Plug-Ins/Components/NOVA BRECHER.component"
echo "Ableton neu starten (Plug-Ins ggf. neu scannen)."
