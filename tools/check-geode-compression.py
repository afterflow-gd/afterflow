"""Fail if a .geode invites the iOS loader to unpack something it cannot read.

Geode 5.x unpacks mods with minizip, and the iOS loader is built without zstd.
A zstd-compressed .geode (zip compression method 93, which `geode package merge
--zstd` produces) therefore lists its entries fine and extracts none of them, and
the game reports:

    Failed to load afterflow.prsevend: No binary could be found for current
    platform. This mod doesn't support this platform.

Only stored (0) and deflate (8) entries are safe. This check reads the central
directory only, so it needs no decompressor and runs on any Python.

Usage:
    python tools/check-geode-compression.py build-clang/afterflow.prsevend.geode
    python tools/check-geode-compression.py build-clang/
"""

import sys
import zipfile
from pathlib import Path

SAFE = {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}
NAMES = {zipfile.ZIP_STORED: "store", zipfile.ZIP_DEFLATED: "deflate", 12: "bzip2", 14: "lzma", 93: "zstd", 95: "xz"}


def check(path: Path) -> bool:
    with zipfile.ZipFile(path) as archive:
        methods = {item.compress_type for item in archive.infolist()}
    unsafe = sorted(method for method in methods if method not in SAFE)
    readable = ", ".join(NAMES.get(method, str(method)) for method in sorted(methods))
    if unsafe:
        print(f"{path}: BAD - {readable}")
        return False
    print(f"{path}: ok - {readable}")
    return True


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    targets = []
    for argument in argv[1:]:
        path = Path(argument)
        targets.extend(sorted(path.rglob("*.geode")) if path.is_dir() else [path])
    if not targets:
        print("no .geode files found")
        return 1
    results = [check(target) for target in targets if target.exists()]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
