#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
log=validation/multiturn-cancel-20261021.log
./scripts/run-board.sh auto 20 1 >"$log" 2>&1 &
child=$!
verified_child() {
    [[ "$(readlink -f "/proc/$child/exe")" == "$PWD/bin/rkvoice_cloud" ]] || return 1
    local -a args=()
    mapfile -d '' -t args <"/proc/$child/cmdline"
    [[ ${#args[@]} == 4 && "${args[0]}" == "$PWD/bin/rkvoice_cloud" &&
       "${args[1]}" == auto && "${args[2]}" == 20 && "${args[3]}" == 1 ]]
}
ready=false
for ((i=0; i<150; ++i)); do
    if grep -q '^RECORD_READY ' "$log"; then ready=true; break; fi
    if ! kill -0 "$child" 2>/dev/null; then break; fi
    sleep 0.2
done
if [[ "$ready" != true ]]; then
    cat "$log"
    if verified_child; then kill -TERM "$child"; fi
    wait "$child" || true
    echo CANCEL_TEST_FAILED_not_ready
    exit 1
fi
verified_child
echo "CANCEL_TARGET_VERIFIED pid=$child executable=rkvoice_cloud args=auto,20,1"
kill -TERM "$child"
set +e
wait "$child"
result=$?
set -e
cat "$log"
echo "CANCEL_TEST_EXIT=$result"
[[ "$result" == 130 ]]
grep -q '^RUN_SUMMARY status=cancelled .*exit_code=130 ' "$log"
! grep -q '^SPEECH_START ' "$log"
! pgrep -u "$(id -u)" -x rkvoice_cloud
export XDG_RUNTIME_DIR=/run/user/1000
echo "SOURCE_OUTPUTS_AFTER_CANCEL"
pactl list short source-outputs
echo "SINK_INPUTS_AFTER_CANCEL"
pactl list short sink-inputs
./scripts/run-board.sh probe
echo CANCEL_AND_RELAUNCH_TEST_OK
