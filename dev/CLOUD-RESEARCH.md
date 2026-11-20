# Cloud storage on Zorin OS 18.1 / Ubuntu 24.04 (research 2026-11-20)

Summary of a research pass for the owner's question about cloud drives.

- Zorin's Files uses GNOME Online Accounts (GOA 3.50.x, Zorin-patched) + gvfs
  1.54.x (noble-updates) with `gvfsd-google` and `gvfsd-onedrive`; Zorin 18
  announced OneDrive in Settings > Online Accounts ("Microsoft 365 + OneDrive").
  https://blog.zorin.com/2025/10/14/zorin-os-18-has-arrived/ ,
  https://bugs.launchpad.net/ubuntu/+source/gvfs/+bug/2061857
- Other apps can reach them: `gio mount -l` lists GOA volumes
  (GProxyVolumeMonitorGoa; `onedrive://<id>/`, `google-drive://user@host/`),
  `gio mount <uri>` mounts, FUSE path `/run/user/<uid>/gvfs/<scheme>:host=...`.
  OneDrive shows real names; Google Drive shows file IDs over FUSE (Drive
  allows duplicate names) — https://gitlab.gnome.org/GNOME/gvfs/issues/402 .
  No cache: large folders are slow. The legacy Google backend is being replaced
  upstream (GNOME 50).
- KDE: kio-gdrive (23.08, KAccounts/signon) is broken on noble
  (https://bugs.launchpad.net/ubuntu/+source/signon-ui/+bug/2052782) and Google
  blocked its Drive scope; kio-onedrive is KF6 only. WebDAV/SMB/SFTP/FTP work
  via KIO (kio-extras) with URL + password.
- rclone (MIT): noble has 1.60.1 (2022); `rclone mount` (FUSE, VFS cache) covers
  ~70 providers; GUI-driven setup via `rclone config create --non-interactive`
  or `rclone rcd` (localhost API — must be locked down). Shared Google OAuth
  client is rate-limited. https://rclone.org/rc/ , https://rclone.org/drive/
- onedriver 0.14.1 (universe, GPL-3): OneDrive-only FUSE.

Proposal to the owner (preview 4 page): C1 sidebar "Cloud" section from GOA
(gio), C2 "Connect to Server" (WebDAV/SMB/SFTP/FTP), C3 later: rclone wizard.
