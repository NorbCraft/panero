# Open questions for the owner

Record the answer and date under each question; move resolved items to the
bottom section.

## Open

_(none)_

## Resolved (asked and answered 2026-11-17)

- **Q1 — Local repository location.** → Repository root is
  `~/Projects/panero` itself (no nested `panero/` folder). The
  handoff folder was moved to `dev/handoff/`.
- **Q2 — Commit e-mail.** → GitHub no-reply address
  `349977413+NorbCraft@users.noreply.github.com`, name "Norbert Szabo",
  configured per repository only.
- **Q3 — Privilege flow.** → Owner runs `dev/scripts/bootstrap-host.sh` once
  with sudo in a terminal. Further privileged needs are batched into a script
  for the owner rather than ad-hoc prompts.
- **Q4 — Isolation for clean installs.** → Both: Docker (user added to `docker`
  group by the bootstrap script) for quick packaging iterations, and a libvirt
  VM (Ubuntu 24.04 cloud image) for clean GUI install tests.
- **Q5 — Publishing the handoff material.** → Commit everything; the gmail
  address in `START-HERE.md` was redacted to the account name.
- **Q6 — Testing on this machine.** → Yes: KDiff3 and the Panero `.deb` may be
  installed on the host, and test windows may open on the live desktop. The
  owner's Krusader config and files must stay untouched.
- **Q7 — X11 coverage.** → XWayland (`QT_QPA_PLATFORM=xcb`) + Xvfb, reported
  honestly as such (not a native Xorg session).
