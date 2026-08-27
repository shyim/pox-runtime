#!/usr/bin/env python3

import json
import urllib.request


with urllib.request.urlopen(
    "https://www.php.net/releases/index.php?json&version=8&max=100", timeout=30
) as response:
    releases = json.load(response)

selected = {}
for version in releases:
    parts = version.split(".")
    if len(parts) != 3 or parts[:2] not in (["8", "4"], ["8", "5"]):
        continue
    series = ".".join(parts[:2])
    candidate = tuple(int(part) for part in parts)
    if series not in selected or candidate > selected[series][0]:
        selected[series] = (candidate, version)

print(json.dumps([selected[series][1] for series in ("8.4", "8.5") if series in selected]))
