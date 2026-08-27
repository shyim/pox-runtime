#!/usr/bin/env python3

import argparse
import json
import os
import urllib.request
from datetime import datetime, timezone
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--repository", required=True)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()

headers = {
    "Accept": "application/vnd.github+json",
    "Authorization": f"Bearer {os.environ['GITHUB_TOKEN']}",
    "X-GitHub-Api-Version": "2022-11-28",
}
releases = []
page = 1
while True:
    request = urllib.request.Request(
        f"https://api.github.com/repos/{args.repository}/releases?per_page=100&page={page}",
        headers=headers,
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        page_releases = json.load(response)
    if not page_releases:
        break
    for release in page_releases:
        if not release["tag_name"].startswith("php-"):
            continue
        manifest_asset = next(
            (asset for asset in release["assets"] if asset["name"] == "release.json"), None
        )
        if manifest_asset is None:
            continue
        manifest_request = urllib.request.Request(
            manifest_asset["url"],
            headers={**headers, "Accept": "application/octet-stream"},
        )
        with urllib.request.urlopen(manifest_request, timeout=30) as response:
            releases.append(json.load(response))
    page += 1

releases.sort(
    key=lambda release: (
        tuple(int(part) for part in release["php_version"].split(".")),
        int(release["runtime_revision"].removeprefix("r")),
    )
)
index = {
    "schema": 1,
    "generated_at": datetime.now(timezone.utc).isoformat(),
    "releases": releases,
}
args.output.write_text(json.dumps(index, indent=2, sort_keys=True) + "\n")
