#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SEL > SCALE's TUNING in host mode (tests/host.c):
  - each EDO, each source: a note sounds at (note - anchor) x 1200 / EDO cents from 12 EDO's anchor, measured in the audio
  - ROOT moves the anchor; OCT moves a real octave; a chord's intervals are the nearest steps; a project's TUNING survives
    a save and a power-on; a project saved before TUNING loads at 12
    python3 tests/tuning_test.py build/host/grimm"""
import re
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/tuningtest")
OUT.mkdir(parents=True, exist_ok=True)
EDOS = [5, 7, 10, 12, 14, 15, 16, 17, 19, 22, 24, 31]
SEL, EDIT, K1, K3 = 3, 6, 2, 4
ENGINES = {"VA": 0, "FM": 1, "VOICE": 3, "WHEEL": 4, "PHASE": 6}
bad = 0


def check(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + "tuning: " + what)
    bad += 0 if ok else 1


def run(name, ev, end, env=None):
    s = OUT / f"{name}.txt"
    s.write_text("".join(f"{t} {w}\n" for t, w in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    r = subprocess.run([BIN, str(OUT / name), str(s)], capture_output=True, text=True, env=env)
    return r.stdout


def setup(eng, edo, root=0):
    ev = [(200, f"btn {EDIT}"), (240, f"btn {EDIT}"), (280, f"enc {K1} -20"), (320, f"enc {K1} {eng}"),
          (400, f"btn {SEL}"), (440, f"enc {K3} {EDOS.index(edo) - EDOS.index(12)}")]
    if root:
        ev.append((480, f"enc {K1} {root}"))
    return ev


def f0(name, a=1.0, b=2.0):
    w = wave.open(str(OUT / f"{name}.wav"))
    n = w.getnchannels()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    x = x.reshape(-1, n).mean(axis=1)[int(a * 44100):int(b * 44100)]
    x = (x - x.mean()) * np.hanning(len(x))
    c = np.fft.irfft(np.abs(np.fft.rfft(x, 2 * len(x))) ** 2)[:44100 // 15]
    c /= c[0]
    lo = int(44100 / 4000)
    k = lo + int(np.argmax(c[lo:]))
    peaks = [i for i in range(lo, len(c) - 1) if c[i] > c[i - 1] and c[i] >= c[i + 1] and c[i] > 0.85 * c[k]]
    k = peaks[0] if peaks else k
    d = (c[k - 1] - c[k + 1]) / (2 * (c[k - 1] - 2 * c[k] + c[k + 1]))
    return 44100 / (k + d)


def hz(semis):
    return 440 * 2 ** ((semis - 69) / 12)


# each EDO, each source: notes around the anchor (C3 = 48)
worst = 0
off = {}
for en, e in ENGINES.items():
    for edo in [12] + [x for x in EDOS if x != 12]:   # (12 EDO first: each source's own offset, as it always had it)
        for n in (48, 55):
            name = f"p{en}{edo}n{n}"
            run(name, setup(e, edo) + [(600, f"midi 0x90 {n} 100"), (2600, f"midi 0x80 {n} 0")], 2700)
            c = 1200 * np.log2(f0(name) / hz(48 + (n - 48) * 12 / edo))
            c = (c + 600) % 1200 - 600          # (the measure may read an octave off)
            off.setdefault(en, c)
            c -= off[en]
            worst = max(worst, abs(c))
            if abs(c) >= 1.5:
                check(False, f"{en} {edo} EDO note {n}: {c:+.2f} cents")
check(worst < 1.5, f"{len(ENGINES)} sources x {len(EDOS)} EDOs: every note within {worst:.2f} cents of its pitch (against 12 EDO)")

# ROOT moves the anchor: D (2) keeps note 50 as D3
run("root", setup(0, 19, 2) + [(600, "midi 0x90 50 100"), (2600, "midi 0x80 50 0")], 2700)
c = (1200 * np.log2(f0("root") / hz(50)) + 600) % 1200 - 600
c -= off["VA"]
check(abs(c) < 1.5, f"ROOT D, 19 EDO: note 50 stays D3 ({c:+.2f} cents)")


def notes(p):
    return sorted(int(x.split(":")[1]) for x in re.search(r"notes((?: \d:\d+)*)", p).group(1).split())


# OCT: 19 steps; 12 EDO's 12
K0 = 41
p = run("oct", setup(0, 19) + [(600, "btn 1"), (700, "keys 0x1"), (900, "probe"), (1000, "keys 0")], 1100)
check(notes(p) == [K0 + 19], f"19 EDO, OCT+: the first key is note {K0 + 19}: {notes(p)}")
p = run("oct12", setup(0, 12) + [(600, "btn 1"), (700, "keys 0x1"), (900, "probe"), (1000, "keys 0")], 1100)
check(notes(p) == [K0 + 12], f"12 EDO, OCT+: the first key is note {K0 + 12}: {notes(p)}")

# a chord: min (0 3 7) at 24 EDO is 0 6 14; at 19: 0 5 11 (the nearest), hold SEL for chord mode
chord = [(500, f"down {SEL}"), (1200, f"up {SEL}"), (1300, "midi 0x90 60 100"), (1600, "probe"), (1700, "midi 0x80 60 0")]
p = run("ch24", setup(0, 24) + chord, 1800)
check(notes(p) == [60, 66, 74], f"24 EDO chord: {notes(p)}")
p = run("ch19", setup(0, 19) + chord, 1800)
check(notes(p) == [60, 65, 71], f"19 EDO chord: {notes(p)}")
p = run("ch12", setup(0, 12) + chord, 1800)
check(notes(p) == [60, 63, 67], f"12 EDO chord: {notes(p)}")

# a scale set: ignored while TUNING is on (no note snapped), back on at 12
p = run("sc", setup(0, 19) + [(480, "enc 3 2"), (600, "midi 0x90 61 100"), (900, "probe"), (950, "midi 0x80 61 0")], 1000)
check(notes(p) == [61], f"19 EDO with MAJOR set: note 61 is not snapped: {notes(p)}")
p = run("sc12", setup(0, 12) + [(480, "enc 3 2"), (600, "midi 0x90 61 100"), (900, "probe"), (950, "midi 0x80 61 0")], 1000)
check(notes(p) == [60], f"12 EDO with MAJOR set: note 61 snaps to 60: {notes(p)}")

# saved and loaded
import os
sv = OUT / "save.bin"
run("save", setup(0, 19), 5000, {**os.environ, "GRIMM_SAVE_OUT": str(sv)})
p = run("load", [(300, f"btn {SEL}"), (400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": str(sv)})
m = re.search(r" tuning (\d+)", p)
check(m and m.group(1) == "19", f"19 EDO saved, power on: TUNING {m and m.group(1)}")
p = run("old", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": "tests/data/a1.2-saves.bin"})
m = re.search(r" tuning (\d+)", p)
check(m and m.group(1) == "12", f"a project from before TUNING loads at {m and m.group(1)}")

print("tuning test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
