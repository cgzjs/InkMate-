#!/usr/bin/env python3
"""Build compact FortunePager TSV vocabulary packs from WordTyper JSON."""

import argparse
import json
import re
from pathlib import Path


PACKS = {
    "cet6_high_freq.json": "vocab_cet6.tsv",
    "ielts_core.json": "vocab_ielts.tsv",
}


def compact_translation(values: list[str], limit: int = 36) -> str:
    text = "；".join(str(value).strip() for value in values[:2] if str(value).strip())
    text = re.sub(r"\s+", " ", text).replace("\t", " ")
    return text[:limit]


def build_pack(source: Path, destination: Path) -> tuple[int, int]:
    payload = json.loads(source.read_text(encoding="utf-8"))
    lines = []
    for item in payload["words"]:
        word = str(item.get("word", "")).strip().replace("\t", " ")
        meaning = compact_translation(item.get("translations") or [])
        if word and meaning:
            lines.append(f"{word}\t{meaning}\n")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("".join(lines), encoding="utf-8", newline="\n")
    return len(lines), destination.stat().st_size


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path, help="WordTyper vocabularies directory")
    parser.add_argument("output", type=Path, help="Arduino sketch data directory")
    args = parser.parse_args()
    for source_name, output_name in PACKS.items():
        count, size = build_pack(args.source / source_name, args.output / output_name)
        print(f"{output_name}: {count} words, {size} bytes")


if __name__ == "__main__":
    main()
