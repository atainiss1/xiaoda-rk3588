"""Host-side fixtures only. The shipped audio implementation is C++."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave

exe = sys.argv[1]
with tempfile.TemporaryDirectory() as tmp:
    root = pathlib.Path(tmp)
    raw, wav = root / 'stereo.raw', root / 'mono.wav'
    left = [-32768, -10000, -1, 0, 1, 10000, 32767]
    raw.write_bytes(b''.join(struct.pack('<hh', x, 12345) for x in left))
    result = subprocess.run([exe, str(raw), str(wav), '4', '0'], check=True, capture_output=True, text=True)
    assert 'clipped_samples=4' in result.stdout, result.stdout
    with wave.open(str(wav), 'rb') as source:
        assert (source.getnchannels(), source.getframerate(), source.getsampwidth()) == (1, 16000, 2)
        assert list(struct.unpack('<7h', source.readframes(7))) == [-32768, -32768, -4, 0, 4, 32767, 32767]
    original = wav.read_bytes()
    assert subprocess.run([exe, str(raw), str(wav), '4', '0'], capture_output=True).returncode != 0
    assert wav.read_bytes() == original
    raw.write_bytes(b'\x00' * 64 + struct.pack('<hh', 42, -30000))
    skipped = root / 'skipped.wav'
    subprocess.run([exe, str(raw), str(skipped), '1', '1'], check=True, capture_output=True)
    with wave.open(str(skipped), 'rb') as source:
        assert source.getnframes() == 1
        assert source.readframes(1) == struct.pack('<h', 42)
    for payload, gain, skip in [(b'\x00', '4', '0'), (b'', '4', '0'), (b'\x00'*4, '17', '0'),
                                (b'\x00'*4, '4x', '0'), (b'\x00'*4, '4', '500')]:
        raw.write_bytes(payload)
        invalid = root / 'invalid.wav'
        assert subprocess.run([exe, str(raw), str(invalid), gain, skip], capture_output=True).returncode != 0
        assert not invalid.exists()
print('PCM_FILE_TESTS_OK')
