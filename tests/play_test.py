#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Arctic Scream's sound and play pages in host mode (tests/host.c): ENV's AHD (ENV again), on a synth and on a drum;
every source answers ENV (a new one too: the list below); two LFOs, their MODE (TRIG) and SYNC, new destinations, on
the drums too; the drums a source each; EUCLID's hits; ALGORITHM's KEYS: STEPS and NOTES into SEQ's step; at an EDO the
keys walk the scale; WARP and MORPH move every source
    python3 tests/play_test.py build/host/grimm"""
import re, subprocess, sys, wave
from pathlib import Path
import numpy as np

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/playtest"); OUT.mkdir(parents=True, exist_ok=True)
FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ = 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
SELECT, ALGO, K1, K2, K3, K4 = 0, 1, 2, 3, 4, 5
NAMES = ["VA", "FM", "SAMPLE", "VOICE", "WHEEL", "PHYS", "PHASE", "HOWL", "KICK", "SNARE", "CLAP", "TOM", "HATS"]
bad = 0


def check(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + "play: " + what)
    bad += 0 if ok else 1


def run(name, ev, end):
    s = OUT / f"{name}.txt"
    s.write_text("".join(f"{t} {w}\n" for t, w in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(s)], capture_output=True, text=True).stdout
    return {int(m.group(1)): m.group(0) for m in re.finditer(r"probe (\d+) .*", out)}


def v(p, key):
    m = re.search(rf" {key} ([^ ]+)", p)
    return m.group(1) if m else ""


def audio(name, a=0, b=None):
    w = wave.open(str(OUT / f"{name}.wav"))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float).reshape(-1, 2).mean(axis=1)
    return x[int(a * 44100):int(b * 44100) if b else None]


def rms(x):
    return float(np.sqrt(((x - x.mean()) ** 2).mean())) if len(x) else 0.0


def f0(x, lo=40, hi=2000):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)), 1 << 18)); fr = np.fft.rfftfreq(1 << 18, 1 / 44100)
    m = (fr > lo) & (fr < hi)
    return fr[np.flatnonzero(m)[np.argmax(S[m])]]


T = 1000                                        # (the setup's events before it)
DRY = [(60, f"btn {FX}"), (70, f"enc {SELECT} 2"), (80, f"enc {K4} -127"), (90, f"enc {SELECT} 1"), (100, f"enc {K4} -127"),
       (110, f"enc {SELECT} -2"), (120, f"enc {K4} -127")]   # (GRAIN, DELAY, SPACE: MIX 0: the dry sound alone)
LOFF = [(150, f"down {SEL}"), (160, "btn 1"), (170, f"up {SEL}")]   # (T1: LATCH off)
NOLFO = [(180, f"btn {LFO}"), (190, f"enc {K2} -100")]              # (LFO 1: DEPTH 0)


def source(e, t=200):                           # SOURCE (EDIT's last): e
    return [(t, f"btn {EDIT}"), (t + 10, f"btn {EDIT}"), (t + 20, f"enc {K1} -20"), (t + 30, f"enc {K1} {e}")]


BASE = DRY + LOFF + NOLFO
note = lambda t, n=48, d=1500, ch=0: [(t, f"midi {0x90 + ch:#x} {n} 100"), (t + d, f"midi {0x80 + ch:#x} {n} 0")]   # noqa: E731

# ENV: every source; ADSR (a held note sustains) and AHD (ENV again: it decays, held or not; a drum's tail shortened)
ADSR = [(300, f"btn {ENV}"), (310, f"enc {K1} -127"), (320, f"enc {K2} 0"), (330, f"enc {K3} 127"), (340, f"enc {K4} -127")]
AHD = ADSR + [(360, f"btn {ENV}"), (370, f"enc {K1} -127"), (380, f"enc {K2} -127"), (390, f"enc {K3} -127"), (400, f"enc {K3} 25"), (420, "probe")]
for e, nm in enumerate(NAMES):
    if nm == "SAMPLE": continue                 # (no sample loaded in host mode: silent)
    p = run(f"a{e}", BASE + source(e) + ADSR + note(T, 48, 1500), T + 2200)
    q = run(f"h{e}", BASE + source(e) + AHD + note(T, 48, 1500), T + 2200)
    sa, sh = rms(audio(f"a{e}", 2.0, 2.4)), rms(audio(f"h{e}", 2.0, 2.4))
    if e < 8:
        check(sa > 100 and sh < 0.2 * sa, f"{nm}: ADSR sustains a held note ({sa:.0f}); AHD's DECAY ends it while held ({sh:.0f})")
    else:                                       # (a drum: AHD's ATTACK at its top, ~10 s: the hit comes in slowly)
        run(f"s{e}", BASE + source(e) + AHD + [(410, f"enc {K1} 127")] + note(T, 48, 1500), T + 2200)
        pa, ps = np.abs(audio(f"a{e}", 0.9, 1.5)).max(), np.abs(audio(f"s{e}", 0.9, 1.5)).max()
        check(ps < 0.2 * pa, f"{nm}: AHD shapes the hit (a slow ATTACK: its peak {pa:.0f} -> {ps:.0f})")
    check(v(q[420], "envm") == "1", f"{nm}: ENV pressed again on ENV: AHD")

# LFO 2 on PITCH (LFO 1 off): the pitch moves; TRIG restarts LFO 1 at START with each note; SYNC 1 BAR at 120: 2 s a cycle
L2 = [(300, f"btn {LFO}"), (310, f"enc {SELECT} 2"), (320, f"enc {K1} 110"), (330, f"enc {K2} 100"), (340, f"enc {K3} -30"), (350, f"enc {K4} -10")]
run("l2", BASE + source(0) + L2 + note(T, 60, 2500), T + 2600)
x = audio("l2")
fs = [f0(x[int(t * 44100):int((t + 0.05) * 44100)], 100, 600) for t in np.arange(1.3, 3.2, 0.05)]
check(max(fs) / min(fs) > 1.2, f"LFO 2 on PITCH: the pitch moves ({min(fs):.0f} .. {max(fs):.0f} Hz)")
TRIG = [(300, f"btn {LFO}"), (310, f"enc {K2} 50"), (320, f"enc {SELECT} 1"), (330, f"enc {K1} 1"), (340, f"enc {K2} 32")]   # TRIG, START 90 deg
p = run("trig", BASE + source(0) + TRIG + note(T, 60, 200) + [(T + 30, "probe")] + note(T + 1700, 60, 200) + [(T + 1730, "probe")], T + 2000)
a1, a2 = int(v(p[T + 30], "lph").split(",")[0]), int(v(p[T + 1730], "lph").split(",")[0])
st = 32 * 33818640
check(abs(a1 - st) < 2 ** 27 and abs(a2 - st) < 2 ** 27, f"LFO 1 TRIG: each note restarts it at START ({a1 / 2 ** 32 * 360:.0f}, {a2 / 2 ** 32 * 360:.0f} deg)")
SYNC = [(310, f"enc {SELECT} 1"), (320, f"enc {K4} 1"), (330, f"enc {SELECT} -1"), (340, f"enc {K1} -127"), (350, f"enc {K1} 5")]
p = run("sync", BASE + SYNC + [(T, "probe"), (T + 500, "probe")], T + 600)
d = (int(v(p[T + 500], "lph").split(",")[0]) - int(v(p[T], "lph").split(",")[0])) % 2 ** 32
check(abs(d / 2 ** 32 - 0.25) < 0.02, f"LFO SYNC 1 BAR at 120 BPM: a quarter cycle in 0.5 s ({d / 2 ** 32:.3f})")
# a new destination: RESONANCE (10)
RES = [(300, f"btn {LFO}"), (310, f"enc {K1} 100"), (320, f"enc {K2} 100"), (330, f"enc {K3} -30"), (340, f"enc {K3} 10")]
run("r0", BASE + source(0) + note(T, 48, 2000), T + 2100); run("r1", BASE + source(0) + RES + note(T, 48, 2000), T + 2100)
dr = rms(audio("r1", 1.3, 2.8) - audio("r0", 1.3, 2.8)) / rms(audio("r0", 1.3, 2.8))
check(dr > 0.02, f"LFO 1 on RESONANCE: the sound moves (the difference: {dr * 100:.0f} %)")
# on a drum: LFO 1 RANDOM on PITCH: the same key, hits at other pitches
DP = [(300, f"btn {LFO}"), (310, f"enc {K1} 90"), (320, f"enc {K2} 100"), (330, f"enc {K3} -30"), (340, f"enc {K4} 5")]
run("dp", BASE + source(11) + DP + note(T, 48, 100) + note(T + 700, 48, 100) + note(T + 1400, 48, 100), T + 2000)
x = audio("dp")
ps = [f0(x[int(t * 44100):int((t + 0.25) * 44100)], 40, 400) for t in (1.05, 1.75, 2.45)]
check(len(set(round(q) for q in ps)) > 1, f"TOM: LFO 1 on PITCH moves its hits ({', '.join(f'{q:.0f}' for q in ps)} Hz)")

# EUCLID: 3 HITS of 8, 1/16 at 120 BPM (8 steps: 1 s), a key held 2 s: 6 notes
EU = [(300, f"btn {ARP}"), (310, f"enc {K2} -8"), (320, f"enc {K1} -13"), (330, f"down {ARP}"), (1000, f"up {ARP}"),
      (1010, f"btn {ENV}"), (1020, f"enc {K2} -127"), (1030, f"enc {K3} -127"), (1040, f"enc {K4} -127")]
run("eu", BASE + source(0) + EU + note(1500, 48, 2000), 3700)
x = np.abs(audio("eu", 1.5, 3.5)); env = np.array([x[i:i + 441].max() for i in range(0, len(x) - 441, 441)])   # (10 ms)
hits, last = [], -99
for i in range(1, len(env)):
    if env[i] > 0.4 * env.max() and env[i - 1] <= 0.4 * env.max() and i - last > 10: hits.append(i), (last := i)
check(len(hits) == 6, f"EUCLID 3 of 8 at 1/16: 6 notes in 2 s, the rest rests ({len(hits)}: {[h * 10 for h in hits]} ms)")

# ALGORITHM off HOME: KEYS: STEPS (key 3: step 3 on, again: off); NOTES: on SEQ's GRID a key's note into the cursor's step
G = [(300, f"btn {SEQ}"), (320, f"enc {ALGO} 1"), (340, "probe"), (400, "keys 0x8"), (450, "keys 0"), (500, "probe"),
     (600, "keys 0x8"), (650, "keys 0"), (700, "probe"), (800, f"enc {ALGO} -1"), (850, "probe"), (900, "keys 0x20"), (950, "keys 0"), (1000, "probe")]
p = run("kb", LOFF + G, 1100)
check(v(p[340], "kbs") == "1" and "STEPS" in v(p[340], "hdr"), f"ALGORITHM right off HOME: KEYS: STEPS ({v(p[340], 'hdr')})")
check(v(p[500], "steps") == "1" and v(p[500], "cur").startswith("3,") and v(p[700], "steps") == "0", f"STEPS: key 4 turns step 4 on, again off ({v(p[500], 'steps')}, {v(p[700], 'steps')})")
check(v(p[850], "kbs") == "0" and v(p[1000], "steps") == "1" and v(p[1000], "cur") != v(p[850], "cur"), f"NOTES on GRID: the key's note into the step shown ({v(p[1000], 'cur')})")

# parameter locks: KEYS: STEPS, a step's key held + a knob on any page (HOME's CUTOFF): locked on that step, the step
# not turned on by it; PLAY: the step has its CUTOFF, the next its own again (a trigless lock); key + OCT-: cleared
PL = [(300, f"btn {SEQ}"), (320, f"enc {ALGO} 1"), (340, f"btn {HOME}"), (360, "probe"), (400, "keys 0x1"), (450, f"enc {K2} -40"),
      (500, "keys 0"), (550, "probe"), (600, "keys 0x2"), (650, "keys 0"), (700, "probe")]
PL += [(800, "btn 12"), (2000, "btn 12")] + [(800 + i, "probe") for i in range(20, 1200, 20)]
p = run("pl", LOFF + PL, 2100)
c0 = int(v(p[360], "cut"))
check(v(p[550], "lks") == "1" and v(p[550], "steps") == "0" and v(p[550], "cut") == str(c0),
      f"STEPS: key 1 held + CUTOFF: a lock on step 1, the knob untouched, the step not on ({v(p[550], 'lks')} lock, {v(p[550], 'steps')} steps, CUTOFF {v(p[550], 'cut')})")
check(v(p[700], "steps") == "1", "STEPS: key 2 tapped: step 2 on")
cuts = [int(v(p[800 + i], "cut")) for i in range(20, 1200, 20)]
check(min(cuts) < c0 and cuts[-1] == c0 and c0 in cuts[len(cuts) // 2:], f"PLAY: step 1 has its locked CUTOFF ({min(cuts)}), the others the knob's ({c0})")
p = run("plc", LOFF + PL[:9] + [(800, "keys 0x1"), (820, "btn 0"), (840, "keys 0"), (900, "probe")], 1000)
check(v(p[900], "lks") == "0", "key 1 held + OCT-: its locks cleared")
# SELECT on SEQ's pages: the cursor again
p = run("cur", [(300, f"btn {SEQ}"), (320, f"enc {SELECT} 1"), (400, "probe")], 500)
check(v(p[400], "cur").startswith("1,"), f"SELECT on SEQ: the cursor on to step 2 ({v(p[400], 'cur')})")
# RANDOMIZE: EDIT held + PRESETS on SOUND: the source's knobs new; EDIT + OCT-: back
RN = source(0) + [(300, f"btn {EDIT}"), (350, "probe"), (400, f"down {EDIT}"), (450, "enc 6 6"), (500, f"up {EDIT}"), (550, "probe"),
                  (600, f"down {EDIT}"), (650, "btn 0"), (700, f"up {EDIT}"), (750, "probe")]
p = run("rn", RN, 800)
check(v(p[550], "sp") != v(p[350], "sp") and v(p[550], "page") == v(p[350], "page"), f"EDIT + PRESETS: the SOUND knobs re-rolled ({v(p[350], 'sp')} -> {v(p[550], 'sp')})")
check(v(p[750], "sp") == v(p[350], "sp"), f"EDIT + OCT-: undone ({v(p[750], 'sp')})")

# 24 EDO, RAST: eight keys, eight notes, none twice (the scale walked)
E24 = [(300, f"btn {SEL}"), (310, f"enc {K3} 7"), (320, f"enc {K2} 50"), (330, "probe")]
ev = LOFF + E24
for i, k in enumerate([0, 1, 2, 3, 4, 5, 6, 7]):
    ev += [(400 + i * 100, f"keys {1 << k:#x}"), (430 + i * 100, "probe"), (460 + i * 100, "keys 0")]
p = run("edo", ev, 1300)
ns = [v(p[430 + i * 100], "notes") for i in range(8)]
check(len(set(ns)) == 8 and "" not in ns, f"24 EDO, {v(p[330], 'hdr').split('|')[1]}: eight keys, eight different notes ({' '.join(ns)})")

# WARP and MORPH move every source
for e, nm in enumerate(NAMES):
    if nm == "SAMPLE": continue
    W = lambda k: [(300, f"btn {HOME}"), (310, f"enc {K3 if k == 'w' else K4} 127")]   # noqa: E731
    run(f"w0{e}", BASE + source(e) + note(T, 48, 3000) + ([] if e < 8 else note(T + 1500, 48, 100) + note(T + 2500, 48, 100)), T + 3500)
    run(f"w1{e}", BASE + source(e) + W("w") + note(T, 48, 3000) + ([] if e < 8 else note(T + 1500, 48, 100) + note(T + 2500, 48, 100)), T + 3500)
    run(f"m1{e}", BASE + source(e) + W("m") + note(T, 48, 3000) + ([] if e < 8 else note(T + 1500, 48, 100) + note(T + 2500, 48, 100)), T + 3500)
    a, w, m = audio(f"w0{e}", 2.4, 4.0), audio(f"w1{e}", 2.4, 4.0), audio(f"m1{e}", 2.4, 4.0)
    check(rms(w - a) > 0.05 * rms(a) and rms(m - a) > 0.05 * rms(a), f"{nm}: WARP and MORPH change it ({rms(w - a) / max(rms(a), 1):.2f}, {rms(m - a) / max(rms(a), 1):.2f})")

print("play test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
