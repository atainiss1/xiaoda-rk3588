#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
printf '#!/usr/bin/env bash\nsleep 30\n' > "$tmp/pactl"
chmod +x "$tmp/pactl"
export PATH="$tmp:$PATH"
source "$root/scripts/pulse-timeout.sh"
before=$SECONDS
rc=0
pactl info >"$tmp/result" 2>&1 || rc=$?
[[ "$rc" == 124 && $((SECONDS-before)) -lt 9 ]]
grep -q 'AUDIO_SETUP_ERROR operation=info code=124' "$tmp/result"
echo 'AUDIO_SETUP_TIMEOUT_TEST_OK hung_daemon_bounded=true timeout_seconds=5'
