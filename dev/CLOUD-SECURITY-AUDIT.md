# Cloud sign-in security audit (2026-11-21)

Scope: the cloud feature of v0.1.0-preview.4 (`app/Cloud/`, D-014 C2/C3) on
Zorin OS 18.1 / Ubuntu 24.04, before the owner signs in with real accounts.
The owner's goal is that anyone can sign in through Panero without risking their
account, and in particular their e-mail account.

Sources:
- Panero source at 62d024dd9.
- rclone source v1.60.1 and v1.76.0.
- The Ubuntu noble rclone changelog and debian/patches.
- cve.org / NVD, GitHub Security Advisories for rclone/rclone.
- Provider documentation (URLs below).

## 1. What preview 4 does

| Step | Implementation |
|---|---|
| rclone | Installed on demand from Ubuntu (`rclone` 1.60.1+dfsg-3ubuntu0.24.04.6) via PackageKit. |
| Config | The user's own `~/.config/rclone/rclone.conf`, **plain text**, mode 0600. Panero lists, creates and deletes remotes in it. |
| OAuth sign-in (Drive, OneDrive, Dropbox, pCloud, Box) | `rclone config create <name> <type>`. rclone's helper listens on 127.0.0.1:53682 and the browser opens. The token is written to the config. stdout is never read. |
| Password sign-in (MEGA, WebDAV/Nextcloud) | The password goes to `rclone obscure -` via stdin. The **obscured password is then passed on the command line** (`config create … pass <obscured> --no-obscure`). |
| Browsing / uploads | `rclone mount <remote>: $XDG_RUNTIME_DIR/panero-cloud/<remote> --vfs-cache-mode full`. |
| Remove ("sign out") | `rclone config delete <remote>` (local only). |
| Logs | No `-v`/`--dump`. stderr is kept in memory (last 8 lines) and only shown in error dialogs. |

## 2. Findings

