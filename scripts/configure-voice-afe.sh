#!/usr/bin/env bash
# Source this file after setting the known ES8388 hardware route.
# Session-local filters use the desktop source, with the hardware clock fixed
# at 48 kHz by install-audio-clock.sh. Never open the ALSA input a second time.
set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/pulse-timeout.sh"
raw_source=alsa_input.platform-es8388-sound.HiFi__hw_rockchipes8388__source
raw_sink=alsa_output.platform-es8388-sound.HiFi__hw_rockchipes8388__sink
default_source=$(pactl get-default-source)
default_sink=$(pactl get-default-sink)
sink_volume=$(pactl get-sink-volume "$raw_sink" | awk 'NR==1 {print $5}')
sources=$(pactl list short sources)
sinks=$(pactl list short sinks)
if ! awk -v name="$raw_source" '$2==name && $0 ~ /48000Hz/ {ok=1} END {exit !ok}' <<< "$sources" ||
   ! awk -v name="$raw_sink" '$2==name && $0 ~ /48000Hz/ {ok=1} END {exit !ok}' <<< "$sinks"; then
    echo 'AUDIO_SETUP_ERROR reason=hardware_clock_not_48000' >&2
    exit 1
fi
if ! pactl list short sources | awk '{print $2}' | grep -qx rkvoice_mic_left; then
    pactl load-module module-remap-source master="$raw_source" source_name=rkvoice_mic_left \
        rate=48000 channels=1 master_channel_map=front-left channel_map=mono remix=no >/dev/null
fi
if ! pactl list short sources | awk '{print $2}' | grep -qx rkvoice_clean; then
    pactl load-module module-echo-cancel source_master=rkvoice_mic_left sink_master="$raw_sink" \
        source_name=rkvoice_clean sink_name=rkvoice_playback rate=16000 channels=1 channel_map=mono \
        aec_method=webrtc 'aec_args="analog_gain_control=0 digital_gain_control=0 noise_suppression=1 high_pass_filter=1 extended_filter=1"' >/dev/null
fi
# Registering virtual devices must not reroute unrelated desktop applications.
pactl set-default-source "$default_source"
pactl set-default-sink "$default_sink"
pactl set-source-volume rkvoice_mic_left 100%
pactl set-source-volume rkvoice_clean 100%
pactl set-source-mute rkvoice_clean 0
pactl set-sink-volume rkvoice_playback 100%
pactl set-sink-mute rkvoice_playback 0
# Flat-volume configurations may raise the master when the virtual sink is set.
pactl set-sink-volume "$raw_sink" "$sink_volume"
export RKVOICE_SOURCE=rkvoice_clean RKVOICE_SINK=rkvoice_playback
echo 'AFE_READY engine=pulseaudio-webrtc aec=true ns=true agc=false capture=left_mono hardware_rate=48000'
