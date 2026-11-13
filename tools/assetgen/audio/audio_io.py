"""File I/O: 16-bit mono WAV writing/reading, Vorbis encoding/decoding through ffmpeg."""
import json
import os
import shutil
import subprocess
import wave

import numpy as np

from synth import SR

FFMPEG = os.environ.get('FFMPEG') or shutil.which('ffmpeg') or '/usr/bin/ffmpeg'
FFPROBE = os.environ.get('FFPROBE') or shutil.which('ffprobe') or '/usr/bin/ffprobe'


def write_wav16(path, x):
    """Write a mono float signal (-1..1) as 16-bit PCM 44.1 kHz WAV."""
    x = np.asarray(x, dtype=float)
    assert x.ndim == 1, 'sound effects are mono'
    pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype('<i2')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def read_wav(path):
    """Returns (float array (C, n), sample_rate, sample_width_bytes, channels)."""
    with wave.open(path, 'rb') as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if sw != 2:
        raise ValueError(f'{path}: expected 16-bit PCM, got {8 * sw}-bit')
    data = np.frombuffer(raw, dtype='<i2').astype(float) / 32768.0
    return data.reshape(-1, ch).T, sr, sw, ch


def decode(path, channels=2):
    """Decode any ffmpeg-readable file to float32 (channels, n) at 44.1 kHz."""
    cmd = [FFMPEG, '-v', 'error', '-i', path, '-f', 'f32le', '-acodec', 'pcm_f32le',
           '-ac', str(channels), '-ar', str(SR), '-']
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    return np.frombuffer(raw, dtype='<f4').reshape(-1, channels).T.astype(float)


def probe(path):
    cmd = [FFPROBE, '-v', 'error', '-show_entries',
           'format=duration,size,bit_rate:stream=codec_name,sample_rate,channels,duration,duration_ts',
           '-of', 'json', path]
    return json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout)


def _encode(path, stereo, quality, title):
    # bitexact + fixed serial -> byte-identical files on every run
    cmd = [FFMPEG, '-v', 'error', '-y', '-f', 'f32le', '-ar', str(SR), '-ac', '2', '-i', '-',
           '-map_metadata', '-1', '-fflags', '+bitexact', '-flags:a', '+bitexact',
           '-c:a', 'libvorbis', '-q:a', str(quality), '-serial_offset', '1',
           '-metadata', f'title={title}', '-metadata', 'artist=Cosmic Platformer (procedural)',
           path]
    data = np.ascontiguousarray(stereo.T.astype('<f4')).tobytes()
    subprocess.run(cmd, input=data, check=True)


def encode_ogg(path, stereo, quality=5, max_peak_db=-1.0, title=None):
    """Encode stereo float (2, n) to Ogg Vorbis. Decodes the result and, if lossy coding pushed
    a peak above `max_peak_db`, re-encodes slightly quieter. Returns (decoded peak dB, gain dB)."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    title = title or os.path.splitext(os.path.basename(path))[0]
    gain_db = 0.0
    for _ in range(6):
        _encode(path, stereo * 10.0 ** (gain_db / 20.0), quality, title)
        dec = decode(path)
        pk = 20.0 * np.log10(np.max(np.abs(dec)) + 1e-12)
        if pk <= max_peak_db - 0.05:
            return pk, gain_db
        gain_db -= (pk - max_peak_db) + 0.15
    raise RuntimeError(f'{path}: could not bring decoded peak under {max_peak_db} dBFS')
