#!/usr/bin/env bash
# Per-device idle policy for the verified ES8388 half-duplex route.
# Changes live PulseAudio state only; does not restart the server or ALSA driver.
set -euo pipefail
export LC_ALL=C
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
case "${1:-fast}" in
    fast) timeout=0 ;;
    restore) timeout=5 ;;
    *) echo "Usage: $0 [fast|restore]" >&2; exit 2 ;;
esac
if [[ -n "$(pactl list short source-outputs)" || -n "$(pactl list short sink-inputs)" ]]; then
    echo "Audio streams are in use; stop them before configuring the idle policy." >&2
    exit 1
fi
mapfile -t policies < <(pactl list short modules | awk -F '\t' '$2 == "module-suspend-on-idle" { print $0 }')
if [[ ${#policies[@]} != 1 ]]; then
    echo "Expected one existing module-suspend-on-idle; no policy changed." >&2
    exit 1
fi
IFS=$'\t' read -r policy_id policy_name policy_args _ <<< "${policies[0]}"
[[ "$policy_id" =~ ^[0-9]+$ ]]
# Keep the module's existing default for all other audio devices.
read -r -a module_args <<< "$policy_args"
source_name="alsa_input.platform-es8388-sound.HiFi__hw_rockchipes8388__source"
sink_name="alsa_output.platform-es8388-sound.HiFi__hw_rockchipes8388__sink"
pactl list short sources | awk '{print $2}' | grep -Fxq "$source_name"
pactl list short sinks | awk '{print $2}' | grep -Fxq "$sink_name"
pacmd update-source-proplist "$source_name" "module-suspend-on-idle.timeout=$timeout"
pacmd update-sink-proplist "$sink_name" "module-suspend-on-idle.timeout=$timeout"
# The module reads device properties only when it attaches to a device.
pactl unload-module "$policy_id"
if ! pactl load-module module-suspend-on-idle "${module_args[@]}" >/dev/null; then
    pacmd update-source-proplist "$source_name" module-suspend-on-idle.timeout=5
    pacmd update-sink-proplist "$sink_name" module-suspend-on-idle.timeout=5
    pactl load-module module-suspend-on-idle "${module_args[@]}" >/dev/null || true
    echo "Could not apply audio idle policy; attempted to restore the 5-second default." >&2
    exit 1
fi
echo "AUDIO_IDLE_POLICY device=es8388 timeout_seconds=$timeout"
