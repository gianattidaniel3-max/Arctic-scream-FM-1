#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The DRUM source in host mode (tests/host.c): a hit, its tail past the note off, its level; KEYS and TUNING's pitches,
measured in the audio; DRUM 1 and 2 change the sound; WANDER and HUMAN; through BANK; saved and loaded
    python3 tests/drum_test.py build/host/grimm"""
import os, re, subprocess, sys, wave
from pathlib import Path
import numpy as np

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/drumtest"); OUT.mkdir(parents=True, exist_ok=True)
EDIT, SEL, LFO, K1, K2, K3, K4 = 6, 3, 5, 2, 3, 4, 5
EDOS = [5, 7, 10, 12, 14, 15, 16, 17, 19, 22, 24, 31]
bad = 0


def check(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + "drum: " + what)
    bad += 0 if ok else 1


def run(name, ev, end, env=None):
    s = OUT / f"{name}.txt"
    s.write_text("".join(f"{t} {w}\n" for t, w in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    return subprocess.run([BIN, str(OUT / name), str(s)], capture_output=True, text=True, env=env).stdout


def audio(name, a=0, b=None):
    w = wave.open(str(OUT / f"{name}.wav"))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float).reshape(-1, w.getnchannels()).mean(axis=1)
    return x[int(a * 44100):int(b * 44100) if b else None]


def rms(x):
    return float(np.sqrt((x ** 2).mean()))


MEMB, HAIL = 63, 84                                  # MODEL: KICK, SNARE, CLAP, MEMBRANE, HAIL, each 21 (its presets)


def src(model=MEMB, edo=12):
    ev = [(100, f"btn {EDIT}"), (140, f"btn {EDIT}"), (180, f"enc {K1} 8")]     # SOURCE: DRUM (SOUND after it)
    ev += [(220, f"btn {SEL}"), (240, f"enc 4 {EDOS.index(edo) - 3}"), (280, f"btn {EDIT}")]
    if model: ev.append((320, f"enc {K1} {model}"))
    return ev


def hit(n, t=600, off=100):
    return [(t, f"midi 0x90 {n} 100"), (t + off, f"midi 0x80 {n} 0")]


def f0(x, lo=25, hi=400):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 19)); fr = np.fft.rfftfreq(1 << 19, 1 / 44100)
    m = (fr > lo) & (fr < hi)
    return fr[np.flatnonzero(m)[np.argmax(S[m])]]


# a hit, and its tail past the note off
run("hit", src() + hit(48), 8000)
a, b, c = audio("hit", 0.3, 0.59), audio("hit", 0.6, 1.0), audio("hit", 7.5, 8.0)
check(rms(a) == 0 and rms(b) > 500, f"MEMBRANE: silent, then a hit (rms {rms(b):.0f})")
check(np.abs(audio("hit")).max() < 32767, f"MEMBRANE: no clipping (peak {np.abs(audio('hit')).max():.0f})")
check(rms(c) < 5, f"MEMBRANE: it dies away (rms {rms(c):.0f})")
run("hitv", src() + hit(48, off=5), 1500)
check(rms(audio("hitv", 0.65, 1.0)) > 0.8 * rms(audio("hit", 0.65, 1.0)), "a note let go at once: the tail is its own")
run("hail", src(HAIL) + hit(60), 2000)
h = audio("hail", 0.6, 0.8)
check(rms(h) > 300 and np.abs(audio("hail")).max() < 32767, f"HAIL: a hit, no clipping (rms {rms(h):.0f})")

# KEYS: C3 and C4 an octave apart; 19 EDO: step 7
PURE = [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (480, f"enc {K1} -60"), (500, f"enc {K2} -60"), (520, f"enc {K4} -30")]   # DRUM 1: no sweep, no modes, no drive


def pitch(n, edo=12):
    name = f"k{edo}{n}"
    run(name, src(MEMB, edo) + PURE + hit(n), 1800)
    return f0(audio(name, 0.62, 0.75))


p48, p60, p55 = pitch(48), pitch(60), pitch(55, 19)
c1 = 1200 * np.log2(p60 / p48); c2 = 1200 * np.log2(p55 / p48)
check(abs(c1 - 1200) < 12, f"KEYS: an octave up the keys is {c1:.1f} cents ({p48:.2f} Hz at C3)")
check(abs(c2 - 7 * 1200 / 19) < 12, f"19 EDO: seven steps are {c2:.1f} cents, wanted {7 * 1200 / 19:.1f}")
ev = src() + PURE + [(540, f"btn {EDIT}"), (560, "enc 3 -1")]   # DRUM 2: KEYS OFF
run("koff", ev + hit(60), 1800)
check(abs(1200 * np.log2(f0(audio("koff", 0.62, 0.75)) / p48)) < 12, "KEYS OFF: every key the same")

# DRUM 1, DRUM 2 change the sound
def snd(name, ev):
    run(name, src() + ev + hit(48), 1800)
    return audio(name, 0.6, 1.6)
base = snd("d0", [])
d1 = snd("d1", [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (500, f"enc {K2} 60")])                  # DRUM 1: MATERIAL
d2 = snd("d2", [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (500, f"enc {K1} 60")])                  # TENSION
d3 = snd("d3", [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (500, f"enc {K4} 60")])                  # DRIVE
d4 = snd("d4", [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (480, f"btn {EDIT}"), (500, f"enc {K1} 60")])   # DRUM 2: PUNCH
for nm, x in (("MATERIAL", d1), ("TENSION", d2), ("DRIVE", d3), ("PUNCH", d4)):
    check(rms(x - base) > 50, f"{nm} changes the sound (rms difference {rms(x - base):.0f})")
w0 = snd("w0", [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (480, f"btn {EDIT}"), (500, f"enc {K3} 127")])
w1 = run("w1", src() + [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (480, f"btn {EDIT}"), (500, f"enc {K3} 127")] + hit(48, 600) + hit(48, 1500) + hit(48, 2400), 3300)
x = audio("w1")
ps = [f0(x[int(t * 44100):int((t + 0.7) * 44100)]) for t in (0.7, 1.6, 2.5)]
check(len(set(round(p, 1) for p in ps)) > 1, f"WANDER: hits differ in pitch ({', '.join(f'{p:.1f}' for p in ps)})")

# through BANK: HAIL is noise; a held C4 gives it a pitch
ev = src(HAIL) + [(420, f"btn {LFO}"), (440, f"btn {LFO}"), (460, f"btn {LFO}"), (480, f"enc {K1} 1"), (500, f"enc {K2} 57"), (520, f"enc {K3} 37")]
run("bank", ev + hit(60, 700, 400), 2000)
S = np.abs(np.fft.rfft(audio("bank", 0.7, 1.4), 1 << 18)); fr = np.fft.rfftfreq(1 << 18, 1 / 44100)
pk = fr[np.argmax(S * (fr > 100) * (fr < 3000))]
check(abs(1200 * np.log2(pk / (440 * 2 ** ((60 - 69) / 12)))) % 1200 < 15 or abs(1200 * np.log2(pk / (440 * 2 ** ((60 - 69) / 12)))) % 1200 > 1185, f"HAIL through BANK sings the note ({pk:.1f} Hz)")

# saved
sv = OUT / "save.bin"
run("save", src(HAIL) + [(440, f"btn {EDIT}"), (460, f"btn {EDIT}"), (500, f"enc {K2} 11")], 5000, {**os.environ, "GRIMM_SAVE_OUT": str(sv)})
p = run("load", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": str(sv)})
m = re.search(r" drum ([\d,]+)", p)
check(m and m.group(1).startswith(f"8,{HAIL},") and m.group(1).split(",")[6] == "11", f"saved, power on: {m and m.group(1)}")
p = run("old", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": "tests/data/a1.2-saves.bin"})
m = re.search(r" drum ([\d,]+)", p)
check(m and m.group(1).split(",")[1] == "0", f"a project from before it loads: {m and m.group(1)}")

# KICK, SNARE, CLAP and the presets: each sounds, none clips, each dies away; about as loud as MEMBRANE
def loud(x):
    return max(rms(x[i:i + 882]) for i in range(0, len(x) - 882, 441))
ref = loud(audio("hit", 0.6, 1.0))
for nm, e in (("KICK", 0), ("SNARE", 21), ("CLAP", 42), ("KICK 909", 1), ("SNARE RIMSHOT", 34), ("CLAP CROWD", 49), ("MEMBRANE CONGA", 68), ("HAIL OPEN HAT", 86)):
    run(f"m{e}", src(e) + hit(48), 6000)
    x = audio(f"m{e}", 0.6, 1.0)
    db = 20 * np.log10(loud(x) / ref)                                 # (its loudest 20 ms: a hat is short, a crowd slow)
    check(rms(audio(f"m{e}", 0.3, 0.59)) == 0 and rms(x) > 200 and np.abs(audio(f"m{e}")).max() < 32767 and rms(audio(f"m{e}", 5.5, 6.0)) < 5,
          f"{nm}: a hit, no clipping, it dies away ({db:+.1f} dB to MEMBRANE)")
    check(-14 < db < 8, f"{nm}: level with the others ({db:+.1f} dB)")
run("kk", src(0) + hit(48), 1500); run("kk2", src(0) + hit(60), 1500)
ka, kb = f0(audio("kk", 0.75, 1.0), 20, 200), f0(audio("kk2", 0.75, 1.0), 20, 200)
check(abs(1200 * np.log2(kb / ka) - 1200) < 40, f"KICK: KEYS, an octave up ({ka:.1f}, {kb:.1f} Hz)")
p = run("pr", src(0) + [(900, f"enc {K1} 4"), (950, "probe")], 1000)   # (slowly: a step a detent)
m = re.search(r" drum ([\d,]+)", p)
check(m and m.group(1).split(",")[1] == "4" and m.group(1).split(",")[2:5] == ["66", "30", "109"], f"MODEL to KICK HARD: its sound ({m and m.group(1)})")
run("sn", src(21) + hit(48) + hit(48, 1400), 2500)
check(rms(audio("sn", 1.4, 1.8)) > 200, "SNARE: a second hit")
if len(sys.argv) > 2:                               # a save from 1.4.0 (its HAIL: 1): HAIL
    p = run("v140", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": sys.argv[2]})
    m = re.search(r" drum ([\d,]+)", p)
    check(m and m.group(1).split(",")[1] == str(HAIL), f"1.4.0's save: HAIL as it was ({m and m.group(1)})")

print("drum test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
