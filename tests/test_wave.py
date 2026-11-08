"""Exercise WAV input without requiring an audio server."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

exe = sys.argv[1]
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp) / 'fixture.wav'
    with wave.open(str(path), 'wb') as out:
        out.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
        out.writeframes(struct.pack('<hhh', -32768, 0, 32767))
    original = path.read_bytes()
    def inspect(data, success):
        path.write_bytes(data)
        r = subprocess.run([exe, 'inspect', str(path)], capture_output=True, text=True)
        assert (r.returncode == 0) == success, (r.returncode, r.stdout, r.stderr)
        return r.stdout
    assert 'frames=3 peak=32768' in inspect(original, True)
    # Unknown, odd-sized metadata chunk with RIFF padding.
    junk = b'JUNK' + struct.pack('<I', 1) + b'x\0'
    metadata = original[:12] + junk + original[12:]
    metadata = metadata[:4] + struct.pack('<I', len(metadata)-8) + metadata[8:]
    inspect(metadata, True)
    for data in [b'', original[:-1], b'NOPE'+original[4:],
                 original[:20]+struct.pack('<H', 3)+original[22:],
                 original[:22]+struct.pack('<H', 0)+original[24:],
                 original[:40]+struct.pack('<I', 999999)+original[44:],
                 original[:32]+struct.pack('<H', 8)+original[34:]]:
        inspect(data, False)
print('WAV_VALIDATION_OK')
