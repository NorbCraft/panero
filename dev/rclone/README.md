# Panero's rclone

Panero ships its own copy of [rclone](https://rclone.org) (MIT) for cloud
drives instead of the distribution's package (Ubuntu 24.04 has 1.60.1, which is
affected by CVE-2026-116194 and other 2026 advisories). See
`dev/CLOUD-SECURITY-AUDIT.md` (R1) and `dev/DECISIONS.md` D-016.

- `rclone.sha256` pins the version and the SHA-256 of the official release
  zips. `dev/scripts/fetch-rclone.sh <dir>` downloads, verifies and unpacks it;
  `build-deb.sh` and CI use it. The package installs it as
  `/usr/lib/<arch>/libexec/panero/rclone`, byte-identical to upstream
  (`test-deb.sh` checks this).
- Panero refuses rclone older than 1.75.2 (`PaneroRclone::minimumVersion()`).
- `.github/workflows/rclone-watch.yml` opens an issue every week when a newer
  release or an advisory exists.

## Updating the pin

1. Download `SHA256SUMS` of the new version from `https://downloads.rclone.org/vX.Y.Z/`
   and verify its signature with rclone's release key
   `FBF737ECE9F8AB18604BD2AC93935E02FF3B54FA` (fingerprint as published on
   https://rclone.org/release_signing/); compare with the digests of the GitHub
   release assets.
2. Put the new version and the `linux-amd64` / `linux-arm64` sums into
   `rclone.sha256`; refresh `COPYING` if upstream changed it.
3. Read the rclone changelog for changes to `config create`, `config
   encryption`, OAuth, `mount` and `bisync` options Panero uses.
4. Run `dev/scripts/cloud-security-test.sh`, ctest, `build-deb.sh` and
   `test-deb.sh`.
