# Merge request — SUBMITTED

**MR:** https://invent.kde.org/network/krdc/-/merge_requests/259
**State:** opened against krdc/master from teejer/krdc@ai-assistant (fork
created via API, branch pushed over SSH, MR + maintainer mention posted via
API on 2026-10-08; the temporary access token was shredded and should be
revoked at invent.kde.org → Preferences → Access Tokens).

The commit series was rewritten to author `TJ Griffiths <teej.d.man@gmail.com>`
because invent.kde.org's pre-receive audit hook rejects non-full-name authors;
the SPDX headers in the new files match. Reviewer assignment via API is
limited to project members, so Fabio Bas (@ctrlaltca, the active RDP backend
maintainer) was cc'd with a mention comment instead.

Below is the description as posted, kept for reference.

## Description

This adds an optional AI assistant dock to the main window: a chat panel
on the right that can look at the remote machine and, with user approval,
drive it.

The assistant talks to any OpenAI-compatible endpoint (Ollama, llama.cpp,
openai.com, ...). It sees the remote screen through screenshots taken
from the session's own video buffer, and acts through a set of tools
(click, double-click, drag, type, key chord, scroll, wait, screen info)
injected over each backend's native input path — for RDP, FreeRDP
scancode/Unicode input events queued onto the existing session loop.

**Safety model** — every state-changing tool call is shown to the user
with the screenshot region it refers to and needs Approve/Deny. There is
an "auto-execute" checkbox for convenience, but it is explicitly
overridden for anything matching a destructive pattern (disk/shutdown/
package-removal commands, `Ctrl+Alt+Del`, `Win+L`, ...): those always ask.

**Design notes**

- The reusable part is a small virtual API on `RemoteView`
  (`supportsAiControl()`, `aiMouseMove/Button/Wheel/KeyPress`,
  `aiTypeText`, `connectionKey()`); backends opt in one at a time. Only
  RDP implements it here; VNC/SPICE could follow the same pattern.
- The panel itself is compiled into the `krdc` executable behind
  `option(WITH_AI ON)` (it needs the main window's docks and action
  collection), gated by `BUILD_AI_ASSISTANT`. `-DWITH_AI=NO` produces a
  binary with zero AI symbols.
- One conversation per connection, keyed by the connection URL so chat
  history follows a server across reconnects and tab switches; persisted
  as JSON under `AppDataLocation/ai/`.
- The dock is hidden by default and, like the other docks, remembers
  being turned on. API keys go to KWallet.
- `setKeyboardGrabSuspended()` lets the chat field receive keys while a
  full-screen "grab all keys" session is running, without touching the
  user's setting.

The first two commits are independent bugfix/API changes and can be
reviewed (or landed) on their own.

**Testing**

- `test/run_ai_test.sh` (behind `-DBUILD_AI_ENGINE_TEST=YES`) drives the
  conversation engine against a mock OpenAI server: approval loop,
  always-ask rule, deny reporting, per-connection history persistence,
  risk classification, key mapping, URL derivation — 66 assertions.
- Manual: RDP against Windows and Linux hosts with Ollama
  (vision + tool-calling) for the full click/type loop.

**Known scope for follow-ups** (happy to do in follow-ups): VNC/SPICE
backends, i18n coverage polish, documentation page under `krdc/doc`.

## Suggested reviewers

- ahiemstra (FreeRDP backend author)
- (check `MAO`/git log for current krdc maintainer before submitting)

## Before submitting checklist

- [ ] Fork https://invent.kde.org/network/krdc on invent.kde.org (KDE
      account, same login as identity.kde.org)
- [ ] `git remote add fork git@invent.kde.org:<your-username>/krdc.git`
      (or the https URL)
- [ ] `git push fork ai-assistant`
- [ ] Open the MR from `ai-assistant` → `master`, paste the description
      above
- [ ] CI will run REUSE (SPDX headers are present on all new files) and
      the standard build jobs. KRDC has no `.clang-format` in-tree, so
      there is no formatting gate; the new code follows the surrounding
      KDE style (4-space indent, same brace style).
- [ ] KDE requires contributors to agree to the developer certificate of
      origin; commits carry your real-name author header already

## Commit series (as pushed)

1. `c36ea776` Add an AI control API to RemoteView
2. `9acfd97f` Give the remote view the keyboard focus when it is clicked
3. `124f3d23` Implement AI control for RDP sessions
4. `9c153da1` Add an AI assistant chat panel
5. `41f4bdfe` Show the AI assistant dock in the main window
6. `4e276020` Add an engine test for the AI assistant
