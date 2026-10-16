// SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "krdccore_export.h"

#include <QImage>

/**
 * A client-side "smart invert" that recolors light application windows to a
 * dark scheme without inverting the whole screen.
 *
 * The filter remaps the brightness of window chrome - near-white
 * backgrounds, grey panels and near-black text - through a light-to-dark
 * curve. Saturated pixels (photo content, colorful icons) are kept
 * untouched, so pictures keep looking like pictures. Structural rules refine
 * that split, each keyed on pixel neighborhoods rather than color: a uniform
 * light neighborhood is a surface whatever its tint (tinted chrome bands
 * darken with the rest of the window); a thin band of saturated pixels
 * between a black glyph core and light chrome is ClearType antialiasing and
 * neutralizes to grey (no colored halos around text); a thin dark saturated
 * stroke standing on clearly brighter pixels is colored text and lifts to a
 * readable light tone of its own hue (GPMC's blue section headers stay
 * legible on darkened bands); a thin bright stroke in a broad dark region is
 * light text and stays bright (desktop labels, the taskbar clock); a flat
 * dark neighborhood is the remote's own dark-mode chrome and is left alone
 * (the Start menu's panel, which the curve would otherwise flip to a glowing
 * slab); and a mostly-dark band docked to a screen edge is the taskbar,
 * passed through raw so its Start button, icons and clock text - light-on-dark
 * pixels the per-pixel rules mangle - survive whole.
 *
 * It cannot tell a white window background from a white document page: both
 * are flat white pixels. Only the remote OS itself knows that difference, so
 * for perfect results the remote should use its native dark mode; this filter
 * is for the apps and operating systems that do not have one. RDP sessions
 * additionally ask the server to disable ClearType font smoothing at connect
 * time: subpixel rendering paints glyph edges with colored pixels that this
 * filter cannot distinguish from colorful content, and they would remain as
 * colored halos around recolored text.
 *
 * medianLuma() and brightFraction() support an auto-bias: a screen that is
 * light, or still has wide white windows under a dark overlay such as the
 * Start menu, is filtered; one that is genuinely dark (native dark mode, or
 * full-screen video) is passed through untouched. The view repaints fully
 * whenever that decision flips, so no region keeps pixels painted under the
 * other state.
 */
namespace SmartInvert
{
/**
 * Median luminance (0-255) of the image, estimated on a subsample.
 * Above ~150 the screen reads as a light theme, below ~110 as a dark one.
 */
KRDCCORE_EXPORT int medianLuma(const QImage &image);

/**
 * Percent (0-100) of subsampled pixels with luma above 200 - white window
 * chrome. This is the guard medianLuma() needs: a dark Start menu opened
 * over a white desktop drops the median to ~50 while a third of the screen
 * is still white chrome, and only medianLuma() cannot tell that from the
 * remote switching to native dark mode or a fullscreen video, where white
 * survives only as sparse text and icons (well under 15 percent).
 */
KRDCCORE_EXPORT int brightFraction(const QImage &image);

/**
 * Recolor the image in place. Low-saturation pixels have their brightness
 * mapped through a smooth, monotone light-to-dark curve; saturated pixels
 * are preserved unless they match the structural rules described above
 * (ClearType fringes, lifted colored text) or sit in a flat light surface.
 * Expects Format_RGB32; other formats are left unchanged.
 */
KRDCCORE_EXPORT void apply(QImage &image);
}
