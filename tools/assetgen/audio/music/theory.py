"""Tiny notation helpers for writing songs as text.

Melody lines:  "D5:1.5 E5:.5 F5 A5 | G5:1.5@0.7 F5:.5 E5:2 |"
  NOTE[:beats][@velocity]   default 1 beat, 'r' = rest, '|' = bar line (checked).
Drum lines:    "x..x..x.x...x..." one char per step; X=accent, x=normal, o=soft, g=ghost.
"""
from synth import note_midi

DRUM_VEL = {'X': 1.0, 'x': 0.8, 'o': 0.55, 'g': 0.3}


def parse_line(text, beats_per_bar=None):
    """-> list of (beat_offset, duration_beats, midi or None, velocity or None)."""
    events = []
    pos = 0.0
    bar_start = 0.0
    for raw_bar in text.split('|'):
        tokens = raw_bar.split()
        if not tokens:
            continue
        for tok in tokens:
            vel = None
            if '@' in tok:
                tok, v = tok.split('@')
                vel = float(v)
            dur = 1.0
            if ':' in tok:
                tok, d = tok.split(':')
                dur = float(d)
            if tok.lower() not in ('r', '-'):
                events.append((pos, dur, note_midi(tok), vel))
            pos += dur
        if beats_per_bar is not None:
            length = pos - bar_start
            if abs(length - beats_per_bar) > 1e-6:
                raise ValueError(f'bar "{raw_bar.strip()}" has {length} beats, expected {beats_per_bar}')
        bar_start = pos
    return events


def parse_drums(pattern):
    """-> list of (step_index, velocity)."""
    out = []
    for i, ch in enumerate(pattern.replace(' ', '')):
        if ch in DRUM_VEL:
            out.append((i, DRUM_VEL[ch]))
    return out


def midis(names):
    return [note_midi(n) for n in names]
