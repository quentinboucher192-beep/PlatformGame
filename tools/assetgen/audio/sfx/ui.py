"""Menu / HUD sounds: short, soft, tonal (A major / E so they sit with the music)."""
from sfx.core import sound
from sfx.kit import *  # noqa: F401,F403


def _ui_note(f, d, decay):
    n = ns(d)
    return (0.7 * sine(f, n) + 0.3 * triangle(f, n)) * perc(n, 0.0015, decay)


@sound('ui_move', -25, 'ui')
def ui_move(rng):
    a = _ui_note(1318.5, 0.06, 0.016)
    a += sine(2637.0, ns(0.06)) * perc(ns(0.06), 0.001, 0.006) * 0.12
    return a


@sound('ui_confirm', -21, 'ui', fade_out=0.03)
def ui_confirm(rng):
    a = _ui_note(880.0, 0.12, 0.04)
    b = _ui_note(1318.5, 0.22, 0.07)
    b += bell(2637.0, 0.22, ratio=2.0, index=0.8, decay=0.05) * 0.12
    return place([(0.0, a), (0.07, b)])


@sound('ui_back', -22, 'ui', fade_out=0.03)
def ui_back(rng):
    a = _ui_note(1318.5, 0.12, 0.04)
    b = _ui_note(880.0, 0.2, 0.06)
    return place([(0.0, a), (0.07, b)])


@sound('ui_error', -21, 'ui', fade_out=0.02)
def ui_error(rng):
    def buzz(d):
        n = ns(d)
        v = square(155.6, n, 0.5) + square(160.0, n, 0.35) * 0.8
        return filt(v, lp(1600, 0.8)) * adsr(n, 0.004, 0.02, 0.8, 0.02, d - 0.02)
    return place([(0.0, buzz(0.085) * 0.4), (0.115, buzz(0.11) * 0.4)])


@sound('timer_tick', -24, 'ui')
def timer_tick(rng):
    n = ns(0.06)
    wood = sine(1760, n) * perc(n, 0.0005, 0.012) + 0.4 * sine(2900, n) * perc(n, 0.0005, 0.005)
    tick = filt(white(n, rng), bp(3500, 2.0)) * perc(n, 0.0003, 0.002) * 0.3
    return wood + tick


@sound('timer_warning', -17, 'ui', fade_out=0.02)
def timer_warning(rng):
    def beep(d):
        n = ns(d)
        v = square(987.8, n, 0.5) * 0.5 + sine(987.8, n) * 0.6 + sine(1975.5, n) * 0.15
        return filt(v, lp(4500, 0.7)) * adsr(n, 0.003, 0.02, 0.85, 0.02, d - 0.02)
    return place([(0.0, beep(0.09)), (0.14, beep(0.09))])


@sound('score_tick', -27, 'ui')
def score_tick(rng):
    n = ns(0.045)
    return (sine(2093.0, n) + 0.25 * triangle(2093.0, n)) * perc(n, 0.0008, 0.009)


@sound('medal', -15, 'ui', fade_out=0.2)
def medal(rng):
    notes = ['D6', 'F#6', 'A6', 'D7']
    line = arp(notes, 0.075, lambda f, i: chime(f, 0.9, decay=0.3 + 0.08 * i, bright=0.7),
               vel=[0.6, 0.7, 0.8, 1.0])
    d = 1.2
    glints = sparkle(d, rng, count=26, fmin=4000, fmax=9500, density=[0.2, 1, 0.8, 0.3, 0.1]) * 0.3
    low = bell('D5', 1.0, ratio=1.0, index=1.0, decay=0.35) * 0.3
    x = place([(0.0, line * 0.4), (0.22, low), (0.05, glints)])
    return room(x, rng, t60=1.2, wet=0.28)
