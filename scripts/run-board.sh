#!/usr/bin/env bash
# iTOP RK3588 + ES8388 analog microphone. Run as the desktop audio user.
set -euo pipefail
export LC_ALL=C
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_dir/scripts/pulse-timeout.sh"
binary="$project_dir/bin/rkvoice_cloud"
if [[ -d "$project_dir/lib/speech" ]]; then
    export LD_LIBRARY_PATH="$project_dir/lib/speech${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
source_name="alsa_input.platform-es8388-sound.HiFi__hw_rockchipes8388__source"
sink_name="alsa_output.platform-es8388-sound.HiFi__hw_rockchipes8388__sink"

if [[ "$(id -u)" == 0 || ! -S "$XDG_RUNTIME_DIR/pulse/native" ]]; then
    echo "Run as the logged-in desktop audio user, with PulseAudio available." >&2
    exit 1
fi
if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    exit 1
fi
if [[ $# == 0 ]]; then set -- auto 30; fi
case "$1" in
    auto|talk|text|probe|--self-test|audio-check|service) ;;
    *) echo "Usage: $0 [auto|talk [SECONDS [TURNS]] | probe | text TEXT | --self-test | audio-check]" >&2; exit 2 ;;
esac

# Lock persists across exec, so launches through this entry cannot overlap.
exec 9>"$XDG_RUNTIME_DIR/rkvoice-cloud.lock"
if ! flock -n 9; then echo "Another voice client is already running." >&2; exit 1; fi
if pgrep -u "$(id -u)" -x rkvoice_cloud >/dev/null; then
    echo "A directly launched voice client is already running; stop it first." >&2
    exit 1
fi

case "$1" in
    auto|talk|text|audio-check|service)
        pactl info >/dev/null
        # A persistent service explicitly suspends its own filter when idle.
        # Its filter streams must not be mistaken for another voice client.
        if [[ "$1" != service ]]; then
            bash "$project_dir/scripts/configure-audio-idle.sh" fast
        fi
        # Pulse/UCM may restore Headset/Line 1. Explicitly select this board's
        # verified Main Mic/Line 2 path on each launch instead of trusting gain.
        if [[ "$1" != service ]]; then pactl suspend-source "$source_name" 0; fi
        pactl set-source-port "$source_name" "[In] Mic"
        amixer -q -c 1 sset "Headset Mic" off
        amixer -q -c 1 sset "Main Mic" on
        amixer -q -c 1 sset "Differential Mux" "Line 2"
        amixer -q -c 1 sset "Left Channel" 8
        amixer -q -c 1 sset "Right Channel" 8
        amixer -q -c 1 sset "PCM" 180
        pactl set-source-mute "$source_name" 0
        pactl set-source-volume "$source_name" 100%
        pactl set-sink-volume "$sink_name" 50%
        amixer -c 1 sget "Differential Mux" | grep -q "Item0: 'Line 2'"
        amixer -c 1 sget "Main Mic" | grep -q '\[on\]'
        export RKVOICE_SOURCE="$source_name" RKVOICE_SINK="$sink_name"
        echo "AUDIO_ROUTE_READY main_mic=on input=Line2 gain_db=24 speaker_percent=50"
        ;;
esac
if [[ "$1" == audio-check ]]; then exit 0; fi
if [[ "$1" == service ]]; then
    source "$project_dir/scripts/configure-voice-afe.sh"
    export RKVOICE_MODELS="$project_dir/models"
    export LD_LIBRARY_PATH="$project_dir/lib/speech${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
exec "$binary" "$@"
