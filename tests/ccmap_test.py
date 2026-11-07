#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SYSTEM 3's CC MAP in host mode (tests/host.c): off, CCs do nothing; on, the channel picks the track; other channels and
CC numbers nothing; SCALE's for all; a MIDI LEARN link first; saved
    python3 tests/ccmap_test.py build/host/grimm"""
import os, re, subprocess, sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/cctest"); OUT.mkdir(parents=True, exist_ok=True)
HOME, ALGO = 8, 1
bad = 0


def check(ok, what):
    global bad
    print(("  ok    " if ok else "  FAIL  ") + "cc map: " + what)
    bad += 0 if ok else 1


def run(name, ev, end, env=None):
    s = OUT / f"{name}.txt"
    s.write_text("".join(f"{t} {w}\n" for t, w in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    return subprocess.run([BIN, str(OUT / name), str(s)], capture_output=True, text=True, env=env).stdout


def v(p, key):
    return re.search(rf" {key} ([\d,-]+)", p).group(1)


# SYSTEM (held HOME), SYSTEM 2, SYSTEM 3; CC MAP on; home again
ON = [(100, f"down {HOME}"), (800, f"up {HOME}"), (900, f"btn {HOME}"), (1000, f"btn {HOME}"), (1100, "enc 3 1"), (1200, f"btn {HOME}")]
cc = lambda t, ch, n, val: (t, f"midi {0xB0 + ch:#x} {n} {val}")
PR = (2000, "probe")

p = run("off", [cc(1500, 0, 102, 127), cc(1520, 0, 74, 0), PR], 2100)
check(v(p, "bank").startswith("0,") and v(p, "ccm") == "0", f"off: nothing moves (bank {v(p, 'bank')})")
p = run("on", ON + [cc(1500, 0, 102, 127), cc(1520, 0, 74, 0), cc(1540, 0, 7, 64), cc(1560, 0, 71, 127), cc(1580, 0, 91, 127), PR], 2100)
check(v(p, "ccm") == "1", "SYSTEM 3: CC MAP on")
check(v(p, "bank").startswith("3,"), f"channel 1, CC 102: T1's BANK MODE to the top ({v(p, 'bank')})")
check(v(p, "cut") == "0" and v(p, "lvl") == "64" and v(p, "res") == "127" and v(p, "rev") == "127", "CC 74, 7, 71, 91: cutoff, level, resonance, reverb")
p = run("ch2", ON + [cc(1500, 1, 102, 127), PR, (2050, "btn 2"), (2070, f"btn {HOME}"), (2100, f"enc {ALGO} 1"),   # (HOME: ALGORITHM picks the track)
                 (2300, "probe")], 2400)
lines = [l for l in p.splitlines() if l.startswith("probe")]
check(v(lines[0], "bank").startswith("0,"), "channel 2: not T1")
check(v(lines[1], "trk") == "1" and v(lines[1], "bank").startswith("3,"), "channel 2: T2")
p = run("ch9", ON + [cc(1500, 8, 102, 127), PR], 2100)
check(v(p, "bank").startswith("0,"), "channel 9: no track, nothing")
p = run("scale", ON + [cc(1500, 8, 117, 127), cc(1520, 0, 118, 127), PR], 2100)
check(v(p, "tuning") == "31", f"CC 117 on any channel: TUNING ({v(p, 'tuning')})")

# a MIDI LEARN link first: LEARN (SYSTEM 2), HOME's VOLUME turned, CC 102 linked: VOLUME, not BANK
ev = ON[:5] + [(1200, f"btn {HOME}"), (1300, "btn 8"), (1400, "enc 2 1"), (1500, "btn 6"), (1600, "btn 8"), (1700, "enc 2 1"),
               (1800, "midi 0xb0 102 10"), (1900, "midi 0xb0 102 33"), (2200, "probe")]
p = run("learn", ev, 2300)
check(v(p, "bank").startswith("0,") and v(p, "lvl") == "33", f"a linked CC goes to its link, not the map (level {v(p, 'lvl')}, bank {v(p, 'bank')})")

p = run("dr", ON + [cc(1500, 0, 107, 11), PR], 2100)
check(v(p, "drum").split(",")[5] == "11", f"CC 107: DRUM 1's first knob ({v(p, 'drum')})")
# switches
p = run("sw", ON + [cc(1500, 0, 88, 127), cc(1520, 1, 89, 127), cc(1540, 0, 90, 127), cc(1560, 0, 92, 127), PR], 2100)
check(v(p, "mute") == "1" and v(p, "solo") == "2" and v(p, "chord") == "1" and v(p, "latch2") == "1", f"CC 88, 89, 90, 92: mute T1, solo T2, chord and LATCH T1 ({v(p, 'mute')} {v(p, 'solo')} {v(p, 'chord')} {v(p, 'latch2')})")
p = run("sw0", ON + [cc(1500, 0, 88, 127), cc(1520, 0, 88, 0), cc(1540, 0, 92, 127), cc(1560, 0, 92, 0), PR], 2100)
check(v(p, "mute") == "0" and v(p, "latch2") == "0", "the same with 0: off again")
p = run("play", ON + [cc(1500, 8, 119, 127), (1700, "probe"), cc(1800, 8, 119, 127), (1900, "probe"), cc(2000, 8, 119, 0), (2200, "probe")], 2300)
ls = [l for l in p.splitlines() if l.startswith("probe")]
check([v(l, "play") for l in ls] == ["1", "1", "0"], f"CC 119 on any channel: PLAY, again: no change, 0: STOP ({[v(l, 'play') for l in ls]})")

# kept in the settings, in LATCH's byte
def settings(latch):
    b = bytearray(170)
    b[167], b[169] = 3, latch
    f = OUT / f"set{latch}.bin"
    f.write_bytes(bytes(b))
    return {**os.environ, "GRIMM_SETTINGS_IN": str(f)}
p = run("set2", [(1000, "probe")], 1100, settings(2))
check(v(p, "ccm") == "1", "saved settings: CC MAP on")
p = run("set1", [(1000, "probe")], 1100, settings(1))
check(v(p, "ccm") == "0", "older settings with LATCH ALWAYS OFF: CC MAP off")
print("cc map test: " + ("FAILED" if bad else "passed"))
sys.exit(1 if bad else 0)
