#!/usr/bin/env python3
# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# fetch.py - Fetch and hash-check pinned S31 PHY, Wi-Fi and coexistence assets.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import json
from pathlib import Path
import urllib.request


def main():
    base = Path(__file__).resolve().parent
    vendor = base / 'vendor'
    vendor.mkdir(exist_ok=True)
    for item in json.loads((base / 'assets.json').read_text()):
        target = vendor / item['file']
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == item['sha256']:
            continue
        with urllib.request.urlopen(item['url'], timeout=60) as source:
            data = source.read()
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('hash mismatch: ' + item['file'])
        temporary = target.with_suffix(target.suffix + '.tmp')
        temporary.write_bytes(data)
        temporary.replace(target)
        print(item['file'])


if __name__ == '__main__':
    main()
