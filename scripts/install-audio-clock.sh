#!/usr/bin/env bash
# Run once as the desktop user. Applies to subsequent PulseAudio starts;
# deliberately does not restart the board or the sound service.
set -euo pipefail
[[ "$(id -u)" != 0 ]] || { echo 'Run as the desktop user.' >&2; exit 1; }
config="${XDG_CONFIG_HOME:-$HOME/.config}/pulse"
mkdir -p "$config"
python3 - "$config/daemon.conf" <<'PY'
import pathlib, sys, time
p = pathlib.Path(sys.argv[1])
begin = '# BEGIN RKVOICE ES8388 CLOCK'
end = '# END RKVOICE ES8388 CLOCK'
old = p.read_text() if p.exists() else '.include /etc/pulse/daemon.conf\n'
if begin in old:
    a, rest = old.split(begin, 1)
    _, b = rest.split(end, 1)
    old = a + b.lstrip('\n')
new = old.rstrip() + '\n\n' + begin + '\n' + \
    'default-sample-rate = 48000\nalternate-sample-rate = 48000\n' + end + '\n'
if p.exists() and p.read_text() == new:
    print('AUDIO_CLOCK_CONFIG already_installed=true'); sys.exit(0)
if p.exists():
    backup = p.with_name(p.name + '.pre-rkvoice-' + str(time.time_ns()))
    backup.write_bytes(p.read_bytes())
p.write_text(new)
print('AUDIO_CLOCK_CONFIG installed=true rate=48000 restart_required=true')
PY
