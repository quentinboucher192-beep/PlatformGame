"""Song / track sequencer and mixer.

A Song is a fixed-length stereo canvas (exactly `bars` bars for loops). Notes are rendered by
instrument functions and mixed in at sample-exact positions; anything that rings past the end of
a loop wraps around into its beginning, and all bus effects (EQ, reverb, ping-pong delay,
side-chain ducking) are computed circularly, so the rendered loop is perfectly periodic.

Positions: bar numbers are 1-based, beats inside a bar are 0-based (fractional allowed).
"""
import zlib

import numpy as np

import dsp
from dsp import filt, hp, lp, mix_into, integrated_lufs, limiter_gain, peak_db, undb
from music.theory import parse_line, parse_drums
from synth import SR, ns, note_midi


class Track:
    def __init__(self, song, name, gain_db=0.0, pan=0.0, reverb=0.0, delay=0.0, duck=0.0,
                 highpass=None, lowpass=None, eq=()):
        self.song = song
        self.name = name
        self.gain_db = gain_db
        self.pan = pan
        self.reverb = reverb
        self.delay = delay
        self.duck = duck
        self.highpass = highpass
        self.lowpass = lowpass
        self.eq = tuple(eq)
        self.buf = np.zeros((2, song.L))
        self.count = 0

    # ---- low level
    def add(self, audio, pos, pan=None, gain=1.0):
        a = np.asarray(audio, dtype=float)
        if a.ndim == 1:
            st = dsp.pan(a, self.pan if pan is None else pan)
        else:
            st = a
            p = self.pan if pan is None else pan
            if p:
                th = (np.clip(p, -1, 1) + 1.0) * np.pi / 4.0
                st = st * np.array([[np.cos(th)], [np.sin(th)]]) * np.sqrt(2.0)
        mix_into(self.buf, st * gain, int(pos), self.song.loop)
        self.count += 1

    def at(self, audio, bar, beat=0.0, pan=None, gain=1.0):
        self.add(audio, self.song.pos(bar, beat), pan, gain)

    # ---- musical helpers
    def note(self, inst, bar, beat, beats, pitch, vel=0.8, pan=None, **kw):
        dur = beats * self.song.spb
        self.add(inst(note_midi(pitch), dur, vel, **kw), self.song.pos(bar, beat), pan)

    def line(self, inst, bar, text, vel=0.8, transpose=0, legato=1.0, pan=None, **kw):
        """Play a melody line (see music.theory) starting at `bar`."""
        for beat, beats, midi, v in parse_line(text, self.song.bar_beats):
            self.note(inst, bar, beat, beats * legato, midi + transpose, v if v else vel, pan, **kw)

    def chord(self, inst, bar, beat, beats, notes, vel=0.7, strum=0.0, pan=None, **kw):
        for i, nt in enumerate(notes):
            dur = beats * self.song.spb - i * strum
            pos = self.song.pos(bar, beat) + ns(i * strum)
            self.add(inst(note_midi(nt), dur, vel, **kw), pos, pan)

    def hits(self, sample, bar, pattern, step=0.25, vel=1.0, pan=None, duck=False):
        """Drum pattern: one character per `step` beats (see music.theory.DRUM_VEL)."""
        for i, v in parse_drums(pattern):
            self.at(sample, bar, i * step, pan, v * vel)
            if duck:
                self.song.duck_at(bar, i * step, v * vel)


