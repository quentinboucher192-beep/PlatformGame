"""Effects and analysis: FFT filters, state-variable filter, reverb, delay, limiter, loudness.

Static filters are applied in the frequency domain with *causal* analog-prototype responses.
Every effect can run ``circular=True`` so that a buffer that represents one loop period stays
perfectly periodic (tails wrap into the start) - this is how the music loops stay seamless.
"""
import numpy as np

from synth import SR, curve, ns

SQ2 = np.sqrt(2.0)


# --------------------------------------------------------------------------- utilities

def db(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-12))


def undb(d):
    return 10.0 ** (np.asarray(d, dtype=float) / 20.0)


def peak_db(x):
    return float(db(np.max(np.abs(x)))) if np.size(x) else -240.0


def rms_db(x):
    return float(10.0 * np.log10(np.mean(np.asarray(x) ** 2) + 1e-24))


def fast_len(n):
    """Smallest m >= n whose only prime factors are 2, 3, 5 (fast FFT sizes)."""
    m = max(int(n), 1)
    while True:
        k = m
        for p in (2, 3, 5):
            while k % p == 0:
                k //= p
        if k == 1:
            return m
        m += 1


def mix(*parts):
    """Sum mono arrays of different lengths (zero padded)."""
    n = max(len(p) for p in parts)
    out = np.zeros(n)
    for p in parts:
        out[:len(p)] += p
    return out


def place(parts, total=None):
    """[(time_seconds, array), ...] -> one mono array."""
    end = max(ns(t) + len(a) for t, a in parts)
    out = np.zeros(total if total else end)
    for t, a in parts:
        s = ns(t)
        e = min(len(out), s + len(a))
        if e > s:
            out[s:e] += a[:e - s]
    return out


def pad_to(x, n):
    return x[:n] if len(x) >= n else np.concatenate([x, np.zeros(n - len(x))])


def mix_into(dst, src, pos, wrap):
    """Add src (C, m) into dst (C, L) at sample pos; wrap around for loops, else truncate."""
    L = dst.shape[-1]
    m = src.shape[-1]
    if wrap:
        pos %= L
        off = 0
        while off < m:
            chunk = min(m - off, L - pos)
            dst[..., pos:pos + chunk] += src[..., off:off + chunk]
            off += chunk
            pos = 0
    else:
        if pos >= L or pos + m <= 0:
            return
        s0 = max(0, -pos)
        e = min(m, L - pos)
        dst[..., pos + s0:pos + e] += src[..., s0:e]


def fade(x, fin=0.0, fout=0.0):
    """Raised-cosine fade in/out on the last axis (returns a copy)."""
    y = np.array(x, dtype=float, copy=True)
    n = y.shape[-1]
    a = min(n, ns(fin))
    if a > 0:
        y[..., :a] *= 0.5 - 0.5 * np.cos(np.pi * (np.arange(a) + 0.5) / a)
    b = min(n, ns(fout))
    if b > 0:
        y[..., n - b:] *= 0.5 + 0.5 * np.cos(np.pi * (np.arange(b) + 0.5) / b)
    return y


def pan(mono, p=0.0):
    """Constant-power pan, centre = unity on both channels. p in [-1, 1]."""
    th = (np.clip(p, -1.0, 1.0) + 1.0) * np.pi / 4.0
    return np.vstack([mono * np.cos(th) * SQ2, mono * np.sin(th) * SQ2])


def soft_clip(x, drive=1.0):
    if drive <= 0:
        return x
    return np.tanh(drive * x) / np.tanh(drive)


# --------------------------------------------------------------------------- static filters (FFT)

def lp(fc, q=0.7071):
    def r(f):
        x = f / fc
        return 1.0 / (1.0 - x * x + 1j * x / q)
    return r


def hp(fc, q=0.7071):
    def r(f):
        x = f / fc
        return -x * x / (1.0 - x * x + 1j * x / q)
    return r


def bp(fc, q=1.0):
    def r(f):
        x = f / fc
        return (1j * x / q) / (1.0 - x * x + 1j * x / q)
    return r


def lp1(fc):
    return lambda f: 1.0 / (1.0 + 1j * f / fc)


def hp1(fc):
    return lambda f: (1j * f / fc) / (1.0 + 1j * f / fc)


def peak(fc, q, gain_db):
    A = 10.0 ** (gain_db / 40.0)

    def r(f):
        s = 1j * f / fc
        return (s * s + s * A / q + 1.0) / (s * s + s / (A * q) + 1.0)
    return r


