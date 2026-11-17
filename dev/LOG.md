# Work log

Append newest entries at the bottom. Each entry: date, who/what agent, what was
done, outcome, next step.

## 2026-11-17 — Claude Code (Opus 5.5) — pre-flight

- Read `START-HERE.md`, `PANERO_AGENT_PROMPT.md`, `references/REFERENCE-NOTES.md`.
- Host inspection: Zorin OS 18.1 / noble / x86_64 / Wayland GNOME. Distro
  Krusader 2.8.1 installed with a real user config (do not touch).
- Upstream tags: latest `v2.9.0` (KF6), `v2.8.1` (KF5). Noble has no KF6 dev
  packages → proposed base `v2.8.1` (D-001).
- Cloned `https://invent.kde.org/utilities/krusader.git` into
  `~/Projects/panero/panero` with remote name `upstream`
  (checked out `master` @ `fe91f0d47`; no branches created yet).
- GitHub: `NorbCraft` exists; `NorbCraft/panero` and
  `NorbCraft/panero-file-manager` return 404 publicly.
- Found missing tooling (no compiler, cmake, gh, Qt/KF dev packages), no
  passwordless sudo, no docker socket access → owner input needed.
- Created `AGENTS.md`, `CLAUDE.md`, `dev/{PLAN,LOG,DECISIONS,ENVIRONMENT,OPEN-QUESTIONS}.md`
  (not committed yet — git identity not configured).
- Next: owner answers Q1–Q7; then toolchain install, gh auth, baseline build.

## 2026-11-17 — Claude Code (Opus 5.5) — owner answers, repo restructure

- Owner answered Q1–Q7 (see `dev/OPEN-QUESTIONS.md` Resolved).
- Moved the clone so the git root is `~/Projects/panero`;
  moved `panero-handoff/` → `dev/handoff/`; redacted gmail in START-HERE copy.
- `git switch -c main v2.8.1`; deleted local `master`.
- Set per-repo identity (Norbert Szabo, GitHub no-reply e-mail).
- Confirmed v2.8.1 has no autotests (D-005); recorded D-004..D-006.
- Wrote `dev/scripts/bootstrap-host.sh` (all packages verified to have
  candidates in the configured noble/Zorin archives).
- Committed dev docs + handoff on `main` (local only; nothing pushed).
- Waiting on owner: run bootstrap script, re-login, `gh auth login`.

## 2026-11-17 — Claude Code (Opus 5.5) — GitHub access check

- Owner is logged in to GitHub in Firefox. Agent cannot use a browser session
  (no browser-control tool; reading Firefox cookies/credentials is off-limits).
- `gh` not installed yet (bootstrap script not run) → access not yet working.
- Recorded owner's account boundary rule in `AGENTS.md`.
- Waiting on owner: bootstrap script, then `gh auth login --web` (Firefox
  session makes approval a single click + one-time code).
