#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The pages' new order in host mode (tests/host.c): FX tapped, the buses, SELECT through them; FX held, the track's own
FX (DRIVE, BANK, BANK 2, DRONE); EDIT on a DRUM track: SOUND, DRUM 1, DRUM 2, SOURCE last (4/4); every header with its
"n/N" on the right (HOME: the tempo); SELECT the tempo on HOME only, ALGORITHM the track on HOME's pages only, PRESETS the
sounds on HOME's pages only; SOURCE's PRESET loads the source's sounds; the track's DRIVE drives, is saved; no PERFORM
    python3 tests/ui_test.py build/host/grimm"""
import os, re, subprocess, sys, wave
from pathlib import Path
import numpy as np

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/uitest"); OUT.mkdir(parents=True, exist_ok=True)
FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE = 2, 3, 4, 5, 6, 7, 8, 9
SELECT, ALGO, K1, K2, K3, K4, PRESETS = 0, 1, 2, 3, 4, 5, 6
bad = 0


def check(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + "ui: " + what)
    bad += 0 if ok else 1


def run(name, ev, end, env=None):
    s = OUT / f"{name}.txt"
    s.write_text("".join(f"{t} {w}\n" for t, w in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(s)], capture_output=True, text=True, env=env).stdout
    return {int(m.group(1)): m.group(0) for m in re.finditer(r"probe (\d+) .*", out)}


def v(p, key):
    m = re.search(rf" {key} ([^ ]+)", p)
    return m.group(1) if m else ""


def hdr(p):
    return v(p, "hdr").replace("_", " ")


# FX tapped: TAPE 1/6; SELECT on: GRAIN .. DRIVE 6/6, no further; FX held: the track's DRIVE 1/4; SELECT: BANK, BANK 2,
# DRONE 4/4; FX tapped from there: the buses again
t = 3000
ev = [(t, f"btn {FX}"), (t + 50, "probe"), (t + 100, f"enc {SELECT} 1"), (t + 150, "probe"), (t + 200, f"enc {SELECT} 9"),
      (t + 250, "probe"), (t + 300, f"down {FX}"), (t + 700, "probe"), (t + 750, f"up {FX}"), (t + 800, "probe"),
      (t + 850, f"enc {SELECT} 1"), (t + 900, "probe"), (t + 950, f"enc {SELECT} 2"), (t + 1000, "probe"),
      (t + 1050, f"btn {FX}"), (t + 1100, "probe"), (t + 1150, f"btn {GLO}"), (t + 1200, "probe")]
p = run("fx", ev, t + 1300)
check(hdr(p[t + 50]) == "FX|TAPE|1/6", f"FX tapped: TAPE, 1/6 on the right ({hdr(p[t + 50])})")
check(hdr(p[t + 150]) == "FX|GRAIN|2/6", f"SELECT: GRAIN 2/6 ({hdr(p[t + 150])})")
check(hdr(p[t + 250]) == "FX|DRIVE|6/6", f"SELECT on and on: DRIVE 6/6, the last ({hdr(p[t + 250])})")
check(hdr(p[t + 700]).startswith("FX|DRIVE T1") and hdr(p[t + 700]).endswith("|1/4"), f"FX held: the track's DRIVE, 1/4 ({hdr(p[t + 700])})")
check(v(p[t + 800], "page") == v(p[t + 700], "page"), "FX let go after its hold: the page stays")
check(hdr(p[t + 900]).startswith("FX|BANK T1") and hdr(p[t + 900]).endswith("|2/4"), f"SELECT: BANK 2/4 ({hdr(p[t + 900])})")
check(hdr(p[t + 1000]).startswith("FX|DRONE T1") and hdr(p[t + 1000]).endswith("|4/4"), f"SELECT: DRONE 4/4 ({hdr(p[t + 1000])})")
check(hdr(p[t + 1100]) == "FX|TAPE|1/6", f"FX tapped from the track's FX: the buses ({hdr(p[t + 1100])})")
check(v(p[t + 1200], "page") == v(p[t + 1100], "page"), "GLO tapped (no pages now): the page stays")

# LFO: one page now (DRONE, BANK gone to FX held); SEL: SCALE 1/2, CHORD 2/2; HOME held: SYSTEM 1/3
ev = [(t, f"btn {LFO}"), (t + 50, "probe"), (t + 100, f"enc {SELECT} 1"), (t + 150, "probe"), (t + 200, f"btn {SEL}"),
      (t + 250, "probe"), (t + 300, f"enc {SELECT} 1"), (t + 350, "probe"), (t + 400, f"down {HOME}"), (t + 1100, f"up {HOME}"),
      (t + 1150, "probe"), (t + 1200, f"enc {SELECT} 2"), (t + 1250, "probe")]
p = run("pages", ev, t + 1300)
check(hdr(p[t + 50]).endswith("|1/1") and hdr(p[t + 150]) == hdr(p[t + 50]), f"LFO: one page ({hdr(p[t + 50])})")
check(hdr(p[t + 250]).endswith("|1/2") and hdr(p[t + 350]).endswith("|2/2"), f"SEL: SCALE 1/2, SELECT: CHORD 2/2 ({hdr(p[t + 250])}, {hdr(p[t + 350])})")
check(hdr(p[t + 1150]) == "HOME|SYSTEM|1/3" and hdr(p[t + 1250]) == "HOME|SYSTEM 3|3/3", f"HOME held: SYSTEM 1/3, SELECT: SYSTEM 3 3/3 ({hdr(p[t + 1150])}, {hdr(p[t + 1250])})")

# EDIT on a DRUM track: SOUND 1/4, DRUM 1, DRUM 2, SOURCE 4/4 (EDIT tapped, or SELECT)
DR = [(t, f"btn {EDIT}"), (t + 40, f"btn {EDIT}"), (t + 60, f"enc {K1} -20"), (t + 80, f"enc {K1} 8"), (t + 120, f"btn {EDIT}")]
ev = DR + [(t + 150, "probe")] + [(t + 200 + 100 * i, f"btn {EDIT}") for i in range(3)] + [(t + 199 + 100 * i, "probe") for i in range(1, 4)]
ev += [(t + 500, "probe")]
p = run("edit", ev, t + 600)
got = [hdr(p[k]).split("|")[1].split(" T")[0] + " " + hdr(p[k]).split("|")[2] for k in (t + 150, t + 299, t + 399, t + 500)]
check(got == ["SOUND 1/4", "DRUM 1 2/4", "DRUM 2 3/4", "SOURCE 4/4"], f"EDIT, a DRUM track: SOUND, DRUM 1, DRUM 2, SOURCE last ({got})")

# SELECT: the tempo on HOME only; ALGORITHM: the track on HOME's pages only; PRESETS: sounds on HOME's only
ev = [(t + 50, f"enc {SELECT} 10"),   # (HOME: where it starts)
       (t + 100, "probe"), (t + 150, f"btn {ENV}"), (t + 200, f"enc {SELECT} 10"),
      (t + 250, f"enc {ALGO} 1"), (t + 300, f"enc {PRESETS} 1"), (t + 350, "probe"), (t + 400, f"btn {HOME}"), (t + 450, f"enc {ALGO} 1"),
      (t + 500, "probe"), (t + 550, f"enc {PRESETS} 1"), (t + 600, "probe")]
p = run("enc", ev, t + 700)
check(v(p[t + 100], "own") == "130", f"SELECT on HOME: the tempo ({v(p[t + 100], 'own')})")
check(v(p[t + 350], "own") == "130" and v(p[t + 350], "sel") == "0" and v(p[t + 350], "snd1") == v(p[t + 100], "snd1"),
      "on ENV: SELECT, ALGORITHM and PRESETS leave the tempo, the track and the sound alone")
check(v(p[t + 500], "sel") == "1", "ALGORITHM on HOME: the next track")
check(hdr(p[t + 600]) != hdr(p[t + 500]), f"PRESETS on HOME: a sound loaded ({hdr(p[t + 600])})")

# SOURCE's PRESET (KNOB 2): the source's own sounds, loaded, named; LATCH on KNOB 3
ev = [(t, f"btn {EDIT}"), (t + 40, f"btn {EDIT}"), (t + 60, f"enc {K1} -20"), (t + 100, "probe"), (t + 150, f"enc {K2} 1"),
      (t + 200, "probe"), (t + 250, f"enc {K2} 1"), (t + 300, "probe"), (t + 350, f"enc {K3} -1"), (t + 400, "probe")]
p = run("pre", ev, t + 500)
check(v(p[t + 100], "spre") == "-1" and v(p[t + 100], "eng") == "0", f"SOURCE to VA: no PRESET loaded yet ({v(p[t + 100], 'spre')})")
check(v(p[t + 200], "spre") == "0" and v(p[t + 300], "spre") == "1" and v(p[t + 200], "eng") == "0" and v(p[t + 300], "eng") == "0",
      f"PRESET: VA's first, then second sound; still VA ({v(p[t + 200], 'spre')}, {v(p[t + 300], 'spre')})")
check(hdr(p[t + 300]) != hdr(p[t + 200]), f"PRESET: a different sound, its name said ({hdr(p[t + 200])} -> {hdr(p[t + 300])})")
check(v(p[t + 400], "latch").startswith("0,") and v(p[t + 300], "latch") != v(p[t + 400], "latch") or v(p[t + 300], "latch").startswith("0,") and v(p[t + 400], "latch").startswith("0,"), f"KNOB 3: LATCH ({v(p[t + 300], 'latch')} -> {v(p[t + 400], 'latch')})")

# the track's DRIVE: AMOUNT up drives the sound; saved
def rms(name, a, b):
    w = wave.open(str(OUT / f"{name}.wav")); x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(float)
    x = x[int(a * 88200):int(b * 88200)]; return float(np.sqrt(((x - x.mean()) ** 2).mean()))
LOFF = [(2000, f"down {SEL}"), (2050, "btn 1"), (2100, f"up {SEL}")]   # (LATCH off: one note, let go)
NOTE = [(t + 600, "midi 0x90 48 110"), (t + 1400, "midi 0x80 48 0")]
TD = [(t, f"down {FX}"), (t + 400, f"up {FX}"), (t + 450, f"enc {K1} 110"), (t + 470, f"enc {K2} 2")]
run("d0", LOFF + NOTE, t + 1500); run("d1", LOFF + TD + NOTE, t + 1500)
r0, r1 = rms("d0", 3.7, 4.3), rms("d1", 3.7, 4.3)
w0 = wave.open(str(OUT / "d0.wav")); w1 = wave.open(str(OUT / "d1.wav"))
a0 = np.frombuffer(w0.readframes(w0.getnframes()), dtype=np.int16).astype(float)[int(3.7 * 88200):int(4.3 * 88200)]
a1 = np.frombuffer(w1.readframes(w1.getnframes()), dtype=np.int16).astype(float)[int(3.7 * 88200):int(4.3 * 88200)]
check(np.sqrt(((a1 - a0) ** 2).mean()) > 0.2 * r0, f"the track's DRIVE (CLIP, AMOUNT 110): the sound changed ({r0:.0f} -> {r1:.0f})")
sv = OUT / "save.bin"
TD0 = [(300, f"down {FX}"), (700, f"up {FX}"), (750, f"enc {K1} 110"), (770, f"enc {K2} 2")]   # (before the first autosave)
run("sv", TD0, 5000, {**os.environ, "GRIMM_SAVE_OUT": str(sv)})
p = run("ld", [(400, "probe")], 500, {**os.environ, "GRIMM_SAVE_IN": str(sv)})
check(v(p[400], "td") == "110,2,127,127", f"the track's DRIVE saved, power on ({v(p[400], 'td')})")

# no PERFORM: FX held + a white key plays its note
ev = LOFF + [(t, f"down {FX}"), (t + 400, "keys 0x4"), (t + 450, "probe"), (t + 500, "keys 0"), (t + 550, f"up {FX}")]
p = run("nopf", ev, t + 600)
check(v(p[t + 450], "notes") not in ("", "fx"), f"FX held + a white key: a note, no trick ({v(p[t + 450], 'notes')})")

print("ui test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