| # | Severity | Finding |
|---|---|---|
| F1 | High | **CVE-2026-116194** (GHSA-hvpr-p4pv-4f46, CVSS 6.8): CSRF in rclone's pCloud OAuth callback. `StateBlankOK: true`, and the `hostname` from the callback is trusted without a check. A web page open during sign-in can plant an attacker's token and API host, so later uploads go to the attacker. Affects 1.52.0 to before 1.75.2. **Noble's 1.60.1 is affected and unpatched** (Ubuntu: "needs evaluation"); verified in the 1.60.1 source, `backend/pcloud/pcloud.go:99`. |
| F2 | High | Tokens and passwords are stored **in plain text** in `~/.config/rclone/rclone.conf`. "Obscured" passwords are reversible by anyone (`rclone reveal`). The file ends up in backups, home-folder sync and dotfile repositories, and on a stolen unencrypted disk. |
| F3 | High | MEGA/WebDAV: the obscured (= plain) password appears in **`/proc/<pid>/cmdline`, which is world-readable** on this system (mode 444, no `hidepid`). auditd and process monitors can record it. |
| F4 | Medium | "Remove" only deletes the local copy. **The token stays valid at the provider.** pCloud tokens never expire, and Google and Microsoft refresh tokens last until they are revoked. The dialog text ("Panero signs out by deleting the rclone remote") is misleading. |
| F5 | Medium | Noble's rclone 1.60.1 also lacks the fixes for CVE-2026-93986 and CVE-2026-88046 (`..` in names from shared content; fixed in 1.75.1), CVE-2024-52522 (fixed in 1.68.2) and the 2026 credential-leak-on-redirect fixes. Ubuntu backported only the three `rcd` command-execution CVEs, and Panero does not use `rcd`. |
| F6 | Medium | The VFS cache (`--vfs-cache-mode full`) keeps downloaded file contents unencrypted in `~/.cache/rclone/vfs/<remote>/`. They stay there after unmount and after the remote is removed. |
| F7 | Medium | Scopes are broad and not explained. Drive uses `drive` (all files). OneDrive uses `Files.ReadWrite.All` and `Sites.Read.All`. Dropbox, pCloud and Box get the full account. The sign-in text does not say what access is granted. |
| F8 | Low | The callback port 53682 is fixed. If another local program already listens there, it can receive the sign-in code; the client secrets inside rclone are public. No provider uses PKCE in rclone. |
| F9 | Low | Panero works on the user's own rclone config: remotes made elsewhere appear, and "Remove" deletes them. |
| F10 | Info | MEGA: the account password is also the encryption key for all MEGA files. Ubuntu's package **disables the mega backend**, so preview 4's MEGA option cannot work with the distro rclone. |
| F11 | Info | rclone's docs (v1.76.0) say the shared Google Drive client id "is being retired and will stop working during 2026". Drive sign-in may need the user's own Google client id. This needs testing. |
| OK | — | OAuth passwords never reach Panero or rclone (sign-in happens on the provider's page). The callback binds to 127.0.0.1 only, with a 128-bit `state` (except pCloud, F1). The config file is 0600. The mount folder is 0700 under `/run/user`, without `allow_other`. rclone's stdout (token) is never read, and nothing is logged to disk. |

## 3. Recommendations

**R1 — Use rclone ≥ 1.76.0, not noble's 1.60.1.**
- Ship the official upstream binary inside Panero's .deb as `/usr/lib/panero/rclone`. CI checks it against the SHA-256 pinned in the repository and rclone's signed `SHA256SUMS`.
- The user's system rclone is not used or touched.
- Panero refuses to sign in with an rclone older than 1.75.2.
- A weekly CI job opens an issue when a new rclone release or advisory appears.
- This fixes F1 and F5, and gives `config redacted`, `config encryption`, PKCE support and a mature `bisync`.

**R2 — Panero's own encrypted rclone config, unlocked by the desktop keyring.**
- `~/.config/panero/cloud/rclone.conf` (folder 0700, file 0600), encrypted with rclone's config encryption (NaCl secretbox).
- The key is 32 random bytes made by Panero and stored in the login keyring: gnome-keyring on Zorin/GNOME, KWallet on KDE, through QtKeychain.
- rclone gets the key only through `--password-command` (a small `panero-cloud-key` helper that reads the keyring) plus `--ask-password=false`. It is never in an argument, an environment variable or a file.
- rclone's weak key derivation (one SHA-256) does not matter for a random 256-bit key.
- The keyring is unlocked at login. With auto-login, GNOME asks for the keyring password once.

**R3 — No secret on any command line, ever.**
- WebDAV/Nextcloud passwords go into the encrypted config without passing through argv.
- A test scans every rclone process's command line during sign-in, browsing and removal.

**R4 — Harden the browser sign-in.**
- Check before signing in that nothing else listens on 127.0.0.1:53682.
- Open only rclone's `http://127.0.0.1:53682/auth?state=…` link.
- pCloud: accept only `api.pcloud.com` / `eapi.pcloud.com` (defence in depth on top of R1).
- Before the browser opens, explain in plain words what access is granted and what is not ("can see, change and delete your Drive files; cannot read your e-mail or change your password").
- Offer **Read-only** where the provider supports it (Drive `drive.readonly`, OneDrive `Files.Read`).
- OneDrive defaults to the user's own files (`Files.ReadWrite offline_access`, without Sites and `.All`).

**R5 — "Remove" really signs out.**
- Revoke the token at the provider where an API exists (Google, Dropbox and Box; pCloud to be tested).
- Then delete the remote and its cache.
- Always offer the provider's "connected apps" page:
  - Google: https://myaccount.google.com/connections
  - Microsoft: https://account.live.com/consent/Manage
  - Dropbox: https://www.dropbox.com/account/connected_apps
  - pCloud: Settings › Linked apps
- Add a "Sign out of all clouds" action.

**R6 — Cache and logs.**
- Use a Panero-owned cache folder (`--cache-dir`), deleted when the remote is removed. What happens on unmount depends on R10.
- Error messages are scrubbed of anything token-like before display. Panero never uses `-v`/`--dump`.

**R7 — Password-based services.**
- Drop MEGA from the guided list (F10); experts can still use "Other (rclone)".
- Nextcloud/WebDAV asks for an app password and explains why: it can be revoked per device without changing the account password.

**R8 — Separate from the user's own rclone.**
- Only Panero's own remotes are shown and removable.
- Optionally, a one-time import of remotes from `~/.config/rclone/rclone.conf` into the encrypted store, with a hint to delete the plain copy.

**R9 — pCloud.**
- CVE-2026-116194 is fixed by R1 (1.76.0: state required, `hostnameRe` only accepts `*.pcloud.com`).
- Verify with a regression test: during `rclone authorize pcloud`, a request with no `state` and `hostname=evil.example` must be rejected.
- pCloud tokens give full account access and never expire, so R5 revocation matters most here.
- If a safe path cannot be shown, the fallback is the official pCloud Drive app (AppImage, its own sign-in). Panero then simply browses `~/pCloudDrive`.
- pCloud WebDAV (account password, reportedly paid plans only) is **not** recommended.

**R10 — Offline files (owner request, follow-up phase after R1–R9).** Windows-style "Files On-Demand" is possible on top of rclone ≥ 1.76:

| State (as in OneDrive) | How |
|---|---|
| Online-only (cloud icon) | Normal mount listing; contents are downloaded only when opened. |
| Available on this device (opened once) | The VFS cache keeps opened files (`--vfs-cache-mode full`, long `--vfs-cache-max-age`, size limit). Panero reads its own cache folder to show the badge. |
| Always keep on this device (pinned) | "Keep on this device" on a folder or file runs a two-way sync of that folder to a local copy (`rclone bisync`, 1.76). That copy also works offline, and changes are synced when online. "Free up space" removes the pin. |

Limits:
- Linux has no cloud-files API like Windows, so other apps see a normal folder without overlay icons. Panero shows the badges in its own panels.
- Offline copies are plain files on disk. The plan should recommend full-disk encryption and offer a "Remove offline copies on sign-out" option.
- bisync conflicts are kept as `*.conflict` files and reported.

## 4. Test plan

Automated tests run in CI, with no accounts:
- Encrypted config round-trip with the keyring helper (gnome-keyring in a headless D-Bus session): no token or password text in any file under `HOME`.
- A process-list scanner during all rclone calls: no secret in any command line or in rclone's environment.
- A local WebDAV server for the password flow.
- The pCloud CVE regression (stateless callback rejected; foreign hostname rejected).
- The port-53682-in-use refusal.
- Revocation calls against a mock server.

Disposable accounts: the owner creates throwaway accounts (Google, Microsoft, Dropbox, pCloud, optionally Box). They are not linked to the real e-mail account, use their own passwords, and their addresses are not written into the repository. The owner types the passwords in the browser when Panero opens the sign-in page; the agent never sees them. For each account:
- sign in;
- browse, upload and download;
- verify at-rest encryption;
- Remove, then confirm the provider no longer accepts the token and the app has disappeared from the provider's connected-apps page.

Only after this does the owner sign in with real accounts.
