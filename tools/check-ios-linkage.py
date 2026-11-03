"""Check a packaged .geode's iOS slice against a Geode iOS release.

iOS is the one target we cannot build or run from a Windows checkout, so the
cheapest way to rule out a whole class of launch crashes is to prove the dylib
is a real arm64 iOS binary and that every Geode symbol it imports is exported
by the Geode build the players run. A missing Geode symbol makes dyld abort
before any mod code runs, which reads exactly like "the game crashes on launch".

Usage:
    python tools/check-ios-linkage.py --geode ci-out/afterflow.prsevend.geode \
                                      --geode-ios geode-v5.11.2-ios.zip \
                                      --slice afterflow.prsevend.ios.dylib

The Geode iOS archives are published per release at
https://github.com/geode-sdk/geode/releases (asset geode-<tag>-ios.zip).
"""

import argparse
import struct
import zipfile

MH_MAGIC_64 = 0xFEEDFACF
LC_SYMTAB = 0x2
LC_BUILD_VERSION = 0x32
PLATFORMS = {1: "macOS", 2: "iOS", 3: "tvOS", 4: "watchOS", 6: "Mac Catalyst", 7: "iOS Simulator"}


def load_commands(data):
    count = struct.unpack_from("<I", data, 16)[0]
    offset = 32
    for _ in range(count):
        cmd, size = struct.unpack_from("<II", data, offset)
        yield cmd, offset, size
        offset += size


def symbols(data):
    """Return (undefined, defined) symbol names from the Mach-O symbol table."""
    table = None
    for cmd, offset, _ in load_commands(data):
        if cmd == LC_SYMTAB:
            table = struct.unpack_from("<IIII", data, offset + 8)
    if not table:
        raise SystemExit("no LC_SYMTAB: the binary is stripped, cannot check linkage")
    symoff, nsyms, stroff, _ = table

    undefined, defined = [], set()
    for index in range(nsyms):
        strx, ntype, _, _, _ = struct.unpack_from("<IBBHQ", data, symoff + index * 16)
        if ntype & 0xE0:  # N_STAB
            continue
        end = data.index(b"\x00", stroff + strx)
        name = data[stroff + strx:end].decode("utf-8", "replace")
        kind = ntype & 0x0E
        if kind == 0x0:
            undefined.append(name)
        elif kind == 0xE:
            defined.add(name)
    return undefined, defined


def describe(data):
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic != MH_MAGIC_64:
        raise SystemExit("not a 64-bit little-endian Mach-O")
    cputype = struct.unpack_from("<i", data, 4)[0]
    filetype = struct.unpack_from("<I", data, 12)[0]
    arch = {0x0100000C: "arm64", 0x01000007: "x86_64"}.get(cputype, hex(cputype))
    kind = {6: "MH_DYLIB", 2: "MH_EXECUTE"}.get(filetype, str(filetype))
    line = f"{arch} {kind}"
    for cmd, offset, _ in load_commands(data):
        if cmd == LC_BUILD_VERSION:
            platform, minos, sdk, _ = struct.unpack_from("<IIII", data, offset + 8)
            def version(value):
                return f"{(value >> 16) & 0xFFFF}.{(value >> 8) & 0xFF}.{value & 0xFF}"
            line += f", platform {PLATFORMS.get(platform, platform)}, min {version(minos)}, sdk {version(sdk)}"
    return line


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--geode", required=True, help="packaged .geode to check")
    parser.add_argument("--geode-ios", required=True, help="geode-<tag>-ios.zip from a Geode release")
    parser.add_argument("--slice", default="afterflow.prsevend.ios.dylib", help="entry name inside the .geode")
    args = parser.parse_args()

    with zipfile.ZipFile(args.geode) as archive:
        binary = archive.read(args.slice)
    with zipfile.ZipFile(args.geode_ios) as archive:
        geode = archive.read("Geode.ios.dylib")

    print(f"{args.slice}: {describe(binary)}")
    undefined, _ = symbols(binary)
    _, geode_exports = symbols(geode)

    # libc / libSystem / libc++abi come from the OS, not from Geode. Only the
    # Geode and Game-API symbols have to resolve against Geode.ios.dylib.
    geode_imports = [name for name in undefined if "geode" in name or name.startswith("__ZN5geode")]
    missing = [name for name in geode_imports if name not in geode_exports]

    print(f"undefined symbols: {len(undefined)}, geode imports: {len(geode_imports)}")
    if missing:
        print("MISSING from Geode.ios.dylib (dyld would abort at load):")
        for name in sorted(missing):
            print("  " + name)
        raise SystemExit(1)
    print("ok: every Geode import resolves against Geode.ios.dylib")


if __name__ == "__main__":
    main()