class Song:
    def __init__(self, name, bpm, bar_beats, bars, loop=True, tail=0.0, target_lufs=-14.0,
                 ceiling_db=-1.8, end_fade=0.0):
        self.name = name
        self.bpm = bpm
        self.bar_beats = bar_beats
        self.bars = bars
        self.loop = loop
        self.spb = 60.0 / bpm
        self.loop_len = int(round(bars * bar_beats * self.spb * SR))
        self.L = self.loop_len if loop else self.loop_len + ns(tail)
        self.target_lufs = target_lufs
        self.ceiling_db = ceiling_db
        self.end_fade = end_fade
        self.tracks = []
        self.reverb = dict(seconds=3.2, t60=2.4, predelay=0.02, lf=1.1, hf=0.35, ret=0.35)
        self.delay = dict(beats=0.75, feedback=0.38, damp=3500.0, ret=0.32, to_reverb=0.3)
        self.duck_hits = []
        self.duck_tau = 0.14
        self.seed = zlib.crc32(name.encode())

    @property
    def seconds(self):
        return self.L / SR

    def pos(self, bar, beat=0.0):
        return int(round(((bar - 1) * self.bar_beats + beat) * self.spb * SR))

    def track(self, name, **kw):
        t = Track(self, name, **kw)
        self.tracks.append(t)
        return t

    def duck_at(self, bar, beat, amount=1.0):
        self.duck_hits.append((self.pos(bar, beat), amount))

    def rng(self, tag=''):
        return np.random.default_rng(self.seed + zlib.crc32(tag.encode()))

    # ---- rendering
    def _duck_curve(self):
        imp = np.zeros(self.L)
        for p, a in self.duck_hits:
            if self.loop:
                imp[p % self.L] = max(imp[p % self.L], a)
            elif p < self.L:
                imp[p] = max(imp[p], a)
        m = ns(self.duck_tau * 7)
        t = np.arange(m) / SR
        kern = (1.0 - np.exp(-t / 0.004)) * np.exp(-t / self.duck_tau)
        kern /= kern.max()
        env = dsp.convolve(imp, kern, circular=self.loop)[:self.L]
        return np.clip(env, 0.0, 1.0)

    def render(self, log=print):
        circ = self.loop
        L = self.L
        dry = np.zeros((2, L))
        rev_in = np.zeros(L)
        dly_in = np.zeros(L)
        duck = self._duck_curve() if self.duck_hits else None
        stats = []
        for tr in self.tracks:
            if tr.count == 0:
                continue
            b = tr.buf
            resp = []
            if tr.highpass:
                resp.append(hp(tr.highpass, 0.7))
            if tr.lowpass:
                resp.append(lp(tr.lowpass, 0.7))
            resp.extend(tr.eq)
            if resp:
                b = filt(b, *resp, circular=circ)[:, :L]
            if tr.duck and duck is not None:
                b = b * (1.0 - tr.duck * duck)
            b = b * undb(tr.gain_db)
            dry += b
            mono = 0.5 * (b[0] + b[1])
            if tr.reverb:
                rev_in += mono * tr.reverb
            if tr.delay:
                dly_in += mono * tr.delay
            stats.append((tr.name, integrated_lufs(b, circular=circ)))
        out = dry
        if np.any(dly_in):
            d = self.delay
            wet = dsp.pingpong(dly_in, d['beats'] * self.spb, d['feedback'], d['damp'], circular=circ)
            wet = wet[:, :L] if wet.shape[1] >= L else np.pad(wet, ((0, 0), (0, L - wet.shape[1])))
            out = out + wet * d['ret']
            rev_in += 0.5 * (wet[0] + wet[1]) * d['to_reverb']
        if np.any(rev_in):
            r = self.reverb
            ir = dsp.reverb_ir(r['seconds'], r['t60'], self.rng('ir'), lf_ratio=r['lf'],
                               hf_ratio=r['hf'], predelay=r['predelay'])
            wet = dsp.convolve(rev_in, ir, circular=circ)[:, :L]
            out = out + wet * r['ret']
        out = filt(out, hp(28.0, 0.7), circular=circ)[:, :L]
        if self.end_fade and not self.loop:
            out = dsp.fade(out, 0.0, self.end_fade)
        out, info = self._master(out)
        if log:
            ref = integrated_lufs(dry, circular=circ)
            log('    track loudness (LU rel. dry mix): ' + ', '.join(f'{n} {v - ref:+.1f}' for n, v in stats))
        info['tracks'] = stats
        return out, info

    def _master(self, x):
        circ = self.loop
        gain = undb(self.target_lufs - integrated_lufs(x, circular=circ))
        y = x
        gmin = 1.0
        for _ in range(5):
            g = limiter_gain(x * gain, self.ceiling_db, window=0.012, circular=circ)
            y = x * gain * g
            gmin = float(g.min())
            measured = integrated_lufs(y, circular=circ)
            if abs(measured - self.target_lufs) < 0.1:
                break
            gain *= undb(self.target_lufs - measured)
        thr = undb(self.ceiling_db)
        y = np.clip(y, -thr, thr)
        if not circ:
            y = dsp.fade(y, 0.0, 0.01)
        return y, dict(lufs=integrated_lufs(y, circular=circ), peak=peak_db(y),
                       gain_reduction=float(dsp.db(gmin)))
