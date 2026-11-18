"""Unpack one of the game's real-time cutscenes ("demos") and parse its timelines (docs/CUTSCENES.md).

A demo id (event op 02, `FUN_0028ef00`) names a record in the table at 0x5a16d0 (ids under 300) or 0x5a1830 (300 on):
word 0 is the demo's file (file index << 16 | archive, archive 0 = GDATA.AFS), word 1 a linked id or -1, words 2-5 its
four language files. A real-time demo's file is a PAC of 13 entries: [0] the scene script (text), then pairs of a name
list and a PAC of those files: [1]/[2] the timelines ("...Set.txt"), [3]/[4] models, textures and stages, [5]/[6] MOT
motions, [7]/[8] hide tracks (.hid), [9]/[10] cameras (.cam), [11]/[12] shared GDATA / effect data ("G n", "V n").

    python tools/demo_unpack.py <iso> <demo id> --output build/demo/<id>      # files, plus timeline.json
    python tools/demo_unpack.py <iso> --list                                 # every demo id and its kind

The ISO and everything written stay local (the repo is public).
"""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_enemy_ai  # noqa: E402  (the executable's reader)
import prepare_skinned_model as psm  # noqa: E402  (Iso9660, AfsArchive)

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "analysis" / "disc" / "SLUS_213.61"
TABLES = ((0x5A16D0, 0, 300), (0x5A1830, 300, 100))
PAIRS = ((1, 2, "set"), (3, 4, "model"), (5, 6, "motion"), (7, 8, "hide"), (9, 10, "camera"))


def pac_entries(data: bytes):
    count = struct.unpack_from("<I", data, 4)[0]
    offsets = list(struct.unpack_from(f"<{count}I", data, 8))
    present = sorted(o for o in offsets if o)
    return [data[o:min([x for x in present if x > o] + [len(data)])] if o else b"" for o in offsets]


def text(blob: bytes) -> str:
    return blob.decode("shift_jis", "replace").replace("\r", "")


def name_list(blob: bytes):
    out = []
    for line in text(blob).split("\n"):
        line = line.strip().strip("\x00")
        if not line or line.startswith(";"):
            continue
        if line == "# End":
            break
        out.append(line)
    return out


def demo_record(elf, demo: int):
    for base, first, count in TABLES:
        if first <= demo < first + count:
            rec = elf.word(base + (demo - first) * 4)
            return [elf.word(rec + k * 4) for k in range(6)] if rec else None
    return None


def parse_script(src: str):
    """The scene script: '# Load' and '# Set' sections of `Kind [slot] file` lines."""
    sections, current = {}, None
    for line in src.split("\n"):
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        if line.startswith("#"):
            current = line[1:].strip()
            sections.setdefault(current, [])
            continue
        parts = line.split()
        if current is None:
            continue
        if len(parts) == 3:
            sections[current].append({"kind": parts[0], "slot": int(parts[1]) if parts[1].isdigit() else parts[1], "file": parts[2]})
        elif len(parts) == 2:
            sections[current].append({"kind": parts[0], "slot": None, "file": parts[1]})
    return sections


def value(parts):
    vals = []
    for p in parts:
        try:
            vals.append(int(p) if p.lstrip("-").isdigit() else float(p))
        except ValueError:
            vals.append(p)
    return vals[0] if len(vals) == 1 else vals


def parse_clips(src: str):
    """A timeline file: '# Clip' ... '# End' blocks of `Key value...` lines (SetFrame, Life, ClipScale, the file...)."""
    clips, clip = [], None
    for line in src.split("\n"):
        line = line.split(";", 1)[0].strip()
        if not line:
            continue
        if line == "# Clip":
            if clip is not None:   # a clip need not end with "# End": the next one closes it
                clips.append(clip)
            clip = {}
            continue
        if line == "# End":
            if clip is not None:
                clips.append(clip)
            clip = None
            continue
        if clip is None:
            continue
        parts = line.split()
        clip[parts[0]] = value(parts[1:]) if len(parts) > 1 else None
    if clip is not None:
        clips.append(clip)
    return clips


