#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk="$PWD/build/qt-sdk"
cxx="${CXX_ARM64:-/usr/local/arm64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++}"
include="$sdk/usr/include/aarch64-linux-gnu/qt5"
lib="$sdk/board-libs"
test -f "$include/QtQuick/QQuickWindow"
mkdir -p build/deploy
flags=(-std=c++17 -O2 -fPIC -Wall -Wextra -Wpedantic
       "-I$sdk/usr/include" "-I$include" "-I$include/QtCore" "-I$include/QtGui" "-I$include/QtQml"
       "-I$include/QtQuick" "-I$include/QtNetwork" "-I$include/QtQmlModels"
       -Wl,--allow-shlib-undefined)
"$cxx" "${flags[@]}" ui/main.cpp -o build/deploy/rkvoice_ui \
    "$lib/libQt5Quick.so.5" "$lib/libQt5Qml.so.5" "$lib/libQt5Gui.so.5" \
    "$lib/libQt5Network.so.5" "$lib/libQt5Core.so.5" -pthread
"$cxx" "${flags[@]}" ui/test_voice_state.cpp -o build/deploy/rkvoice_ui_state_test \
    "$lib/libQt5Core.so.5" -pthread
echo "UI_CROSS_BUILD_OK"
