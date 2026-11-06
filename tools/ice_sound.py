#!/usr/bin/env python3
"""The sound of the Ultra graphics' frozen water breaking (effects.cpp FxIceBreak):

    python3 tools/ice_sound.py [data/sound]

writes icecrack1.wav, icecrack2.wav and icecrack3.wav (44.1 kHz, mono, 16 bits),
synthesized here from nothing but noise and sines, so they are this project's own
(LGPL like the rest). Each one: the sharp crack (a few clicks of bright noise), the
"pew" of a sheet of ice (a falling chirp: high frequencies travel faster through
it), the crunch of the pieces (short high pings) and a little water at the end.
The same seed gives the same files. Needs numpy.
"""
import os
import sys
import wave

import numpy as np

RATE = 44100


def env(n, attack, decay):
    t = np.arange(n) / RATE
    return np.minimum(t / max(attack, 1e-4), 1.0) * np.exp(-t / decay)


def highpass(x, cutoff):
    a = np.exp(-2 * np.pi * cutoff / RATE)
    y = np.zeros_like(x)
    prev_x = prev_y = 0.0
    for i, v in enumerate(x):
        prev_y = a * (prev_y + v - prev_x)
        prev_x = v
        y[i] = prev_y
    return y


def lowpass(x, cutoff):
    a = 1 - np.exp(-2 * np.pi * cutoff / RATE)
    y = np.zeros_like(x)
    acc = 0.0
    for i, v in enumerate(x):
        acc += a * (v - acc)
        y[i] = acc
    return y


def crack(seed):
    rng = np.random.default_rng(seed)
    n = int(RATE * 0.9)
    out = np.zeros(n)
    # the crack: a burst of clicks of bright noise over 40 ms
    for _ in range(rng.integers(4, 8)):
        at = int(rng.uniform(0, 0.04) * RATE)
        m = int(RATE * rng.uniform(0.006, 0.02))
        click = highpass(rng.standard_normal(m), 1800) * env(m, 0.0004, rng.uniform(0.002, 0.006))
        out[at:at + m] += click * rng.uniform(0.6, 1.0)
    # the ice sheet's "pew": a chirp falling from ~4 kHz, two of them a little apart
    for k in range(2):
        at = int(RATE * (0.005 + 0.03 * k))
        m = int(RATE * rng.uniform(0.25, 0.4))
        t = np.arange(m) / RATE
        f0, f1 = rng.uniform(3000, 4500), rng.uniform(250, 450)
        f = f1 + (f0 - f1) * np.exp(-t / rng.uniform(0.04, 0.07))
        phase = 2 * np.pi * np.cumsum(f) / RATE
        out[at:at + m] += np.sin(phase) * env(m, 0.001, rng.uniform(0.05, 0.09)) * (0.35 if k == 0 else 0.2)
    # the pieces: short high pings, thinning out
    for _ in range(rng.integers(14, 24)):
        at = int(RATE * (0.02 + rng.exponential(0.12)))
        if at >= n - 2000:
            continue
        m = int(RATE * 0.03)
        t = np.arange(m) / RATE
        ping = np.sin(2 * np.pi * rng.uniform(2200, 6500) * t) * env(m, 0.0003, rng.uniform(0.004, 0.01))
        out[at:at + m] += ping * rng.uniform(0.05, 0.18)
    # a little water welling up: low, soft noise
    at = int(RATE * 0.06)
    m = n - at
    water = lowpass(rng.standard_normal(m), 700) * env(m, 0.04, 0.18)
    out[at:] += water * 0.25
    out /= np.max(np.abs(out)) / 0.89
    fade = int(RATE * 0.05)
    out[-fade:] *= np.linspace(1, 0, fade)
    return out


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "sound")
    for i in range(3):
        pcm = (crack(1000 + i) * 32767).astype("<i2")
        path = os.path.join(dst, "icecrack%d.wav" % (i + 1))
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(pcm.tobytes())
        print(path)


if __name__ == "__main__":
    main()
