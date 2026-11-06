#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LFO's BANK in host mode (tests/host.c): with only WIND going in, each band's pitch as measured in the audio, in each
MODE and at TUNINGs; its ring dies to silence; no clipping at its widest; GLACIER glides; a save and a power-on keep it
    python3 tests/bank_test.py build/host/grimm"""
import os
import re
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/banktest")
OUT.mkdir(parents=True, exist_ok=True)
EDOS = [5, 7, 10, 12, 14, 15, 16, 17, 19, 22, 24, 31]
SEL, LFO, K1, K2, K3, K4 = 3, 5, 2, 3, 4, 5
bad = 0


def check(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + "bank: " + what)
    bad += 0 if ok else 1


def run(name, ev, end, env=None):
    s = OUT / f"{name}.txt"
    s.write_text("".join(f"{t} {w}\n" for t, w in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    return subprocess.run([BIN, str(OUT / name), str(s)], capture_output=True, text=True, env=env).stdout


def audio(name, a, b):
    w = wave.open(str(OUT / f"{name}.wav"))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float).reshape(-1, w.getnchannels()).mean(axis=1)
    return x[int(a * 44100):int(b * 44100)]


def peak_cents(x, hz, span=70):                 # (an average of windows: noise through a narrow band wanders)
    N = 1 << 17
    S = 0
    for i in range(0, len(x) - N, N // 2):
        S = S + np.abs(np.fft.rfft((x[i:i + N] - x[i:i + N].mean()) * np.hanning(N), 1 << 19)) ** 2
    fr = np.fft.rfftfreq(1 << 19, 1 / 44100)
    m = (fr > hz * 2 ** (-span / 1200)) & (fr < hz * 2 ** (span / 1200))
    return 1200 * np.log2(fr[np.flatnonzero(m)[np.argmax(S[m])]] / hz)


def hz(s):
    return 440 * 2 ** ((s - 69) / 12)


def setup(mode, ring=127, mix=127, wind=127, edo=12, glacier=0):
    ev = [(100, f"btn {SEL}"), (140, f"enc 4 {EDOS.index(edo) - 3}"),      # SCALE: TUNING
          (200, f"down {SEL}"), (240, "btn 1"), (280, f"up {SEL}"),       # LATCH off
          (340, f"btn {LFO}"), (380, f"btn {LFO}"), (420, f"btn {LFO}"),   # BANK
          (460, f"enc {K1} {mode}")]
    ev += [(500, f"enc {K2} {ring - 70}"), (520, f"enc {K3} {mix - 90}"), (540, f"enc {K4} {wind}")]
    if glacier:
        ev += [(580, f"btn {LFO}"), (620, f"enc {K1} {glacier}")]
    return ev


def note(n, a, b):
    return [(a, f"midi 0x90 {n} 100"), (b, f"midi 0x80 {n} 0")]


OFF = 0.0                                       # (440 Hz is 440 Hz: VA's few cents in tuning_test are its DETUNE)


def pitches(name, mode, n, wants, edo=12):
    run(name, setup(mode, edo=edo) + note(n, 800, 1000), 40000)
    x = audio(name, 2.5, 40.0)
    worst = max(abs(peak_cents(x, f) - OFF) for f in wants)
    check(worst < 2, f"{name}: bands within {worst:.2f} cents of the voices' tuning")


pitches("chord12", 1, 60, [hz(60), hz(72), hz(84), hz(96)])
pitches("chord19", 1, 55, [hz(48 + 7 * 12 / 19)], 19)
pitches("chord24", 1, 55, [hz(48 + 7 / 2)], 24)
pitches("harm12", 2, 48, [hz(48) * h for h in (1, 2, 3, 5)])
pitches("harm19", 2, 55, [hz(48 + 7 * 12 / 19) * h for h in (1, 2, 3)], 19)
pitches("ghost12", 3, 60, [hz(60.5)])
pitches("ghost24", 3, 60, [hz(48 + 12 / 2 + 0.5)], 24)

# off: nothing; on with no WIND: a ring, then exact silence
run("ring", setup(1, ring=127, wind=0) + note(60, 800, 1000), 16000)
tail, end = audio("ring", 1.5, 2.0), audio("ring", 15.0, 16.0)
check(np.sqrt((tail ** 2).mean()) > 5, f"no WIND: it rings after the note (rms {np.sqrt((tail ** 2).mean()):.0f})")
check(np.abs(end).max() == 0, f"no WIND: then silence ({np.abs(end).max():.0f})")

# level: narrowest and widest, four notes held hard: no clipping, and it does not vanish
for r in (0, 127):
    ev = setup(1, ring=r, wind=127)
    for i, n in enumerate((48, 55, 60, 64)):
        ev.append((800 + i * 20, f"midi 0x90 {n} 127"))
    run(f"lvl{r}", ev, 4000)
    x = audio(f"lvl{r}", 1.5, 4.0)
    check(np.abs(x).max() < 32767, f"RING {r}, four notes, WIND full: no clipping (peak {np.abs(x).max():.0f})")
    check(np.sqrt((x ** 2).mean()) > 300, f"RING {r}: audible (rms {np.sqrt((x ** 2).mean()):.0f})")
run("wind", setup(1, ring=70, wind=127) + note(60, 800, 1000), 4000)
w = np.sqrt((audio("wind", 2.0, 4.0) ** 2).mean())
run("wind0", setup(1, ring=127, wind=127) + note(60, 800, 1000), 4000)
w2 = np.sqrt((audio("wind0", 2.0, 4.0) ** 2).mean())
check(100 < w < 12000 and 100 < w2 < 12000, f"WIND alone sounds at a usable level (rms {w:.0f}, {w2:.0f})")

# GLACIER: 127, C3 to C4: not there at 0.5 s, there at ~20 s
ev = setup(1, glacier=127) + note(48, 800, 1000) + note(60, 3000, 3200)
run("gl", ev, 26000)
early, late = audio("gl", 3.2, 3.7), audio("gl", 22.0, 26.0)
S = lambda x, f: np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 18))[int(f * (1 << 18) / 44100)]
check(S(late, hz(60)) > 5 * S(late, hz(48)) and abs(peak_cents(late, hz(60)) - OFF) < 3, "GLACIER: at the new pitch in the end")
check(S(early, hz(60)) < S(late, hz(60)) * (len(early) / len(late)) * 0.6, "GLACIER: not there yet after half a second")
print(f"        (levels: WIND rms {w:.0f} at RING 70, {w2:.0f} at 127)")

# saved
sv = OUT / "save.bin"
run("save", setup(3, ring=100, mix=120, wind=40, glacier=50), 5000, {**os.environ, "GRIMM_SAVE_OUT": str(sv)})
p = run("load", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": str(sv)})
m = re.search(r" bank ([\d,]+)", p)
check(m and m.group(1) == "3,100,120,40,50", f"saved, power on: {m and m.group(1)}")
p = run("old", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": "tests/data/a1.2-saves.bin"})
m = re.search(r" bank ([\d,]+)", p)
check(m and m.group(1) == "0,0,0,0,0", f"a project from before it: {m and m.group(1)}")

print("bank test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