def lowshelf(fc, gain_db, q=0.7071):
    A = 10.0 ** (gain_db / 40.0)

    def r(f):
        s = 1j * f / fc
        return A * (s * s + np.sqrt(A) / q * s + A) / (A * s * s + np.sqrt(A) / q * s + 1.0)
    return r


def highshelf(fc, gain_db, q=0.7071):
    A = 10.0 ** (gain_db / 40.0)

    def r(f):
        s = 1j * f / fc
        return A * (A * s * s + np.sqrt(A) / q * s + 1.0) / (s * s + np.sqrt(A) / q * s + A)
    return r


def filt(x, *responses, circular=False, tail=0.0):
    """Apply a chain of analog-prototype responses via FFT. Works on (n,) or (C, n).

    Non-circular: zero padded (linear convolution); output keeps the input length plus `tail`.
    """
    x = np.asarray(x, dtype=float)
    n = x.shape[-1]
    if n == 0 or not responses:
        return x.copy()
    if circular:
        N = n
        keep = n
    else:
        keep = n + ns(tail)
        N = fast_len(keep + max(4096, n // 4))
    X = np.fft.rfft(x, n=N, axis=-1)
    f = np.fft.rfftfreq(N, 1.0 / SR)
    H = np.ones_like(f, dtype=complex)
    for r in responses:
        H = H * r(f)
    H[0] = H[0].real
    y = np.fft.irfft(X * H, n=N, axis=-1)
    return y[..., :keep]


# --------------------------------------------------------------------------- time-varying SVF

def svf(x, cutoff, q=0.7071, mode='lp', circular=False):
    """Zero-delay-feedback state-variable filter with per-sample cutoff/Q (Python loop).

    mode: 'lp', 'bp' (unity peak), 'hp', 'notch'. circular=True pre-rolls the loop tail so the
    filter state at the start matches the end (periodic output).
    """
    x = np.asarray(x, dtype=float)
    n = len(x)
    fc = np.clip(curve(cutoff, n), 5.0, 0.45 * SR)
    qq = np.maximum(curve(q, n), 0.05)
    pre = 0
    if circular:
        pre = min(n, ns(0.5))
        x = np.concatenate([x[n - pre:], x])
        fc = np.concatenate([fc[n - pre:], fc])
        qq = np.concatenate([qq[n - pre:], qq])
    g = np.tan(np.pi * fc / SR)
    k = 1.0 / qq
    a1 = 1.0 / (1.0 + g * (g + k))
    a2 = g * a1
    a3 = g * a2
    lo = [0.0] * len(x)
    bd = [0.0] * len(x)
    ic1 = 0.0
    ic2 = 0.0
    for i, (v0, A1, A2, A3) in enumerate(zip(x.tolist(), a1.tolist(), a2.tolist(), a3.tolist())):
        v3 = v0 - ic2
        v1 = A1 * ic1 + A2 * v3
        v2 = ic2 + A2 * ic1 + A3 * v3
        ic1 = 2.0 * v1 - ic1
        ic2 = 2.0 * v2 - ic2
        lo[i] = v2
        bd[i] = v1
    lo = np.array(lo[pre:])
    bd = np.array(bd[pre:])
    x = x[pre:]
    k = k[pre:]
    if mode == 'lp':
        return lo
    if mode == 'bp':
        return k * bd
    if mode == 'hp':
        return x - k * bd - lo
    if mode == 'notch':
        return x - k * bd
    raise ValueError(mode)


# --------------------------------------------------------------------------- reverb / delay

def reverb_ir(seconds, t60, rng, lf_ratio=1.15, hf_ratio=0.35, predelay=0.012, early=6,
              buildup=0.01, stereo=True):
    """Synthetic stereo impulse response: decorrelated noise with frequency-dependent decay."""
    n = ns(seconds)
    t = np.arange(n) / SR
    C = 2 if stereo else 1
    noise = rng.standard_normal((C, n))
    X = np.fft.rfft(noise, axis=-1)
    f = np.fft.rfftfreq(n, 1.0 / SR)
    lf = np.log2(np.maximum(f, 1.0))
    edges = [180.0, 700.0, 2200.0, 5500.0, 11000.0]
    U = [1.0 / (1.0 + np.exp(-(lf - np.log2(e)) / 0.35)) for e in edges]
    masks = [1.0 - U[0]] + [U[i] - U[i + 1] for i in range(len(U) - 1)] + [U[-1]]
    ratios = np.geomspace(lf_ratio, hf_ratio, len(masks))
    ir = np.zeros((C, n))
    for m, rr in zip(masks, ratios):
        band = np.fft.irfft(X * m, n=n, axis=-1)
        ir += band * 10.0 ** (-3.0 * t / (t60 * rr))
    ir *= 1.0 - np.exp(-t / max(buildup, 1e-4))
    energy = np.sqrt(np.mean(np.sum(ir ** 2, axis=1)))
    for e in range(early):
        tt = 0.006 + 0.055 * (e + rng.random()) / max(early, 1)
        i = ns(tt)
        if i < n:
            for c in range(C):
                side = 1.0 if (e + c) % 2 else 0.55
                ir[c, i] += 0.16 * energy * (1.0 - 0.1 * e) * side * rng.choice([-1.0, 1.0])
    pd = ns(predelay)
    if pd:
        ir = np.concatenate([np.zeros((C, pd)), ir[:, :n - pd]], axis=1)
    ir /= np.sqrt(np.sum(ir ** 2, axis=1, keepdims=True)) + 1e-12
    return ir if stereo else ir[0]


def convolve(x, ir, circular=False):
    """Mono x convolved with mono or stereo IR (FFT). Output length = len(x) (circular) or
    len(x) + len(ir) - 1."""
    n = len(x)
    m = ir.shape[-1]
    if circular:
        if m > n:
            ir = ir[..., :n]
        N = n
        out_len = n
    else:
        out_len = n + m - 1
        N = fast_len(out_len)
    X = np.fft.rfft(x, n=N)
    H = np.fft.rfft(ir, n=N, axis=-1)
    y = np.fft.irfft(X * H, n=N, axis=-1)
    return y[..., :out_len]


def pingpong(x, delay_s, feedback=0.4, damp_hz=4000.0, circular=False, tail=0.0):
    """Ping-pong feedback delay with damping in the loop, computed in closed form in the
    frequency domain (exact infinite feedback; circular=True keeps a loop periodic).
    Returns stereo wet signal: first echo left, second right, ..."""
    n = len(x)
    if circular:
        N = n
        keep = n
    else:
        keep = n + ns(tail)
        N = fast_len(keep + ns(delay_s) * 4)
    X = np.fft.rfft(x, n=N)
    k = np.arange(len(X))
    f = k * SR / N
    D = ns(delay_s)
    z = np.exp(-2j * np.pi * k * D / N)
    H = 1.0 / (1.0 + 1j * f / damp_hz)
    hz = H * z
    common = 1.0 / (1.0 - (feedback * hz) ** 2)
    YL = X * hz * common
    YR = X * feedback * hz * hz * common
    y = np.vstack([np.fft.irfft(YL, n=N), np.fft.irfft(YR, n=N)])
    return y[:, :keep]


def echo(x, delay_s, feedback=0.35, damp_hz=5000.0, tail=None):
    """Simple mono feedback echo for sound effects (non-circular, keeps the tail)."""
    if tail is None:
        tail = delay_s * np.log(0.003) / np.log(max(feedback, 1e-3))
    y = pingpong(x, delay_s, feedback, damp_hz, circular=False, tail=tail)
    return y.mean(axis=0) * SQ2


def chorus(x, rate=0.6, depth_ms=3.0, base_ms=12.0, phase0=0.0, circular=False):
    """Single modulated delay tap (mono -> mono). Mix with the dry signal outside."""
    n = len(x)
    t = np.arange(n)
    if circular:
        rate = max(1, round(rate * n / SR)) * SR / n  # integer cycles per loop
    d = (base_ms + depth_ms * np.sin(2 * np.pi * (rate * t / SR + phase0))) * SR / 1000.0
    idx = t - d
    if circular:
        idx = idx % n
        xp = np.concatenate([x, x[:1]])
        i0 = np.floor(idx).astype(np.int64)
        fr = idx - i0
        return xp[i0] * (1 - fr) + xp[i0 + 1] * fr
    return np.interp(idx, t, x, left=0.0)


# --------------------------------------------------------------------------- dynamics

def sliding_min(x, L):
    """out[i] = min(x[i:i+L]); len(out) = len(x) - L + 1 (sparse-table doubling)."""
    M = x
    P = 1
    while P * 2 <= L:
        M = np.minimum(M[:-P], M[P:])
        P *= 2
    m = len(x) - L + 1
    return np.minimum(M[:m], M[L - P:L - P + m])


def _box(x, h, circular):
    L = 2 * h + 1
    xp = np.concatenate([x[-h:], x, x[:h]]) if circular else np.pad(x, h, mode='edge')
    c = np.concatenate([[0.0], np.cumsum(xp)])
    return (c[L:] - c[:-L]) / L


def limiter_gain(x, ceiling_db=-1.0, window=0.01, circular=False):
    """Gain curve (n,) that keeps |x| below the ceiling; smooth, look-ahead, never overshoots."""
    xs = np.atleast_2d(x)
    pk = np.max(np.abs(xs), axis=0)
    thr = 10.0 ** (ceiling_db / 20.0)
    r = np.minimum(1.0, thr / np.maximum(pk, 1e-12))
    if np.all(r >= 1.0):
        return np.ones_like(r)
    w = max(1, ns(window))
    rp = np.concatenate([r[-w:], r, r[:w]]) if circular else np.pad(r, w, mode='edge')
    m = sliding_min(rp, 2 * w + 1)
    return np.minimum(_box(m, w, circular), 1.0)


def limit(x, ceiling_db=-1.0, window=0.01, circular=False):
    g = limiter_gain(x, ceiling_db, window, circular)
    y = np.asarray(x) * g
    thr = 10.0 ** (ceiling_db / 20.0)
    return np.clip(y, -thr, thr)


def envelope_follow(x, window=0.01):
    """RMS envelope (non-causal box) for analysis/ducking."""
    w = max(1, ns(window))
    return np.sqrt(_box(np.asarray(x) ** 2, w, False))


# --------------------------------------------------------------------------- loudness (BS.1770)

def _biquad_power(b, a, f):
    w = 2.0 * np.pi * f / SR
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    num = b[0] + b[1] * z1 + b[2] * z2
    den = a[0] + a[1] * z1 + a[2] * z2
    return np.abs(num / den) ** 2


def k_weight_power(f):
    """|H_K(f)|^2 for BS.1770 K-weighting (libebur128 derivation for arbitrary sample rates)."""
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / SR)
    Vh = 10.0 ** (G / 20.0)
    Vb = Vh ** 0.4996667741545416
    a0 = 1.0 + K / Q + K * K
    b = [(Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a = [1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    p1 = _biquad_power(b, a, f)
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / SR)
    a0 = 1.0 + K / Q + K * K
    a = [1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0]
    return p1 * _biquad_power([1.0, -2.0, 1.0], a, f)


def k_weighted(x, circular=False):
    x = np.atleast_2d(np.asarray(x, dtype=float))
    n = x.shape[-1]
    N = n if circular else fast_len(n + 8192)
    X = np.fft.rfft(x, n=N, axis=-1)
    f = np.fft.rfftfreq(N, 1.0 / SR)
    y = np.fft.irfft(X * np.sqrt(k_weight_power(f)), n=N, axis=-1)
    return y[:, :n]


def integrated_lufs(x, circular=False):
    y = k_weighted(x, circular)
    n = y.shape[-1]
    blk = ns(0.4)
    hop = ns(0.1)
    if n < blk:
        z = np.sum(np.mean(y ** 2, axis=1))
        return float(-0.691 + 10 * np.log10(z + 1e-24))
    c = np.concatenate([np.zeros((y.shape[0], 1)), np.cumsum(y ** 2, axis=1)], axis=1)
    starts = np.arange(0, n - blk + 1, hop)
    z = np.sum((c[:, starts + blk] - c[:, starts]) / blk, axis=0)
    lk = -0.691 + 10 * np.log10(z + 1e-24)
    z1 = z[lk > -70.0]
    if len(z1) == 0:
        return -70.0
    rel = -0.691 + 10 * np.log10(np.mean(z1)) - 10.0
    z2 = z[(lk > -70.0) & (lk > rel)]
    return float(-0.691 + 10 * np.log10(np.mean(z2) + 1e-24))


def max_momentary(x, window=0.1):
    """Max K-weighted loudness over sliding windows (used to level short sound effects)."""
    y = k_weighted(x)
    n = y.shape[-1]
    w = min(n, ns(window))
    c = np.concatenate([np.zeros((y.shape[0], 1)), np.cumsum(y ** 2, axis=1)], axis=1)
    z = np.sum((c[:, w:] - c[:, :-w]) / w, axis=0)
    return float(-0.691 + 10 * np.log10(np.max(z) + 1e-24))
