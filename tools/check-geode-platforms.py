import sys
import zipfile
from pathlib import Path

BINARIES = [
    "afterflow.prsevend.dll",
    "afterflow.prsevend.dylib",
    "afterflow.prsevend.android32.so",
    "afterflow.prsevend.android64.so",
    "afterflow.prsevend.ios.dylib",
]


def check(path: Path) -> bool:
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
    missing = [binary for binary in BINARIES if binary not in names]
    if missing:
        print(f"{path}: BAD - missing {', '.join(missing)}")
        return False
    print(f"{path}: ok - {len(BINARIES)} platform binaries present")
    return True


def main(argv):
    if len(argv) < 2:
        print("usage: check-geode-platforms.py <package-or-directory>")
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
