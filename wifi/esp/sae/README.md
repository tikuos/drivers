# SAE H2E

The native C6 Wi-Fi adapter uses Hostap 2.12 SAE with Mbed TLS 3.6.7
P-256 arithmetic. TikuKits supplies SHA-256, and the radio allocator and
RF entropy source supply memory and randomness. The profile accepts group
19, hash-to-element and passwords of 1 through 63 bytes. It requires PMF
with CCMP and BIP-CMAC-128. Password identifiers, SAE-PK, hunting-and-pecking,
transition mode and other groups are outside this profile.

Fetch the source archives before building C6 Wi-Fi or the SAE host tests:

```sh
python3 drivers/wifi/esp/sae/fetch.py
```

`fetch.py` pins archive URLs and SHA-256 digests. `SHA256SUMS` records the
source and header inputs used by this profile; C6 builds verify them.
Downloaded sources remain in the gitignored `vendor/` directory.
Hostap's license is in `vendor/hostap/COPYING`; Mbed TLS's license is in
`vendor/mbedtls/LICENSE`. Hostap is distributed under the BSD license,
and Mbed TLS under Apache-2.0 or GPL-2.0-or-later.

The engine retains one password element and one peer exchange. It verifies
commits and confirms before releasing the PMK, retains transmitted messages
for retries, and clears temporary scalars after confirmation. The AP expires
an unassociated exchange after five seconds. An admitted connection owns the
peer slot until disconnect. The credential worker derives the
password element before configuring the radio. The AP keeps no PMKSA cache
across disconnects. A stale PMKID receives association status 53; the
client can retry a full SAE exchange.

Run the engine, management-frame crypto and station exchange checks from
the OS root:

```sh
make -C TikuBench/tests/host/nonkernel wifi-sae wifi-pmf wifi-wpa3
make -C TikuBench/tests/host/kernel wifi-ap-sae wifi-sae-driver ipc-radio-sae
python3 -B TikuBench/tests/host/kernel/test_wifi_security_mutations.py
```
