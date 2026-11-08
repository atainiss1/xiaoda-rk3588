#!/usr/bin/env bash
# Cross-build headers only. Never installs/replaces board or VM system libraries.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/qt-sdk/downloads
cd build/qt-sdk
base=https://ports.ubuntu.com/ubuntu-ports/pool/universe/q
if [[ ! -f downloads/qtbase5-dev.deb ]]; then
    curl -fL --connect-timeout 10 --max-time 60 \
        "$base/qtbase-opensource-src/qtbase5-dev_5.15.3%2Bdfsg-2ubuntu0.2_arm64.deb" \
        -o downloads/qtbase5-dev.deb
fi
if [[ ! -f downloads/qtdeclarative5-dev.deb ]]; then
    curl -fL --connect-timeout 10 --max-time 60 \
        "$base/qtdeclarative-opensource-src/qtdeclarative5-dev_5.15.3%2Bdfsg-1_arm64.deb" \
        -o downloads/qtdeclarative5-dev.deb
fi
# SHA256 values from the target board's APT metadata, queried 2026-10-21.
sha256sum -c <<'HASHES'
1be2cdeed7b73fd5c5e3dd6b0bc537d374cf01ac6bd76215466ebee9cec54ac6  downloads/qtbase5-dev.deb
402743972e2e03f24978f19fdadbc99650f6c96b80af53c94a1c79449e671b42  downloads/qtdeclarative5-dev.deb
HASHES
for package in downloads/qtbase5-dev.deb downloads/qtdeclarative5-dev.deb; do
    test "$(dpkg-deb -f "$package" Architecture)" = arm64
    dpkg-deb -x "$package" .
done
echo "QT_HEADERS_READY: supply existing board Qt runtime link libraries in build/qt-sdk/board-libs"
