"""Scorecard: every tools/ref_compare.py script, the PS2 (PCSX2, the 100% save) against the port (PPSSPP).

    python tools/ps2_score.py [names]      # table; also analysis/ref/ps2_score.json

Per script: horizontal travel, peak height and frames in the air on both, and the frame-by-frame RMS error of
height and travel once both curves are lined up at their first movement (tools/hd_score.py's method). PASS when
the travel and peak are within 5% (or 10 cm) and the RMS errors under 12 cm.
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hd_score as hs
import ref_compare as rc

OUT = rc.OUT


def rows(path):
    return [{"f": r["f"], "x": r["x"], "y": r["y"], "z": r["z"]} for r in json.loads(path.read_text())[4:]]


def close(a, b):
    return abs(a - b) <= max(10.0, 0.05 * max(abs(a), abs(b)))


def main(names):
    results = {}
    print(f"{'script':10} {'travel ps2/port':>16} {'peak ps2/port':>14} {'air ps2/port':>13} {'h err':>6} {'d err':>6}  verdict")
    passed = 0
    for n in names:
        a, b = OUT / f"cmp_ps2_{n}.json", OUT / f"cmp_psp_{n}.json"
        if not a.exists() or not b.exists():
            continue
        ca, cb = hs.curves(rows(a)), hs.curves(rows(b))
        c = hs.compare(ca, cb)
        if c is None:
            continue
        # Over each whole recording, not cut to the shorter: one side standing still while the other moves
        # must not pass because both curves were cut to a single frame.
        c["travel"] = (ca["travel"][-1] - ca["travel"][0], cb["travel"][-1] - cb["travel"][0])
        c["peak"] = (max(ca["height"]), max(cb["height"]))
        ok = close(*c["travel"]) and close(*c["peak"]) and c["rms_height"] < 12 and c["rms_travel"] < 12
        passed += ok
        results[n] = dict(c, ok=ok)
        print(f"{n:10} {c['travel'][0]:7.0f} /{c['travel'][1]:6.0f}  {c['peak'][0]:6.0f} /{c['peak'][1]:5.0f}  "
              f"{c['air'][0]:5d} /{c['air'][1]:5d}  {c['rms_height']:6.1f} {c['rms_travel']:6.1f}  {'PASS' if ok else 'fail'}")
    print(f"{passed} of {len(results)} pass")
    (OUT / "ps2_score.json").write_text(json.dumps(results, indent=1))


if __name__ == "__main__":
    main(sys.argv[1:] or [n for n in rc.SCRIPTS if (OUT / f"cmp_ps2_{n}.json").exists()])
