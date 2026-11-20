#!/usr/bin/env python3
"""Fetch redistribution-cleared Marburgum art-reference media.

Reads art_reference/media/licensed/MANIFEST.csv and downloads only rows with
auto_fetch=yes. Files are kept outside the repository. Files are reference
material, not automatically licensed as in-game assets.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import pathlib
import shutil
import sys
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "art_reference" / "media" / "licensed" / "MANIFEST.csv"
DEFAULT_OUTPUT = pathlib.Path.home() / "marburgum-data" / "art-reference" / "licensed"
USER_AGENT = "MarburgumII-Art-Reference/1.0 (+https://github.com/simon-gros/MarburgumII)"


def download(url: str, target: pathlib.Path) -> str:
    target.parent.mkdir(parents=True, exist_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response, target.open("wb") as out:
        shutil.copyfileobj(response, out)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    return digest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=pathlib.Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--force", action="store_true", help="replace existing files")
    args = parser.parse_args()

    output = args.output if args.output.is_absolute() else ROOT / args.output
    checksums: list[tuple[str, str, str]] = []

    with MANIFEST.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))

    for row in rows:
        allowed = row["auto_fetch"].strip().lower() == "yes"
        if not allowed:
            print(f"skip {row['ref_id']}: manual review required")
            continue

        target = output / row["filename"]
        if target.exists() and not args.force:
            digest = hashlib.sha256(target.read_bytes()).hexdigest()
            print(f"keep {row['ref_id']}: {target.relative_to(output)}")
        else:
            print(f"fetch {row['ref_id']}: {row['remote_url']}")
            try:
                digest = download(row["remote_url"], target)
            except Exception as exc:
                print(f"ERROR {row['ref_id']}: {exc}", file=sys.stderr)
                return 1
        checksums.append((digest, row["ref_id"], target.relative_to(output).as_posix()))

    output.mkdir(parents=True, exist_ok=True)
    checksum_path = output / "CHECKSUMS.sha256"
    checksum_path.write_text(
        "".join(f"{digest}  {filename}  # {ref_id}\n" for digest, ref_id, filename in checksums),
        encoding="utf-8",
    )
    print(f"wrote {checksum_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
