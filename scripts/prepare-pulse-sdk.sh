#!/usr/bin/env bash
# Match the PulseAudio version already installed on this RK3588 board.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/pulse-sdk/downloads
cd build/pulse-sdk
base=https://mirrors.huaweicloud.com/ubuntu-ports/pool/main/p/pulseaudio
for name in libpulse-dev libpulse0; do
    if [ ! -f "downloads/$name.deb" ]; then
        curl --fail --location --connect-timeout 10 --max-time 60 --retry 1 \
          "$base/${name}_15.99.1%2bdfsg1-1ubuntu2.2_arm64.deb" -o "downloads/$name.deb"
    fi
done
cat <<'CHECKSUMS' | sha512sum -c -
db1ad60b1522e986e8520edc3b5462c116a2e8c45d9f8016fab270f7d474a09863d13a5771f05d0b684157cbae41d2bd0a0011988f681ba5895431a01bef00b0  downloads/libpulse-dev.deb
8f4a7ad88fb5eaea70778a622c0c993ad722be99d107b8ee622921d9f74f9b3aa968b4dd15eefe1468d0f3eb9088637c97bb676b860e7c63b443a8f09495de8c  downloads/libpulse0.deb
CHECKSUMS
for package in downloads/libpulse-dev.deb downloads/libpulse0.deb; do
    test "$(dpkg-deb -f "$package" Architecture)" = arm64
    dpkg-deb -x "$package" .
done
echo "ARM64 PulseAudio SDK ready: $PWD"