def unpack(iso_path: Path, demo: int, out: Path):
    elf = gen_enemy_ai.Elf(EXE.read_bytes())
    rec = demo_record(elf, demo)
    if not rec or rec[0] in (0, 0xFFFFFFFF) or rec[0] & 0xFFFF:
        raise SystemExit(f"Demo {demo} has no GDATA file (record {rec})")
    with psm.Iso9660(iso_path) as iso:
        gdata = psm.AfsArchive(iso, "GDATA.AFS")
        entry = gdata.entries[rec[0] >> 16]
        data = iso.read_file("GDATA.AFS", entry["offset"], entry["size"])
    parts = pac_entries(data)
    if len(parts) != 13 or not parts[0].startswith(b";"):
        raise SystemExit(f"Demo {demo} (GDATA {rec[0] >> 16}) isn't a real-time demo")
    out.mkdir(parents=True, exist_ok=True)
    (out / "script.txt").write_text(text(parts[0]), encoding="utf-8")
    files = {}
    for names_at, data_at, kind in PAIRS:
        names = name_list(parts[names_at])
        blobs = pac_entries(parts[data_at]) if parts[data_at][:4] in (b"PAC\x00", b"PNST") else []
        sub = out / kind
        sub.mkdir(exist_ok=True)
        for k, name in enumerate(names):
            blob = blobs[k] if k < len(blobs) else b""
            (sub / name).write_bytes(blob)
            files[name] = (kind, blob)
    script = parse_script(text(parts[0]))
    timeline = {"demo": demo, "gdata": rec[0] >> 16, "linked": rec[1] if rec[1] != 0xFFFFFFFF else -1,
                "shared": name_list(parts[11]), "load": script.get("Load", []), "tracks": []}
    for item in script.get("Set", []):
        kind, blob = files.get(item["file"], (None, b""))
        clips = parse_clips(text(blob)) if blob else []
        timeline["tracks"].append({"kind": item["kind"], "slot": item["slot"], "file": item["file"], "clips": clips})
    end = max((c.get("SetFrame", 0) + c.get("Life", 0) * c.get("ClipScale", 1.0)
               for t in timeline["tracks"] if t["kind"] == "Camera" for c in t["clips"]), default=0)
    timeline["frames"] = end
    (out / "timeline.json").write_text(json.dumps(timeline, indent=1, ensure_ascii=False), encoding="utf-8")
    cams = sum(1 for n in files if n.endswith(".cam"))
    mots = sum(1 for n in files if n.endswith(".mot"))
    print(f"Demo {demo}: GDATA {rec[0] >> 16}, {len(timeline['tracks'])} tracks, {cams} cameras, {mots} motions, "
          f"{end:.0f} frames -> {out}")


def list_demos(iso_path: Path):
    elf = gen_enemy_ai.Elf(EXE.read_bytes())
    with psm.Iso9660(iso_path) as iso:
        gdata = psm.AfsArchive(iso, "GDATA.AFS")
        for base, first, count in TABLES:
            for k in range(count):
                try:
                    rec = demo_record(elf, first + k)
                except ValueError:
                    continue
                if not rec or rec[0] in (0, 0xFFFFFFFF) or rec[0] & 0xFFFF or (rec[0] >> 16) >= len(gdata.entries):
                    continue
                entry = gdata.entries[rec[0] >> 16]
                head = iso.read_file("GDATA.AFS", entry["offset"], 16)
                kind = "other"
                if head[:4] == b"PAC\x00" and struct.unpack_from("<I", head, 4)[0] == 13:
                    first_at = struct.unpack_from("<I", head, 8)[0]
                    if iso.read_file("GDATA.AFS", entry["offset"] + first_at, 1) == b";":
                        kind = "realtime"
                print(first + k, rec[0] >> 16, kind, entry["size"])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("iso", type=Path)
    parser.add_argument("demo", type=int, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        list_demos(args.iso)
    elif args.demo is not None:
        unpack(args.iso, args.demo, args.output or ROOT / "build" / "demo" / str(args.demo))
    else:
        parser.error("a demo id or --list")


if __name__ == "__main__":
    main()
