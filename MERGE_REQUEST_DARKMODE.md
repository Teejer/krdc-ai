# MR: Add an optional dark mode filter for remote screens

**Create it here:** https://invent.kde.org/teejer/krdc/-/merge_requests/new?merge_request%5Bsource_branch%5D=dark-mode-filter

- **Fork branch:** `teejer/krdc:dark-mode-filter` (3 commits, cut from master `5876dc36`)
- **Independent of !259** — no AI code, no shared files beyond the same
  `remoteview`/`rdpview`/`mainwindow` files touched at different places.
  Either MR can land, be rejected, or be reworked without affecting the other.
- Attach the on/off screenshot pairs from `~/Pictures/Screenshots` (Explorer
  and GPMC, filter off vs on) in the MR description editor.

---

## Title

Add an optional dark mode filter for remote screens (SmartInvert)

## Description

Windows Server has no dark theme, and its daily-driver tools — File
Explorer and the mmc.exe consoles (Group Policy Management, Active
Directory Users and Computers, Event Viewer, Server Manager) — are flat
white. This adds an opt-in, client-side filter that recolors the
displayed image to a dark scheme. Nothing is sent to the remote machine
and the video buffer is never modified; a filtered copy is painted on
top of the raw buffer, so screenshots and any other consumer of
`takeScreenshot()` still see real pixels.

The filter is **off by default** (Settings menu entry, remembered,
Ctrl+D to flip mid-session) and auto-passes screens that are already
dark (native dark mode, a fullscreen video) via median + white-chrome
hysteresis, so a dark remote never sees it.

### Caveats — please read before trying it

This is a heuristic working from pixels; only the remote OS itself knows
whether a flat white area is a window background or a document page. In
exchange for that honesty it has known edges:

- **Text and icons can be affected.** ClearType subpixel rendering bakes
  cyan/orange colors into glyph edges; the filter neutralizes those
  fringes, but text rendering is visibly best when **ClearType / font
  smoothing is turned off on the remote** — the RDP integration asks
  the server for exactly that at connect time (the mstsc "font
  smoothing" untick; a group policy can override the request). Thin
  colorful icon detail next to dark outlines can occasionally be
  flattened to grey, and colorful text may be treated as content or as
  text depending on its neighborhood. It is not pixel-perfect.
- White document pages inside a white app darken too — same pixels.
  For perfect results the remote should use its native dark mode; this
  filter is for the apps and operating systems that do not have one.

That said, in daily use against Server 2012/2022 it is **very
comfortable**: it works especially well on mmc.exe consoles and File
Explorer, and with font smoothing off the text recolors cleanly with
no halos. I have been running it for weeks as the primary way I use
these machines.

### How it works

All rules key on pixel *structure* rather than color, so they survive
arbitrary themes and tints:

- a uniform light neighborhood is chrome whatever its tint (tinted MMC
  row bands darken with the window, clamped dark);
- a thin saturated band between a black glyph core and light chrome is
  ClearType antialiasing and neutralizes to grey (no colored halos);
- a thin dark saturated stroke on clearly brighter neighbors is colored
  text and lifts to a readable light tone of its own hue (GPMC's blue
  section headers survive their darkened band);
- a thin bright stroke in a broad region that stays dark is light text
  (desktop labels, Start menu clock) and stays bright;
- a flat dark neighborhood is the remote's own dark chrome (Start menu
  panel) and is untouched, instead of the curve flipping it into a
  glowing slab;
- a mostly-dark band docked to a screen edge is the taskbar, passed
  through whole;
- saturated pixels (photos, icons) are preserved, so picture content
  keeps looking like picture content.

`medianLuma()` / `brightFraction()` drive an engage/disengage
hysteresis so already-dark screens pass through untouched; flips
repaint the whole view so no stale region survives.

### Commits

1. `Add SmartInvert, a client-side dark mode filter for remote screens`
   — `core/smartinvert.{h,cpp}` (self-contained) + `test/smartinvert_test.cpp`
   (QTest, no display or connection needed, expectations measured from
   real Server 2012 captures; builds under the existing `BUILD_TEST` gate).
2. `Apply the dark mode filter to RDP sessions` — `RemoteView::setDarkModeFilter()`
   virtual (no-op default: VNC/SPICE unaffected), RdpView paint path with
   hysteresis + recheck timer, and the connect-time font-smoothing
   request.
3. `Add a Dark Mode Filter setting with a Ctrl+D shortcut` — kcfg entry,
   Settings menu action, window-level shortcut.
