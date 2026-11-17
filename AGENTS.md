# Panero — agent / contributor entry point

Panero is an independent fork of Krusader (GPL-2.0-or-later). It is not
affiliated with or endorsed by KDE or the Krusader maintainers.

If you are an agent (or a human) picking this work up, read in this order:

1. `dev/handoff/PANERO_AGENT_PROMPT.md` — the authoritative requirements
   (plus `dev/handoff/START-HERE.md` and `dev/handoff/references/`).
2. `dev/PLAN.md` — phased plan with status checkboxes. Source of truth for
   "what is done / what is next".
3. `dev/LOG.md` — chronological work log (newest at the bottom). Append to it.
4. `dev/DECISIONS.md` — decisions taken and why. Do not silently reverse one;
   add a new entry that supersedes it.
5. `dev/ENVIRONMENT.md` — build/test machine baseline, required tools,
   how to reproduce the build, GitHub access setup.
6. `dev/OPEN-QUESTIONS.md` — questions for the owner and their answers.

## Working rules

- Keep `dev/PLAN.md` and `dev/LOG.md` current at the end of every work
  session (and before any long-running step), so another machine or agent can
  continue without the chat history.
- Never commit credentials, tokens, private keys, or personal paths/data.
- Never touch the owner's real Krusader configuration (`~/.config/krusaderrc`,
  `~/.local/share/krusader`) or real files/Trash. Tests use temporary
  `HOME`/`XDG_*` directories.
- Do not open PRs or issues against upstream KDE/Krusader. Only the owner's
  repository (`NorbCraft/panero`) is in scope.
- GitHub account boundary (owner instruction, 2026-11-17): use the `NorbCraft`
  account only to create and work in `NorbCraft/panero` (code, branches, PRs,
  issues, Actions, releases, repo settings needed for those). Do not modify
  anything else on the account: profile, e-mails, SSH/GPG keys, tokens/apps,
  other repositories, stars/follows, organizations or account settings.
- Preserve upstream history, copyright notices and license texts. Upstream
  remote is named `upstream` (https://invent.kde.org/utilities/krusader.git).
- Commit identity for this repository is set per-repository (see
  `dev/ENVIRONMENT.md`), never globally.
