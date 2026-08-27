#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--directory", required=True, type=Path)
parser.add_argument("--php-version", required=True)
parser.add_argument("--runtime-revision", required=True)
parser.add_argument("--repository", required=True)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()

tag = f"php-{args.php_version}-{args.runtime_revision}"
artifacts = []
for archive in sorted(args.directory.glob(f"pox-php-{args.php_version}-{args.runtime_revision}-*.tar.zst")):
    prefix = f"pox-php-{args.php_version}-{args.runtime_revision}-"
    target = archive.name.removeprefix(prefix).removesuffix(".tar.zst")
    artifacts.append(
        {
            "target": target,
            "url": f"https://github.com/{args.repository}/releases/download/{tag}/{archive.name}",
            "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        }
    )

if not artifacts:
    raise SystemExit("no runtime archives found")

release = {
    "php_version": args.php_version,
    "runtime_revision": args.runtime_revision,
    "abi_major": 1,
    "abi_minor": 0,
    "artifacts": artifacts,
}
args.output.write_text(json.dumps(release, indent=2, sort_keys=True) + "\n")
