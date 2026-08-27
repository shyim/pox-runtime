#!/usr/bin/env python3

import argparse
import hashlib
import json
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument("--library", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--php-version", required=True)
parser.add_argument("--runtime-revision", required=True)
parser.add_argument("--target", required=True)
args = parser.parse_args()

manifest = {
    "schema": 1,
    "abi_major": 1,
    "abi_minor": 0,
    "php_version": args.php_version,
    "runtime_revision": args.runtime_revision,
    "target": args.target,
    "zts": True,
    "library": args.library.name,
    "library_sha256": hashlib.sha256(args.library.read_bytes()).hexdigest(),
}
args.output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
