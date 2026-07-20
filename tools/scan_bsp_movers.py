#!/usr/bin/env python3
"""Scans a folder of Quake 3 .bsp maps and reports which ones have mover
entities (doors, platforms/elevators, rotators, ...) — useful for picking a
map to test BspEntityInstance-style movers against instead of guessing.

Only reads the entities lump (a substring of the file), no full BSP parse.
Quake 3 IBSP is version 46; Quake 2 reuses the same "IBSP" magic at version
38 with a completely different lump layout, so it's rejected rather than
mis-parsed.

Usage:
    python3 tools/scan_bsp_movers.py [maps_dir]

Defaults to /media/projectos/assets/quake/maps if no dir is given.
"""
import os
import re
import struct
import sys

Q3_MAGIC = b"IBSP"
Q3_VERSION = 46
LUMP_ENTITIES = 0

# classnames we consider "movers" — doors/elevators/rotators/etc, the kind
# of thing a BspEntityInstance would eventually drive. func_static (never
# moves — mappers use it to split off brushwork with its own origin/
# rotation) and func_timer (pure logic, fires targets on an interval, no
# brush motion) don't belong here even though they're func_* brush
# entities.
MOVER_CLASSES = [
    "func_door", "func_door_rotating", "func_plat", "func_rotating",
    "func_button", "func_train", "func_bobbing", "func_pendulum",
]

CLASSNAME_RE = re.compile(rb'"classname"\s*"([^"]+)"')


def read_entities_text(path):
    with open(path, "rb") as f:
        header = f.read(8)
        if len(header) < 8 or header[0:4] != Q3_MAGIC:
            return None, "not IBSP"
        version = struct.unpack("<i", header[4:8])[0]
        if version != Q3_VERSION:
            return None, f"IBSP v{version} (not Quake 3 — v46 expected; v38 is Quake 2)"

        lump_dir = f.read(17 * 8)
        if len(lump_dir) < 8:
            return None, "truncated lump directory"
        offset, length = struct.unpack_from("<ii", lump_dir, LUMP_ENTITIES * 8)

        f.seek(offset)
        return f.read(length), None


def scan(path):
    text, err = read_entities_text(path)
    if err:
        return None, err

    classnames = CLASSNAME_RE.findall(text)
    counts = {}
    for raw in classnames:
        name = raw.decode("utf-8", "replace")
        counts[name] = counts.get(name, 0) + 1

    movers = {c: n for c, n in counts.items() if c in MOVER_CLASSES}
    return {"total_entities": len(classnames), "movers": movers}, None


def main():
    maps_dir = sys.argv[1] if len(sys.argv) > 1 else "/media/projectos/assets/quake/maps"
    if not os.path.isdir(maps_dir):
        print(f"not a directory: {maps_dir}", file=sys.stderr)
        return 1

    bsp_files = sorted(f for f in os.listdir(maps_dir) if f.lower().endswith(".bsp"))
    if not bsp_files:
        print(f"no .bsp files in {maps_dir}", file=sys.stderr)
        return 1

    results = []
    for fname in bsp_files:
        full = os.path.join(maps_dir, fname)
        info, err = scan(full)
        results.append((fname, info, err))

    # most movers first, then alphabetical
    def sort_key(r):
        _, info, err = r
        mover_count = sum(info["movers"].values()) if info else -1
        return (-mover_count, r[0])

    results.sort(key=sort_key)

    print(f"{'map':30} {'movers':7} breakdown")
    print("-" * 70)
    for fname, info, err in results:
        if err:
            print(f"{fname:30} {'--':7} skipped: {err}")
            continue
        mover_total = sum(info["movers"].values())
        breakdown = ", ".join(f"{c}x{n}" for c, n in sorted(info["movers"].items())) or "(none)"
        print(f"{fname:30} {mover_total:<7} {breakdown}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
