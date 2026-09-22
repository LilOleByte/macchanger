#!/usr/bin/env python3
# MAC Changer: IEEE OUI list builder
#
# Authors:
#      Alvaro Lopez Ortega <alvaro@alobbs.com>
#
# Copyright (C) 2002,2003,2013 Alvaro Lopez Ortega
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of the
# License, or (at your option) any later version.

"""Build data/OUI.list from the IEEE MA-L (OUI) registry."""

import re
import sys
import urllib.request
from pathlib import Path

URL = "https://standards-oui.ieee.org/oui/oui.txt"
ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "data" / "OUI.list"
HEX_RE = re.compile(
    r"^([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})-([0-9A-Fa-f]{2})\s+\(hex\)\s+(.+?)\s*$"
)


def load_registry():
    """Read oui.txt from the current directory, or download it."""
    local = Path("oui.txt")
    if local.is_file():
        print(f"Reading {local}", file=sys.stderr)
        return local.read_text(encoding="utf-8", errors="replace")

    print(f"Downloading {URL}", file=sys.stderr)
    request = urllib.request.Request(URL, headers={"User-Agent": "macchanger-oui-update"})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read().decode("utf-8", errors="replace")


def main():
    content = load_registry()
    lines = []
    for raw in content.splitlines():
        match = HEX_RE.match(raw.strip())
        if not match:
            continue
        oui = " ".join(part.upper() for part in match.group(1, 2, 3))
        name = " ".join(match.group(4).split())
        if not name:
            continue
        lines.append(f"{oui} {name}\n")

    if not lines:
        print("No OUI entries found", file=sys.stderr)
        return 1

    lines.sort()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("".join(lines), encoding="utf-8")
    print(f"Wrote {len(lines)} entries to {OUTPUT}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
