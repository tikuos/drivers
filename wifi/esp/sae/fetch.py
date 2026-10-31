#!/usr/bin/env python3
# Tiku Drivers
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# fetch.py - Download and verify the SAE and P-256 source archives.
# SPDX-License-Identifier: Apache-2.0
import hashlib
import io
from pathlib import Path, PurePosixPath
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parent
ASSETS = (
    ("hostap", "https://w1.fi/releases/hostapd-2.12.tar.gz",
     "f43502561c28ba47ab77e18e1a973d07361c68cc8b14178e619bd5796b70eabd",
     ("src/", "README", "COPYING")),
    ("mbedtls", "https://codeload.github.com/Mbed-TLS/mbedtls/tar.gz/"
     "068ff080b369adfac81509f9b57b2afabaf82dc5",
     "ca6bd316bbec49ef20088f39b8755fcaec0b7e45781506de55b17cd191ae6937",
     ("include/", "library/", "LICENSE")),
)


def main():
    """Verify each complete archive before extracting regular source files."""
    for name, url, digest, prefixes in ASSETS:
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit(f"{name}: archive checksum mismatch")
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            for member in archive:
                parts = PurePosixPath(member.name).parts[1:]
                if not member.isfile() or not parts or ".." in parts:
                    continue
                relative = "/".join(parts)
                if not relative.startswith(prefixes):
                    continue
                target = ROOT / "vendor" / name / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
        print(f"{name}: verified and extracted")


if __name__ == "__main__":
    main()
