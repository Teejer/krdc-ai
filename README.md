# krdc-ai

[KRDC](https://apps.kde.org/krdc/) (KDE Remote Desktop Client) with an
integrated **AI assistant panel**: a chat dock on the right that can look at
the remote machine and, with your approval, drive it.

This repository is the **packaging and distribution** side of the project.
The feature itself is maintained as a patch series on top of upstream KRDC
and is under review upstream:

- KDE merge request: **[network/krdc !259](https://invent.kde.org/network/krdc/-/merge_requests/259)**
- Live AUR package: **[aur.archlinux.org/packages/krdc-ai](https://aur.archlinux.org/packages/krdc-ai)**
- Source branch (browsable, full upstream history): [`ai-assistant`](../../tree/ai-assistant)

## What it does

- **Sees the remote machine** — screenshots taken from the session's own
  video buffer (no extra protocols, no server agents), scaled and sent to
  any OpenAI-compatible vision model (Ollama, llama.cpp, openai.com, ...).
- **Drives it** — tools for click, double-click, drag, type, key chords,
  scroll and wait, injected through each backend's native input path
  (RDP: FreeRDP scancode/Unicode events on the existing session loop).
- **Always asks before acting** — every state-changing tool call needs
  Approve/Deny. An auto-execute checkbox exists for convenience but is
  *explicitly overridden* for destructive actions (`rm -rf`, shutdown,
  disk tools, `Ctrl+Alt+Del`, `Win+L`, ...): those always prompt.
- **One conversation per connection** — chat follows a server across
  reconnects and tab switches, persisted per connection key.
- **Polite by design** — the dock is hidden until you turn it on, then
  remembered like any other dock; API keys go to KWallet; model discovery
  from the endpoint; `-DWITH_AI=NO` builds a binary with zero AI symbols.

## Install

Arch: `yay -S krdc-ai` (replaces the repo `krdc`, config carries over).

Manual: clone upstream [invent.kde.org/network/krdc](https://invent.kde.org/network/krdc)
at the commit pinned in `pkg/PKGBUILD`, apply `pkg/krdc-ai.patch`, build
with CMake (`WITH_AI` is on by default).

## Repository layout

| Path | Contents |
|---|---|
| `pkg/` | AUR package: `PKGBUILD`, `krdc-ai.patch` (the whole feature, squashed), `.SRCINFO`, packaging README |
| `MERGE_REQUEST.md` | The upstream MR description + submission record |
| `ai-assistant` branch (in `krdc/` history) | The six-commit review series |

## Status

The RDP backend is complete and tested (66-assertion engine test against a
mock OpenAI server, plus manual RDP sessions). VNC/SPICE backends are
follow-ups; the panel plugs into backends through a small `RemoteView`
virtual API, so they are additive.

## License

GPL-2.0-or-later. KRDC code is copyright its respective authors; the AI
assistant code is copyright TJ Griffiths.
