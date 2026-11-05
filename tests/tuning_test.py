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

# a scale at a TUNING
def scale(edo, k, n):
    return notes(run(f"sc{edo}{k}{n}", setup(0, edo) + [(480, f"enc 3 {k}"), (600, f"midi 0x90 {n} 100"), (900, "probe"), (950, f"midi 0x80 {n} 0")], 1000))
check(scale(12, 2, 61) == [60], "12 EDO MAJOR: note 61 snaps to 60")
check(scale(19, 2, 61) == [62], "19 EDO MAJOR, converted: step 13 snaps to 14 (a major sixth)")
check(scale(19, 15, 50) == [49], "19 EDO KLEISMIC: step 2 snaps to 1")
check(scale(24, 15, 49) == [48], "24 EDO RAST: step 1 snaps to 0")
check(scale(24, 15, 55) == [55], "24 EDO RAST: step 7, the half-flat third, stays")
check(scale(24, 19, 54) == [54], "24 EDO MIXO ZONES: step 6, a quarter tone in the zone, stays")
check(scale(24, 19, 57) == [56], "24 EDO MIXO ZONES: step 9, outside the zones, snaps to 8")


# chord types: hold SEL for chord mode, then SEL again to CHORD (KNOB 1: its type)
def chord_of(edo, ty, sc=0, field=0, n=60):
    ev = setup(0, edo)
    if sc: ev.append((480, f"enc 3 {sc}"))
    if field: ev.append((490, "enc 5 1"))
    ev += [(500, f"down {SEL}"), (1200, f"up {SEL}"), (1300, f"btn {SEL}"), (1400, f"enc {K1} {ty}"),
           (1500, f"midi 0x90 {n} 100"), (1800, "probe"), (1900, f"midi 0x80 {n} 0")]
    return notes(run(f"cht{edo}{ty}{sc}{field}", ev, 2000))
for what, got, want in (
    ("12 EDO MAJOR, SCALE7", chord_of(12, 11, 2), [60, 64, 67, 71]),
    ("12 EDO, no scale, SCALE7", chord_of(12, 11), [60, 63, 67, 70]),
    ("12 EDO QUART3", chord_of(12, 12), [60, 65, 70]),
    ("12 EDO QUART4", chord_of(12, 13), [60, 65, 70, 75]),
    ("12 EDO QUINT", chord_of(12, 14), [60, 67, 74]),
    ("12 EDO HARM", chord_of(12, 15), [60, 64, 67, 70]),
    ("24 EDO HARM", chord_of(24, 15), [60, 68, 74, 79]),
    ("19 EDO HARM", chord_of(19, 15), [60, 66, 71, 75]),
    ("24 EDO QUART4", chord_of(24, 13), [60, 70, 80, 90]),
    ("24 EDO FIELD: HARM's odd step moves in", chord_of(24, 15, 0, 1), [60, 68, 74, 78]),
):
    check(got == want, f"{what}: {got}")


# FIELD
def field(edo, on, ns):
    ev, t = setup(0, edo), 600
    if on: ev.append((500, "enc 5 1"))
    for n in ns:
        ev.append((t, f"midi 0x90 {n} 100")); t += 100
    ev += [(t + 200, "probe")]
    return notes(run(f"f{edo}{on}" + "_".join(map(str, ns)), ev, t + 300))
check(field(24, 0, [60, 61]) == [60, 61], "24 EDO FIELD off: 60 and 61 sound together")
check(field(24, 1, [60, 61]) == [60, 60], "24 EDO FIELD on: 61 over 60 takes a step of 60's field")
check(field(24, 1, [60, 62]) == [60, 62], "24 EDO FIELD on: 62 over 60 is in its field")
check(field(24, 1, [61, 62]) == [61, 61], "24 EDO FIELD on: a quarter tone first sets the other field")
check(field(19, 1, [60, 61]) == [60, 61], "19 EDO: no fields, FIELD does nothing")
check(field(12, 1, [60, 61]) == [60, 61], "12 EDO: FIELD does nothing")
ev = setup(0, 24) + [(500, "enc 5 1"), (520, f"down {SEL}"), (560, "btn 1"), (600, f"up {SEL}"),   # (LATCH off: SEL + OCT+)
                     (700, "midi 0x90 60 100"), (900, "midi 0x80 60 0"), (1100, "midi 0x90 61 100"), (1300, "probe")]
p = run("fm", ev, 1400)
check(notes(p) == [61], f"24 EDO FIELD: nothing sounding, the next note may change field: {notes(p)}")
ev = setup(0, 24) + [(500, "enc 5 1"), (700, "midi 0x90 60 100"), (900, "midi 0x80 60 0"), (1100, "midi 0x90 61 100"), (1300, "probe")]
p = run("fl", ev, 1400)
check(60 in notes(p) and 61 not in notes(p), f"24 EDO FIELD, LATCH on: a latched note still holds the field: {notes(p)}")

# saved and loaded
import os
sv = OUT / "save.bin"
run("save", setup(0, 24) + [(480, "enc 3 15"), (490, "enc 5 1")], 5000, {**os.environ, "GRIMM_SAVE_OUT": str(sv)})
p = run("load", [(300, f"btn {SEL}"), (400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": str(sv)})
m = re.search(r" tuning (\d+) scale (\d+) field (\d+)", p)
check(m and m.groups() == ("24", "15", "1"), f"24 EDO, RAST, FIELD saved, power on: {m and m.groups()}")
p = run("old", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": "tests/data/a1.2-saves.bin"})
m = re.search(r" tuning (\d+)", p)
check(m and m.group(1) == "12", f"a project from before TUNING loads at {m and m.group(1)}")

print("tuning test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
