#!/usr/bin/env python3
"""Convert every TIM2 member/chunk indexed in analysis/pac to PNG and RGBA."""

import argparse
import json
from pathlib import Path

from dmc3_image import AfsArchive, Iso9660, now, write_json
from tim2 import convert


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--index", type=Path, default=Path("analysis/pac/gdata-packs.jsonl"))
    parser.add_argument("--output", type=Path, default=Path("build/assets/tim2"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = [json.loads(line) for line in args.index.read_text(encoding="utf-8").splitlines() if line]
    results = []
    with Iso9660(args.iso) as iso:
        afs = AfsArchive(iso, "GDATA.AFS")
        for record in records:
            slot = record["slot"]
            candidates = []
            if record.get("container_magic") == "TIM2":
                candidates.append((None, 0, record["size"]))
            candidates.extend((chunk["index"], chunk["offset"], chunk["size"])
                              for chunk in record.get("chunks", []) if chunk.get("magic") == "TIM2")
            for chunk_index, offset, size in candidates:
                suffix = "raw" if chunk_index is None else f"chunk_{chunk_index:03d}"
                name = f"slot_{slot:05d}_{suffix}.tim2"
                source = args.output / "sources" / name
                destination = args.output / "converted" / name
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_bytes(iso.read_file("GDATA.AFS", record["offset"] + offset, size))
                result = {"slot": slot, "chunk": chunk_index, "source": str(source)}
                try:
                    convert(source, destination, True)
                    result["result"] = "passed"
                    result["manifest"] = str(destination / f"{source.stem}.json")
                except Exception as error:
                    result["result"] = "failed"
                    result["error"] = str(error)
                results.append(result)
    passed = sum(item["result"] == "passed" for item in results)
    failed = len(results) - passed
    summary = {
        "generated_utc": now(),
        "source_iso": args.iso.name,
        "candidate_count": len(results),
        "passed": passed,
        "failed": failed,
        "results": results,
    }
    write_json(args.output / "conversion-summary.json", summary)
    print(f"TIM2 conversion complete: {passed} passed, {failed} failed")
    if failed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
