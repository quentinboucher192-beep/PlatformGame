#!/usr/bin/env python3
"""Regenerates every sound effect (assets/sounds/*.wav) and music track (assets/music/*.ogg).

Usage (from the repo root):
    python3 tools/assetgen/audio/generate.py              # everything
    python3 tools/assetgen/audio/generate.py --sfx        # sound effects only
    python3 tools/assetgen/audio/generate.py --music      # music only
    python3 tools/assetgen/audio/generate.py jump menu_theme   # just these names

Deterministic (fixed seeds) and offline: needs only numpy and ffmpeg (libvorbis).
Fonts are fetched separately by fetch_fonts.py; validate.py checks the results.
"""
import argparse
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)

from audio_io import encode_ogg  # noqa: E402
from music import instruments  # noqa: E402
from music.songs import SONGS  # noqa: E402
from sfx import REGISTRY  # noqa: E402
from sfx.render import render_all  # noqa: E402

SOUNDS_DIR = os.path.join(ROOT, 'assets', 'sounds')
MUSIC_DIR = os.path.join(ROOT, 'assets', 'music')
VORBIS_QUALITY = 5


def generate_music(names=None, log=print):
    os.makedirs(MUSIC_DIR, exist_ok=True)
    for mod in SONGS:
        if names and mod.NAME not in names:
            continue
        t0 = time.time()
        song = mod.build()
        stereo, info = song.render(log=log)
        path = os.path.join(MUSIC_DIR, mod.NAME + '.ogg')
        pk, gain = encode_ogg(path, stereo, quality=VORBIS_QUALITY, title=mod.NAME)
        kind = 'loop' if song.loop else 'one-shot'
        log(f'  music/{mod.NAME}.ogg  {stereo.shape[1] / 44100:6.2f}s ({kind}, {stereo.shape[1]} samples)'
            f'  {info["lufs"]:5.1f} LUFS  decoded peak {pk:5.2f} dBFS'
            f'  limiter {info["gain_reduction"]:4.1f} dB  {os.path.getsize(path) / 1024:6.0f} KiB'
            f'  [{time.time() - t0:4.1f}s]')
        instruments.clear_cache()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('names', nargs='*', help='only generate these sounds / songs')
    ap.add_argument('--sfx', action='store_true', help='sound effects only')
    ap.add_argument('--music', action='store_true', help='music only')
    args = ap.parse_args()
    do_sfx = args.sfx or not args.music
    do_music = args.music or not args.sfx
    names = set(args.names) if args.names else None
    if names:
        known = set(REGISTRY) | {m.NAME for m in SONGS}
        unknown = names - known
        if unknown:
            ap.error('unknown name(s): ' + ', '.join(sorted(unknown)))
    t0 = time.time()
    if do_sfx and (not names or names & set(REGISTRY)):
        print(f'== sound effects -> {os.path.relpath(SOUNDS_DIR, ROOT)}', flush=True)
        render_all(SOUNDS_DIR, names)
    if do_music and (not names or names & {m.NAME for m in SONGS}):
        print(f'== music -> {os.path.relpath(MUSIC_DIR, ROOT)}', flush=True)
        generate_music(names)
    print(f'Audio done in {time.time() - t0:.1f}s')


if __name__ == '__main__':
    main()
