#!/usr/bin/env python3
"""Validates the audio + font deliverables of docs/ASSET_SPEC.md sections 12 and 13.

Usage (from the repo root):  python3 tools/assetgen/audio/validate.py [--quiet] [--no-fonts]

Checks
  sounds/*.wav  every listed effect exists; RIFF PCM 16-bit / mono / 44.1 kHz; peak <= -1 dBFS;
                no clipped samples; no DC offset; starts/ends at ~zero (no clicks); little leading /
                trailing silence; not silent; isolated sample-discontinuity scan; spectral sanity
                (sub rumble, piercing highs); loudness balance between categories.
  music/*.ogg   every listed track exists; Vorbis / 44.1 kHz / stereo; exact bar-length sample
                count for loops; decoded peak <= -1 dBFS; integrated loudness (ffmpeg ebur128) and
                true peak; DC; dropouts; stereo correlation; octave-band balance; loop seam
                (boundary step, envelope and spectral continuity, high-frequency splice energy);
                total size < 12 MB.
  fonts/        files exist, sfnt magic, required tables, PIL/FreeType can load them, cmap covers
                English + French glyphs, OFL licence texts present.
Exit status 1 if any FAIL.
"""
import argparse
import os
import re
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)

from audio_io import FFMPEG, decode, probe, read_wav  # noqa: E402
from dsp import max_momentary  # noqa: E402
from fontcheck import check_font  # noqa: E402

SR = 44100
ASSETS = os.path.join(ROOT, 'assets')

# ---- the contract (docs/ASSET_SPEC.md section 12), independent of the generator's registry
SOUNDS = """jump double_jump land footstep_a footstep_b wall_jump slide dash wave_cast wave_hit
shield_up shield_block shield_down impulse teleport_out teleport_in slash slash_hit enemy_hurt
enemy_death player_hurt player_death respawn checkpoint collect_shard collect_relic collect_orb
power_unlock door_open lever crumble bounce laser_charge laser_fire enemy_shoot enemy_alert
enemy_telegraph golem_slam explosion boss_roar boss_phase summon wisp_teleport spore_spit ui_move
ui_confirm ui_back ui_error timer_tick timer_warning medal score_tick portal energy_low""".split()

# name: (loop?, min s, max s, LUFS low, LUFS high)
MUSIC = {
    'menu_theme': (True, 55, 75, -17.5, -12.5),
    'planet_map': (True, 40, 56, -17.5, -12.5),
    'sylvanox_level': (True, 88, 102, -16.5, -12.0),
    'sylvanox_boss': (True, 55, 66, -16.0, -11.5),
    'victory': (False, 5, 8, -17.0, -11.5),
    'sylvanox_ambience': (True, 35, 45, -30.0, -19.0),
}
FONTS = {'Orbitron.ttf': 'title', 'ChakraPetch-Regular.ttf': 'ui', 'ChakraPetch-SemiBold.ttf': 'ui'}
LICENCES = ['OFL-Orbitron.txt', 'OFL-ChakraPetch.txt']

# loudness groups for the balance check (max-momentary LUFS)
QUIET = {'ui_move', 'ui_confirm', 'ui_back', 'ui_error', 'timer_tick', 'score_tick',
         'footstep_a', 'footstep_b'}
LOUD = {'explosion', 'boss_roar', 'golem_slam', 'boss_phase'}


class Report:
    def __init__(self, quiet=False):
        self.fails = []
        self.warns = []
        self.quiet = quiet

    def fail(self, what, msg):
        self.fails.append(f'{what}: {msg}')

    def warn(self, what, msg):
        self.warns.append(f'{what}: {msg}')

    def line(self, text):
        if not self.quiet:
            print(text)


def db(x):
    return 20 * np.log10(max(float(x), 1e-12))


