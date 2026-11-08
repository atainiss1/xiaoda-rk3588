#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk="$PWD/build/cloud-sdk"
mkdir -p "$sdk/debs"
fetch_extract() {
  local name="$1" path="$2" sha="$3"
  local file="$sdk/debs/$name.deb"
  if [ ! -f "$file" ]; then
    curl --fail --location --max-time 120 "https://ports.ubuntu.com/ubuntu-ports/$path" -o "$file"
  fi
  printf '%s  %s\n' "$sha" "$file" | sha256sum -c -
  dpkg-deb -x "$file" "$sdk"
}
# Extract into a project SDK only; do not install ARM64 packages on the x86 VM.
# Board uses OpenSSL 3.0.2 Ubuntu1.29; the SDK uses the ABI-compatible Ubuntu1.30
# packages because the mirror no longer carries .29. Check ldd -r on the board.
fetch_extract libssl-dev pool/main/o/openssl/libssl-dev_3.0.2-0ubuntu1.30_arm64.deb 8f8e53d5cbefd7ea0796cf43d1016b7c5d5ba31418eb5ef9bc5353374a60a04c
fetch_extract libssl3 pool/main/o/openssl/libssl3_3.0.2-0ubuntu1.30_arm64.deb 1c4b8cc736eed8a0caafc385f854fa6852382221d84be4795f1d19a52b9fec53
fetch_extract libopus-dev pool/main/o/opus/libopus-dev_1.3.1-0.1build2_arm64.deb 8824dbcc118a75cebe4deb75956504cd3e40c433ca2751c20b1108e1f9fc2a7a
fetch_extract libopus0 pool/main/o/opus/libopus0_1.3.1-0.1build2_arm64.deb 9a18b90e6757e53a1e84e1ca2328c38fb00563eb93db324e1f84ae39b74d545f
echo CLOUD_SDK_READY
