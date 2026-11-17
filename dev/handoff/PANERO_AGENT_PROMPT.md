# Build and publish Panero: a modern native Krusader fork

You are the implementation agent. Carry this project through to a working, tested, public GitHub repository and a downloadable **amd64 `.deb` release**. Implement the application, not just a design, plan, website, or demonstration. Work autonomously through investigation, coding, testing, packaging, commits, CI, and publication. Ask me when you need authentication, account access, OS privilege approval, or information that genuinely blocks progress. Make routine implementation decisions yourself and continue unaffected work while resolving blockers.

## 1. Product and scope

The application is called **Panero**. It is an independent, native Linux fork of **Krusader**, with a modern appearance and a workflow familiar to someone who uses Total Commander. Preserve Krusader's working file-management engine and extensive existing functionality. Improve the native C++/Qt/KDE application; do not replace it with Electron, a web wrapper, or an unrelated implementation.

The user has chosen the Panero name and **icon concept A**. Use them throughout the product. This choice is not a statement that the name has received professional trademark clearance. There is known existing software use of PANERO; record that fact in development documentation, do not assert exclusive rights or legal clearance, and do not restart naming selection or impose a speculative naming approval gate. Comply with actual applicable licensing and attribution requirements.

Target PC: a standard Intel **x86_64** machine running the latest Zorin OS. At handoff on 17 November 2026, the [official Zorin details page](https://zorin.com/os/details/) identifies **Zorin OS 18.1, based on Ubuntu 24.04 LTS**. Verify this when starting and document the actual supported baseline. Support the standard Zorin desktop, without requiring KDE Plasma. Ubuntu 24.04 and Linux Mint 22.x compatibility is desirable where the same package works, but do not claim untested platforms. The implementation machine may be different from the target PC.

**Release format: `.deb` only. No AppImage, Flatpak, Snap, Windows, or macOS deliverables are requested.** Prefer the simplest reliable native packaging and installation path. The user should download the package from GitHub Releases and install through Zorin's graphical package installer, with a documented terminal fallback that resolves dependencies.

## 2. References and precedence

The attached `references/` directory contains a visual concept, screenshots, the selected icon, and reference notes. Read them before designing. They communicate visual direction; the written requirements here are authoritative. The HTML is a prototype with sample data and simulated operations, not an implementation to ship or a test substitute.

In particular, the final application must improve on the reference: denser adjustable rows, subtly colored icons, alternating row backgrounds, original native context menus, the existing column-header chooser, and preserved original menus. Do not reproduce misleading sample labels, artificial thumbnail support, simplified menus, or mock behavior.

Aim for a compact, polished desktop utility: clear hierarchy, restrained rounded controls, consistent spacing, readable typography, lightly colored icons, and useful density. Avoid oversized padding or sacrificing file-list space. Support light and dark system appearances with appropriate contrast. Apply changes within Panero; do not change the user's global desktop theme or other applications.

## 3. Repository and technical foundation

1. Inspect the environment, applicable repository instructions, available tools, GitHub authentication, distro, dependencies, and existing work before changing anything. Use an appropriate existing checkout when available; do not overwrite unrelated projects.
2. Use canonical upstream `https://invent.kde.org/utilities/krusader` and the KDE GitHub mirror where useful for a GitHub fork. Preserve Git history, authorship, licenses, and an upstream remote. Create a **public repository under my GitHub account**, preferably `panero`; use `panero-file-manager` if the name is occupied by an unrelated repository. Inspect an existing related repository before reusing it. Ask only if the account/owner cannot be determined or authentication is required.
3. Select and pin a suitable stable upstream tag/commit. Prefer a maintained base that can be reliably packaged for the target distribution. Inspect actual Qt/KDE requirements: a newer upstream Qt6/KF6 branch may require dependencies unavailable in the Ubuntu 24.04 repositories. Do not blindly select master or assume newer dependencies are installable. A compatible stable Qt5/KF5 base is acceptable when justified. Document the choice and update implications. Do not solve this by upgrading the user's OS or adding untrusted repositories. If necessary, build required dependencies in an isolated, reproducible environment and package them lawfully, rather than leaving the target with unresolved dependencies.
4. Establish a successful baseline build and record how to reproduce it. Implement changes on a feature branch with reviewable commits. Use the existing native action, model/view, KIO, settings, and file-operation mechanisms wherever possible.
5. Create a Panero-specific executable, package, desktop entry, icons, application identity, configuration, cache/data paths, and D-Bus/single-instance identifiers where applicable. Derive any reverse-domain identifier from the real project owner; do not impersonate `org.kde`. Panero must install and run alongside official Krusader without file collisions or overwriting its settings. Offer any settings import explicitly and keep the original configuration intact.
6. Do not blindly replace every occurrence of “Krusader”: preserve historical attribution and any compatibility-sensitive identifiers that need deliberate handling. Update user-facing product branding, own issue links, and application metadata appropriately.

I authorize the work necessary to implement this project: creating the public fork/repository, installing reputable build/test dependencies through normal privilege flows, committing and pushing changes, using branches and PRs in my own repository, merging ready changes there, running GitHub Actions, and publishing the resulting tested release. Do not ask again for routine permission already covered here. Do not submit upstream PRs, contact maintainers, purchase services, register trademarks, or change unrelated repositories/accounts. Never ask me to paste passwords or access tokens into chat; use normal login and OS authorization flows.

## 4. Copyright, licensing, and publication

Handle the fork's legal and distribution requirements as part of implementation, not as a final placeholder:

- [Krusader publicly describes its license](https://krusader.org/) as **GPL version 2 or later**. Inspect the chosen checkout, per-file notices/SPDX identifiers, bundled assets, and dependencies to establish the applicable licenses precisely. Preserve upstream copyright notices, author credits, warranty notices, and license texts. Observe requirements to identify modifications and dates where applicable. Attribute new human contributions accurately; do not claim ownership of upstream work.
- Keep the derivative under compatible GPL terms. Do not relicense the entire fork under MIT, proprietary terms, or a different blanket license. Keep the permissions users receive with the software.
- Clearly state in README and About: **“Panero is an independent fork of Krusader. It is not affiliated with or endorsed by KDE or the Krusader maintainers.”** Preserve upstream acknowledgements and useful documentation while pointing Panero support and bug reporting to this repository.
- Audit redistributed Qt/KDE components, plugins, icons, fonts, and other assets. Include required license texts, copyright/third-party notices, and source availability. Prefer system packages for runtime libraries when appropriate. Do not assume a GitHub-generated zip of Panero alone fulfills source obligations for third-party code bundled into a binary.
- Publish corresponding source matching each distributed binary, including necessary modifications and build/install/packaging scripts. Provide a source release archive with the exact tag/commit and any further material required by the actual licenses. Put license/copyright information in the installed package as well as the repository, with source and credits links accessible from About.
- Include a readable licensing/attribution document and a dependency/asset provenance inventory. Record any concrete unresolved obligation accurately; do not claim a compliance audit passed without checking it.
- Redraw the chosen icon into an original editable SVG suitable for production. Document that its reference was AI generated and document the final asset's provenance and applicable permissions accurately. Do not invent exclusive copyright or trademark rights over it, copy another application's logo, or retain the Krusader/KDE logo as Panero's branding.

## 5. Main window, toolbar, and navigation

Keep a true dual-panel layout with independent locations, per-panel tabs, breadcrumbs/location entry, and a clearly visible active panel.

### Main toolbar

Include working native actions for:

- **Up, Home, Refresh**.
- **Compare**: content comparison using optional host-installed KDiff3, as specified below.
- **Pack and Unpack**: reuse the actual archive functions, with correct source/target selection and existing format capabilities.
- **Search**: launch the existing capable search UI.
- **Trash**: open the original trash location (`trash:/`) in the active panel, using existing functionality. This button opens Trash; it does not delete the current selection.
- Convenient **Hidden files**, **Thumbnails/Details**, **Tree**, and **View & Colors** controls, placed in a compact toolbar/view strip as appropriate.

Remove Copy, Move, and New Folder from the **default main toolbar** because the bottom function bar already exposes them. Preserve these commands elsewhere. Do not show Sync in the default toolbar; preserve the original Synchronizer and its menu access. Reuse existing actions and shortcuts instead of creating competing implementations.

### Drives, tree, and pins

- Provide a visible **drive selector for each panel**, using real mounted/removable devices and existing Krusader/Solid/KIO mechanisms. Retain normal OS mount/authentication behavior. Handle missing, unmounted, and disconnected devices gracefully.
- Add a **single folder-tree sidebar on the left** that can be shown or hidden. It follows and navigates the **active panel**, including the right panel when that is active. Clearly convey the destination panel.
- Provide persistent **pinned favorite folders** in the sidebar with practical pin/unpin access. Preserve full locations and relevant drive identities. Opening a pin navigates the active panel; unavailable locations should show a useful recoverable error.
- Keep tree expansion, scrolling, visibility, and sensible panel state across focus changes and sessions. Lazy-load large trees and avoid blocking network/removable-device operations on the UI thread. Support keyboard navigation and accessible controls.

## 6. File views and customization

- Preserve a compact, sortable detailed file view. Provide a separate **Extension** column with sensible handling of directories, hidden files, extensionless files, and multiple suffixes. Avoid awkward duplicate extensions between Name and Extension when this layout is active; opening/renaming must always use the actual filename.
- **Ordinary short right-click on a column header opens Krusader's native column chooser immediately.** Retain useful existing show/hide, order, resize, and sort behavior. Column controls must not be hidden only inside View & Colors. Preserve per-panel column settings where upstream supports them.
- Use **alternating row backgrounds** in details mode. Allow users to customize both stripe colors, with separate appropriate light/dark settings if needed. Current-row and marked-row rendering must remain clear over either stripe.
- Make row height adjustable **below the prototype's 29 px minimum**. Aim for roughly **18–48 logical pixels**, permitting about 18–20 px with suitable small fonts/icons. Respect font metrics and scaling so content does not clip. Provide a compact preset, a comfortable default, persistent settings, and responsive HiDPI behavior.
- Provide a **thumbnail mode/toggle** using actual supported KDE/KIO previews. Use modern, slightly colored native icons when a preview is unavailable. The concept's illustrative CAD/photo/document thumbnails are not evidence of available previewers; do not fake unsupported previews. Keep thumbnail generation asynchronous and bounded.
- Include reliable icon fallbacks so toolbar and file icons appear on Zorin's non-KDE desktop. Declare/install necessary icon-theme, SVG rendering, and plugin dependencies or lawfully supply scoped fallbacks. Avoid reproducing the original missing-icons problem. Do not globally replace the desktop icon theme.
- Make hidden-file visibility easy to toggle and preserve the existing shortcut/action. “View & Colors” should offer practical view mode, row density, mark/current colors, stripe colors, and related appearance preferences, with reset-to-defaults. Keep the original settings UI/functions accessible.
- Hide the embedded **command line by default**, but retain its original toggle and behavior. Keep native terminal access available. Do not turn it into a prominent permanent extra row.

## 7. Commander selection and original context menus

Implement an explicit, testable input model. Prefer a configurable Commander profile, selected by default for Panero, while retaining useful upstream alternatives.

### Cursor versus persistent marks

- Maintain a **current/keyboard row** separately from a **set of marked files**.
- Give the current row a clearly visible blue focus treatment, such as a solid border/background and leading indicator. A faint dotted frame is insufficient. Moving Up/Down must move that treatment cleanly; with no marks, the old row must not remain blue as if selected.
- Render persistent marks in a contrasting **red by default**, with user-adjustable colors and a check/indicator so status does not rely only on color. Ensure a marked current row still communicates both states.
- **Short right-click toggles the item's mark. Space toggles the current item's mark.** Preserve sensible Insert-based behavior if available.
- Plain left-click and Up/Down move the current row **without clearing existing marks**. Mouse movement does not clear marks. Focus and panel switching do not clear marks.
- Keep marks tied to stable item identity as far as practical through sorting, refresh of existing items, and Details/Thumbnails changes. Remove obsolete entries when files disappear. Do not retain hidden operation targets from a previous directory. Provide an explicit, discoverable Clear Marks action. Escape should first dismiss open UI and must not unexpectedly erase the marked set.
- File operations must consistently use marked files when there are marks, otherwise the current file where appropriate. Show the actual target set/count in native confirmations. Preserve upstream modifier/range selection where compatible and document chosen behavior.

### Hold right-click for the native context menu

- **Holding the right button for approximately 600 ms**, configurable, opens the **original Krusader/KDE system file context menu**, including available Open With, Properties, and applicable service/plugin actions. Do not substitute a custom menu containing a few imitation actions. On GNOME/Zorin this means the native context providers used by Krusader; it does not require embedding another file manager's menu implementation.
- If there are marked items and the press is on an item, the context menu targets the applicable marked set. One marked file means that file. With no marks, it targets the item under the pointer. A hold on empty space opens the **current folder/background** context menu even when marks exist.
- A long press must not first toggle a mark, clear marks, or fire a second short-click action on release. Handle movement beyond a drag threshold, canceled presses, lost focus, release outside, drag initiation, and menu dismissal deliberately. Keep normal drag/drop useful.
- **Column headers are an exception:** their ordinary short right-click immediately opens the original column chooser, without the long-press gesture.
- Provide keyboard access through Menu/Shift+F10 and accessible alternatives. Preserve the context target set when invoking actions, and revalidate paths if files change before an operation.

## 8. Preserve original menus, functions, and shortcuts

Keep upstream menus—File, Edit, Go, View, Useractions, Tools, Window, Settings, Help, or their actual equivalents in the chosen base—and their functions wherever compatible. Add or reposition controls thoughtfully instead of rebuilding a reduced menu set from the mockup. Preserve remote protocols, bookmarks, archive browsing, transfer queues, viewer/editor, search, user actions, settings, and other upstream capabilities.

Retain the native bottom function-key bar with actual actions and existing configured shortcuts: viewing/editing, copy, move, new folder, delete, and relevant rename/terminal/quit actions according to the upstream keymap. Verify mappings in code instead of copying mockup labels blindly; do not replace an existing F10 Quit binding with “Menu” just because the concept showed it. Preserve context-specific dialog shortcuts. Maintain keyboard shortcut customization.

Use the native file-operation machinery for real operations, cancellation, overwrite handling, errors, and progress. Do not implement destructive operations directly in view event handlers or route them through unsafe interpolated shell commands. Preserve normal Trash versus permanent-delete distinctions. Never run the whole file manager as root to make routine features work.

## 9. KDiff3 integration and installation

Use **KDiff3** as the default content comparison tool when installed. Reuse/extend Krusader's existing external comparison support. Preserve the distinction between **Compare by Content**, upstream name/date **Compare Folders**, and Synchronizer; do not relabel a name/date check as a content diff.

- Support the normal case of one file from each panel, and two marked files in one panel if feasible within the upstream action model. Make target selection deterministic. Provide a deliberate folder-comparison path where supported by KDiff3. Show clear guidance for invalid selections and unsupported inputs; do not claim semantic comparison of arbitrary CAD, binary, or image formats.
- Detect the host executable or a user-configured path. Launch safely with argument arrays, including filenames with spaces/unicode. Keep KDiff3 an **optional dependency**; do not make package installation mandatory just to run Panero.
- If it is missing and Compare is requested, ask **“Install KDiff3?”**, with Install and Cancel. On consent, use the platform's normal package manager/PackageKit and PolicyKit or another appropriate supported installation UI. Show progress and handle canceled authentication, package/network errors, and missing installer services. Do not request passwords in the app or chat or silently install it at startup.
- After successful installation, re-detect KDiff3 and resume or re-offer the pending comparison after revalidating the files. If automated installation is unavailable, give accurate actionable guidance and a retry path.
- You may install KDiff3 in the test environment using the normal authorization flow to verify the real integration. Test the missing-dependency path in an isolated environment rather than uninstalling a user's existing installation.

## 10. Debian packaging, CI, and GitHub release

Produce a normal **`panero_<version>_amd64.deb`** with correct metadata, dependencies, copyright/license files, desktop entry, icon sizes, install paths, and clean removal. It must coexist with Krusader. Prefer distro-provided shared libraries and accurately generated dependencies. Avoid unresolvable library versions, undeclared runtime plugins, machine-specific paths, or requiring a development checkout at runtime.

The package should install through Zorin's package UI. Document an exact terminal fallback such as `sudo apt install ./panero_<version>_amd64.deb`, using the actual released filename. Provide launch, upgrade, uninstall, configuration-location, and troubleshooting instructions. Do not add an APT repository, updater daemon, global file associations, or change the default file manager without a separate user request.

Create GitHub Actions workflows that build and run suitable tests on PRs/pushes, and package/release from version tags or an intentional manual release workflow. Pin source/dependency inputs appropriately and record the actual build environment. Use minimal required token permissions, keep secrets out of logs, and avoid privileged execution of untrusted PR code.

For a release, publish **downloadable assets in GitHub Releases**, not just expiring workflow artifacts:

1. The tested amd64 `.deb`.
2. Corresponding source/build material and required licensing/third-party notices.
3. SHA-256 checksums and concise release notes with target OS, installation steps, known limitations, and source commit.

An honest initial version such as `v0.1.0-preview.1` is appropriate; use a valid Debian version mapping. Mark it prerelease when warranted. Do not call it production-stable based only on compilation. Verify that the published assets exist, match the tested build/commit, can be downloaded, and have correct checksums. Check package upgrades and removal. No AppImage workflow or deliverable is needed.

## 11. Verification and completion

Use meaningful native tests for the changed behavior and actual GUI/package checks. Prototype HTML tests do not validate the C++ application. Use temporary data and isolated settings for tests; never delete real user files, empty their Trash, unmount their drives, or disturb their existing configurations.

Verify at least:

- A clean build, relevant existing tests, and new focused tests for cursor/marks, short/long right-click event sequencing, operation target selection, and persisted appearance/view settings.
- Current-row visibility, retained marks through left-click/arrows/panel changes, explicit clearing, vanished items, empty space, one/multiple marks, keyboard context menus, and immediate header column menus. Verify no double-trigger after a hold.
- Actual original context-menu providers/actions; both panels; tabs; active-panel tree; pins; drive selector behavior; details, Extension, sorting, column persistence; colored icons and fallbacks; thumbnails; hidden files; density and stripe customization; light/dark and relevant display scaling.
- Real copy/move/rename/new folder/search and pack/unpack operations with disposable files, including spaces/unicode, overwrite cancellation, and representative symlinks. Check archive contents. Exercise Trash/restore in an isolated test account/session or properly isolated test environment.
- KDiff3 launching with real files; absent tool, install consent/cancel, failed installation, retry, and successful detection. Exercise real integration without altering the user's existing packages unnecessarily.
- Install, launch, upgrade, and uninstall the `.deb` in a clean target-compatible environment, without the source/build tree or development dependencies. Verify desktop launcher/icon, runtime plugins, and coexistence with official Krusader. Test a real GUI on Zorin 18.1 if available; an Ubuntu 24.04 container is useful for packaging but is not proof that the Zorin desktop was tested. Cover Wayland/X11 as available and report actual coverage honestly.
- Correspondence between release tag, binary, source, licenses, and checksums. Confirm no private keys, credentials, local personal paths, or real user data were committed.

Use available tooling or a disposable VM/container to overcome environmental limitations. If a required check is unavailable, explain precisely what was and was not tested, continue other work, and seek only the access or decision genuinely needed. Do not fabricate results or silently declare incomplete core features finished.

The task is complete when the public source repository contains the functioning native fork, required features are implemented and meaningfully verified, automated builds work, and a usable `.deb` plus corresponding source/notices/checksums are published. Do not stop at a plan, UI skeleton, local-only build, or untested draft PR. If a concrete blocker prevents completion, identify it and leave the work reproducible and ready to resume.

Finish with the public repository link, release/download link, exact install command, screenshots of the actual application, tested environments/results, and any material limitations. Include a short user guide explaining marks versus cursor, short/held right-click, the column chooser, pins/tree, KDiff3 installation, appearance settings, and obtaining future releases.

## Official reference sources

Recheck sources relevant to the chosen version. The source tree and licenses govern implementation details.

- [Krusader project and GPL statement](https://krusader.org/)
- [Canonical source](https://invent.kde.org/utilities/krusader)
- [KDE GitHub mirror](https://github.com/KDE/krusader)
- [Native comparison behavior](https://docs.kde.org/trunk_kf6/en/krusader/krusader/compare.html)
- [KDiff3](https://apps.kde.org/kdiff3/)
- [Zorin version/platform details](https://zorin.com/os/details/)
- [Zorin installation guidance](https://help.zorin.com/docs/apps-games/install-apps/)

Begin by inspecting the references, repository/authentication state, and target-compatible build options. Then carry out the project.
