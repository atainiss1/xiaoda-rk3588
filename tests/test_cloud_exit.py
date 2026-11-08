"""Exercise real process signals during a failing OTA TLS handshake, offline."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile


def check(binary, stop_signal):
    with tempfile.TemporaryDirectory(prefix="rkvoice-exit-") as directory:
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(10)
            port = listener.getsockname()[1]
            # Synthetic identity only. This listener never speaks TLS or HTTP.
            config = {"SYSTEM_OPTIONS": {
                "DEVICE_ID": "offline-test-device",
                "CLIENT_ID": "offline-test-client",
                "NETWORK": {"OTA_VERSION_URL": f"https://127.0.0.1:{port}/"}
            }}
            root = Path(directory)
            (root / "config.json").write_text(json.dumps(config))
            (root / "efuse.json").write_text(json.dumps({"hmac_key": "offline-test-hmac"}))
            env = dict(os.environ, RKVOICE_IDENTITY_DIR=directory)
            process = subprocess.Popen(
                [binary, "probe"], env=env, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, text=True)
            try:
                connection, _ = listener.accept()
                with connection:
                    # Receive ClientHello: the program has installed its handlers.
                    connection.settimeout(5)
                    assert connection.recv(4096), "Missing TLS ClientHello"
                    if stop_signal is not None:
                        process.send_signal(stop_signal)
                    # Closing this connection causes the real TLS error path.
                output, _ = process.communicate(timeout=25)
                expected_code = 130 if stop_signal is not None else 1
                expected_status = "cancelled" if stop_signal is not None else "failed"
                assert process.returncode == expected_code, output
                assert f"RUN_SUMMARY status={expected_status}" in output, output
                assert "completed=0 no_speech=0" in output, output
                assert "RECORD_READY" not in output, output
                assert "offline-test-hmac" not in output, "Credential leaked"
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate()


for termination in (None, signal.SIGINT, signal.SIGTERM):
    check(str(Path(sys.argv[1]).resolve()), termination)
print("CLOUD_EXIT_TEST_OK network_failure=1 SIGINT=130 SIGTERM=130")
