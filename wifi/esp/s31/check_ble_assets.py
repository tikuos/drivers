#!/usr/bin/env python3
# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# check_ble_assets.py - Verify the S31 BLE source and controller ABI inputs.
# SPDX-License-Identifier: Apache-2.0
"""Refuse SDK files or controller archives outside the qualified hash manifest."""
import argparse
import hashlib
import json
from pathlib import Path


def check(sdk, controller, manifest):
    """Return mismatched or missing input paths without modifying the inputs."""
    errors = []
    for group, root in [('sdk', sdk), ('controller', controller)]:
        for relative, expected in manifest[group].items():
            path = root / relative
            try:
                actual = hashlib.sha256(path.read_bytes()).hexdigest()
            except OSError:
                actual = None
            if actual != expected:
                errors.append(str(path))
    return errors


def main():
    """Check supplied assets; print mismatches and return a failing exit code."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--controller', type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('ble_assets.json').read_text())
    errors = check(args.sdk, args.controller, manifest)
    if errors:
        print('S31 BLE asset mismatch: ' + ', '.join(errors))
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
