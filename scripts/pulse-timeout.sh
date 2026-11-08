#!/usr/bin/env bash
# Source in audio setup scripts: a blocked sound daemon must fail promptly.
pactl() {
    local rc=0
    command timeout --kill-after=1s 5s pactl "$@" || rc=$?
    if (( rc != 0 )); then
        echo "AUDIO_SETUP_ERROR operation=$1 code=$rc" >&2
        return "$rc"
    fi
}