def octave_bands(x):
    """Energy share (dB) per octave band 31.5 Hz .. 16 kHz."""
    X = np.abs(np.fft.rfft(x)) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    tot = X.sum() + 1e-20
    centers = [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    return centers, [10 * np.log10(X[(f >= c / 2 ** 0.5) & (f < c * 2 ** 0.5)].sum() / tot + 1e-15)
                     for c in centers]


def discontinuities(x):
    """Isolated jumps: second difference spikes far above the local texture."""
    if len(x) < 64:
        return 0, 0.0
    d2 = np.abs(np.diff(x, 2))
    w = 64
    pad = np.pad(d2, w, mode='edge')
    c = np.cumsum(np.concatenate([[0], pad ** 2]))
    local = np.sqrt((c[2 * w + 1:] - c[:-2 * w - 1]) / (2 * w + 1))[:len(d2)]
    ratio = d2 / (local + 1e-4)
    spikes = np.nonzero((ratio > 9.0) & (d2 > 0.08))[0]
    return len(spikes), float(d2.max())


# --------------------------------------------------------------------------- sounds

def check_sounds(rep):
    rep.line('\n== sounds (assets/sounds) ==')
    rep.line(f'{"name":17} {"dur":>5} {"peak":>6} {"loud":>6} {"cent":>6} {"lead":>5} {"tail":>5}  notes')
    loud = {}
    sdir = os.path.join(ASSETS, 'sounds')
    for name in SOUNDS:
        path = os.path.join(sdir, name + '.wav')
        what = f'sounds/{name}.wav'
        if not os.path.exists(path):
            rep.fail(what, 'missing')
            continue
        try:
            with open(path, 'rb') as f:
                hdr = f.read(36)
            fmt_tag = int.from_bytes(hdr[20:22], 'little')
            if hdr[:4] != b'RIFF' or hdr[8:12] != b'WAVE' or fmt_tag != 1:
                rep.fail(what, f'not a RIFF/WAVE PCM file (format tag {fmt_tag})')
                continue
            data, sr, sw, ch = read_wav(path)
        except Exception as e:
            rep.fail(what, f'unreadable: {e}')
            continue
        if ch != 1:
            rep.fail(what, f'{ch} channels (expected mono)')
        if sr != SR:
            rep.fail(what, f'{sr} Hz (expected 44100)')
        x = data[0]
        n = len(x)
        dur = n / SR
        notes = []
        if not 0.02 <= dur <= 5.0:
            rep.fail(what, f'duration {dur:.2f}s out of range')
        pk = db(np.max(np.abs(x)))
        if pk > -1.0:
            rep.fail(what, f'peak {pk:.2f} dBFS > -1 dBFS')
        if pk < -30:
            rep.fail(what, f'nearly silent (peak {pk:.1f} dBFS)')
        if np.sum(np.abs(x) >= 0.999) > 0:
            rep.fail(what, 'clipped samples')
        dc = float(np.mean(x))
        if abs(dc) > 0.003:
            rep.fail(what, f'DC offset {dc:.4f}')
        if abs(x[0]) > 0.002 or abs(x[-1]) > 0.002:
            rep.fail(what, f'does not start/end at zero ({x[0]:.4f} / {x[-1]:.4f}) -> click')
        above = np.nonzero(np.abs(x) > 10 ** (-60 / 20))[0]
        lead = above[0] / SR if len(above) else dur
        tail_idx = np.nonzero(np.abs(x) > 10 ** (-66 / 20))[0]
        tail = (n - tail_idx[-1]) / SR if len(tail_idx) else dur
        if lead > 0.02:
            rep.warn(what, f'{lead * 1000:.0f} ms of leading near-silence')
        if tail > 0.06:
            rep.warn(what, f'{tail * 1000:.0f} ms of trailing silence')
        k, _ = discontinuities(x)
        if k:
            notes.append(f'{k} sharp edges')
            if k > 3:
                rep.warn(what, f'{k} isolated sample discontinuities (possible clicks)')
        cen, bands = octave_bands(x)
        X = np.abs(np.fft.rfft(x)) ** 2
        f = np.fft.rfftfreq(n, 1 / SR)
        centroid = float((f * X).sum() / (X.sum() + 1e-20))
        hf = X[f > 12000].sum() / (X.sum() + 1e-20)
        sub = X[f < 35].sum() / (X.sum() + 1e-20)
        if hf > 0.2:
            rep.warn(what, f'{hf * 100:.0f}% of energy above 12 kHz (harsh/hissy?)')
        if sub > 0.2:
            rep.warn(what, f'{sub * 100:.0f}% of energy below 35 Hz (rumble)')
        lm = max_momentary(x)
        loud[name] = lm
        rep.line(f'{name:17} {dur:5.2f} {pk:6.1f} {lm:6.1f} {centroid:6.0f} {lead * 1000:5.0f} '
                 f'{tail * 1000:5.0f}  {" ".join(notes)}')
    # balance between categories
    if loud:
        mid = [v for k_, v in loud.items() if k_ not in QUIET and k_ not in LOUD]
        med = float(np.median(mid)) if mid else -16.0
        for k_ in QUIET & loud.keys():
            if loud[k_] > med:
                rep.fail(f'sounds/{k_}.wav', f'UI/footstep louder ({loud[k_]:.1f}) than gameplay median {med:.1f}')
        for k_ in LOUD & loud.keys():
            if loud[k_] < med:
                rep.fail(f'sounds/{k_}.wav', f'big impact quieter ({loud[k_]:.1f}) than gameplay median {med:.1f}')
        spread = max(loud.values()) - min(loud.values())
        rep.line(f'loudness: gameplay median {med:.1f} LUFS (100 ms max), span {spread:.1f} LU')


# --------------------------------------------------------------------------- music

def ebur128(path):
    cmd = [FFMPEG, '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-']
    err = subprocess.run(cmd, capture_output=True, text=True).stderr
    summary = err[err.rfind('Summary:'):]
    i = re.search(r'I:\s+(-?[\d.]+) LUFS', summary)
    lra = re.search(r'LRA:\s+(-?[\d.]+) LU', summary)
    tp = re.search(r'Peak:\s+(-?[\d.]+|-inf) dBFS', summary)
    return (float(i.group(1)) if i else None, float(lra.group(1)) if lra else None,
            float(tp.group(1)) if tp and tp.group(1) != '-inf' else None)


def expected_samples(name):
    """Exact sample length from the song's musical definition (tempo x bars)."""
    try:
        import importlib
        m = importlib.import_module(f'music.songs.{name}')
    except Exception:
        return None
    n = int(round(m.BARS * m.BEATS_PER_BAR * 60.0 / m.BPM * SR))
    if not m.LOOP:
        n += int(round(getattr(m, 'TAIL', 0.0) * SR))
    return n


def seam_metrics(x):
    """Compare the loop seam (end -> start) with ordinary consecutive material."""
    mono = x.mean(axis=0)
    n = len(mono)
    # 1) boundary step vs the steps in the 50 ms on either side of the seam (a discontinuity is
    #    a local outlier; a note onset on the downbeat is not)
    h50 = int(0.05 * SR)
    around = np.concatenate([x[:, -h50:], x[:, :h50]], axis=1)
    steps = np.abs(np.diff(around, axis=1)).max(axis=0)
    jump = float(steps[h50 - 1])
    local = float(np.max(np.delete(steps, h50 - 1)))
    # 2) level drop across the seam (a tail that was cut instead of wrapped shows up as a drop;
    #    a rise is just the downbeat). Compared with the largest drops inside the track.
    w = int(0.02 * SR)
    r_end = np.sqrt(np.mean(mono[-w:] ** 2)) + 1e-9
    r_start = np.sqrt(np.mean(mono[:w] ** 2)) + 1e-9
    env_drop = db(r_end) - db(r_start)
    k = n // w
    rms = np.sqrt(np.mean(mono[:k * w].reshape(k, w) ** 2, axis=1)) + 1e-9
    drops = -np.diff(20 * np.log10(rms))
    env_p995 = float(np.percentile(drops, 99.5))
    # 3) spectral continuity: 0.25 s windows; seam pair vs consecutive pairs
    W = int(0.25 * SR)
    win = np.hanning(W)

    def spec(seg):
        return np.log10(np.abs(np.fft.rfft(seg * win))[:2048] + 1e-6)
    seam_d = float(np.mean(np.abs(spec(mono[-W:]) - spec(mono[:W]))))
    pairs = []
    for s in range(0, n - 2 * W, max(W, (n - 2 * W) // 120)):
        pairs.append(np.mean(np.abs(spec(mono[s:s + W]) - spec(mono[s + W:s + 2 * W]))))
    spec_p95 = float(np.percentile(pairs, 95))
    # 4) high-frequency energy of a 10 ms window straddling the seam (clicks are broadband)
    hpx = np.diff(mono)                      # crude high-pass
    h = int(0.005 * SR)
    seam_hf = np.sqrt(np.mean(np.concatenate([hpx[-h:], [mono[0] - mono[-1]], hpx[:h]]) ** 2))
    m2 = (len(hpx) // (2 * h)) * 2 * h
    blocks = np.sqrt(np.mean(hpx[:m2].reshape(-1, 2 * h) ** 2, axis=1))
    hf_ratio = float(seam_hf / (np.percentile(blocks, 99) + 1e-9))
    return dict(jump=jump, local=local, env_drop=env_drop, env_p995=env_p995, seam_d=seam_d,
                spec_p95=spec_p95, hf_ratio=hf_ratio)


def check_music(rep):
    rep.line('\n== music (assets/music) ==')
    total = 0
    mdir = os.path.join(ASSETS, 'music')
    for name, (loop, dmin, dmax, llo, lhi) in MUSIC.items():
        path = os.path.join(mdir, name + '.ogg')
        what = f'music/{name}.ogg'
        if not os.path.exists(path):
            rep.fail(what, 'missing')
            continue
        size = os.path.getsize(path)
        total += size
        try:
            info = probe(path)
            st = info['streams'][0]
        except Exception as e:
            rep.fail(what, f'ffprobe failed: {e}')
            continue
        if st.get('codec_name') != 'vorbis':
            rep.fail(what, f'codec {st.get("codec_name")} (expected vorbis)')
        if int(st.get('sample_rate', 0)) != SR:
            rep.fail(what, f'sample rate {st.get("sample_rate")}')
        if int(st.get('channels', 0)) != 2:
            rep.fail(what, f'{st.get("channels")} channels (expected stereo)')
        dur = float(info['format'].get('duration', 0))
        if not dmin <= dur <= dmax:
            rep.fail(what, f'duration {dur:.2f}s outside {dmin}-{dmax}s')
        x = decode(path)
        n = x.shape[1]
        exp = expected_samples(name)
        if exp is not None and n != exp:
            rep.fail(what, f'{n} samples decoded, expected exactly {exp} (bar-length)')
        pk = db(np.max(np.abs(x)))
        if pk > -1.0:
            rep.fail(what, f'decoded peak {pk:.2f} dBFS > -1 dBFS')
        lufs, lra, tp = ebur128(path)
        if lufs is None or not llo <= lufs <= lhi:
            rep.fail(what, f'integrated loudness {lufs} LUFS outside [{llo}, {lhi}]')
        if tp is not None and tp > -0.5:
            rep.warn(what, f'true peak {tp:.2f} dBTP')
        dc = float(np.abs(x.mean(axis=1)).max())
        if dc > 0.002:
            rep.fail(what, f'DC offset {dc:.4f}')
        # dropouts: 100 ms windows below -60 dBFS RMS
        w = int(0.1 * SR)
        k = n // w
        mono = x.mean(axis=0)
        rms_db = 20 * np.log10(np.sqrt(np.mean(mono[:k * w].reshape(k, w) ** 2, axis=1)) + 1e-12)
        silent = rms_db < -60
        if not loop:
            silent[-int(1.5 * 10):] = False   # a one-shot may fade to silence at the end
        run = best = 0
        for s in silent:
            run = run + 1 if s else 0
            best = max(best, run)
        if best * 0.1 > 0.5:
            rep.fail(what, f'{best * 0.1:.1f}s dropout (silence) inside the track')
        corr = float(np.corrcoef(x[0], x[1])[0, 1])
        if corr < 0.0:
            rep.warn(what, f'L/R correlation {corr:.2f} (phase problems)')
        if corr > 0.995:
            rep.warn(what, 'effectively mono')
        cen, bands = octave_bands(mono)
        if bands[-1] > -14:
            rep.warn(what, f'16 kHz band at {bands[-1]:.1f} dB (hiss)')
        if bands[0] > -8:
            rep.warn(what, f'31 Hz band at {bands[0]:.1f} dB (rumble)')
        if max(bands) - bands[5] > 14:
            rep.warn(what, 'mids (1 kHz) buried more than 14 dB under the strongest band')
        seam = ''
        if loop:
            m = seam_metrics(x)
            if m['jump'] > max(3.0 * m['local'], 0.01):
                rep.fail(what, f'loop seam step {m["jump"]:.4f} vs local max {m["local"]:.4f} (click)')
            if m['env_drop'] > max(m['env_p995'], 3.0):
                rep.fail(what, f'loop seam level drop {m["env_drop"]:.1f} dB (cut tail?)')
            if m['seam_d'] > m['spec_p95'] * 1.25:
                rep.fail(what, f'loop seam spectral change {m["seam_d"]:.3f} vs typical {m["spec_p95"]:.3f}')
            if m['hf_ratio'] > 2.0:
                rep.fail(what, f'loop seam high-frequency burst x{m["hf_ratio"]:.1f} of onsets (click)')
            seam = (f'seam: step {m["jump"]:.4f}/local {m["local"]:.4f} drop {m["env_drop"]:+.1f} dB '
                    f'spec {m["seam_d"]:.2f}/{m["spec_p95"]:.2f} hf x{m["hf_ratio"]:.2f}')
        rep.line(f'{name:18} {dur:6.2f}s {n:8d} smp  {size / 1024:6.0f} KiB  I {lufs:6.1f} LUFS  '
                 f'LRA {lra if lra is not None else float("nan"):4.1f}  TP {tp if tp is not None else float("nan"):5.1f}  '
                 f'peak {pk:5.2f}  corr {corr:4.2f}  {seam}')
        rep.line(' ' * 18 + ' octave bands dB: ' + ' '.join(f'{c:g}:{b:.0f}' for c, b in zip(cen, bands)))
    rep.line(f'total music size {total / 1024 / 1024:.2f} MB')
    if total > 12 * 1024 * 1024:
        rep.fail('music', f'total size {total / 1024 / 1024:.1f} MB > 12 MB')


# --------------------------------------------------------------------------- fonts

def check_fonts(rep):
    rep.line('\n== fonts (assets/fonts) ==')
    fdir = os.path.join(ASSETS, 'fonts')
    for name, role in FONTS.items():
        path = os.path.join(fdir, name)
        what = f'fonts/{name}'
        if not os.path.exists(path):
            rep.fail(what, 'missing (run tools/assetgen/audio/fetch_fonts.py)')
            continue
        errors, warnings, info = check_font(path, role)
        for e in errors:
            rep.fail(what, e)
        for w in warnings:
            rep.warn(what, w)
        rep.line(f'{name:26} {os.path.getsize(path):7d} B  family "{info.get("family", "?")}"  '
                 f'{info.get("glyphs", 0)} mapped chars  variable={info.get("variable")}  role={role}')
    for name in LICENCES:
        path = os.path.join(fdir, name)
        if not os.path.exists(path):
            rep.fail(f'fonts/{name}', 'missing')
            continue
        with open(path, encoding='utf-8', errors='replace') as f:
            if 'SIL OPEN FONT LICENSE' not in f.read().upper():
                rep.fail(f'fonts/{name}', 'not an SIL OFL text')
        rep.line(f'{name:26} {os.path.getsize(path):7d} B  SIL OFL 1.1')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--no-fonts', action='store_true')
    ap.add_argument('--no-music', action='store_true')
    ap.add_argument('--no-sounds', action='store_true')
    args = ap.parse_args()
    rep = Report(args.quiet)
    if not args.no_sounds:
        check_sounds(rep)
    if not args.no_music:
        check_music(rep)
    if not args.no_fonts:
        check_fonts(rep)
    print()
    for w in rep.warns:
        print('WARN ', w)
    for f in rep.fails:
        print('FAIL ', f)
    print(f'\n{len(rep.fails)} failure(s), {len(rep.warns)} warning(s)')
    sys.exit(1 if rep.fails else 0)


if __name__ == '__main__':
    main()
