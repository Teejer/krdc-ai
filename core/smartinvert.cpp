// SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "smartinvert.h"

#include <QVector>

#include <algorithm>
#include <array>
#include <cstdlib>

namespace {
/**
 * Brightness curve for the light-to-dark remap, in three pieces:
 *
 * - the text end (v <= s_textCutoff): near-black becomes near-white
 *   (0 -> 248, 90 -> 228). Dark theme text is white on dark grey, and
 *   landing at "light grey" instead read as cloudy in File Explorer.
 * - the blend (to s_blendEnd): interpolates into the chrome curve so
 *   antialiased edges between text and background stay monotone and smooth.
 * - the chrome end (v >= s_blendEnd): 225 - 3v/4. White (255) becomes dark
 *   grey (34) and mid greys cross the identity around 130, keeping the
 *   low-contrast feel of real dark themes. The output stops short of pure
 *   black and pure white.
 */
constexpr int s_textCutoff = 90;
constexpr int s_blendEnd = 130;

constexpr std::array<int, 256> buildCurve()
{
    std::array<int, 256> curve{};
    for (int v = 0; v < 256; ++v) {
        if (v <= s_textCutoff) {
            curve[v] = 248 - v * 20 / s_textCutoff;
        } else if (v < s_blendEnd) {
            const int text = 248 - v * 20 / s_textCutoff;
            const int chrome = 225 - (v * 3) / 4;
            const int w = v - s_textCutoff; // 0..40
            curve[v] = (text * (s_blendEnd - s_textCutoff - w) + chrome * w) / (s_blendEnd - s_textCutoff);
        } else {
            curve[v] = 225 - (v * 3) / 4;
        }
    }
    return curve;
}

constexpr auto s_curve = buildCurve();

// Per-pixel content rule. A pixel with saturation below s_satKeep is window
// chrome and fully recolored; above s_satKeep + s_satKeepRange it is content
// and untouched; in between it fades.
constexpr int s_satKeep = 40;
constexpr int s_satKeepRange = 60;

// Flat surface rule. Tinting window chrome is common (GPMC's rows are
// (159, 183, 207), MMC's dialogs (192, 208, 224)) and a mild tint slips past
// the saturation rule, so those rows stayed light while the rest of the
// window went dark, taking their text readability with them. A surface is
// recognizable by structure rather than color: within a 9x9 window it is
// uniform, whereas icons, photos and text always vary. A pixel in such a
// uniform, clearly light neighborhood is a surface and recolors fully,
// whatever its tint. Surfaces land extra dark (output clamped to
// s_surfMax): the brightness curve's identity crossing near luma 128 keeps
// mid-tone chrome readable, but Aero-era panels (GPMC's rows are
// (160, 186, 203)) are then "lighter than the window" instead of the
// slightly-lighter-than-black bands a real dark theme paints. Dark uniform
// areas (a Server 2012 taskbar is (0, 3, 8)) are already dark-mode and left
// alone, and strongly colored uniform areas (own saturation above
// s_flatOwnSat) stay content.
constexpr int s_flatRadius = 4;
constexpr int s_flatSpread = 28; // max minus min luma across the window
constexpr int s_flatMean = 130; // the window must average this bright
constexpr int s_flatOwnSat = 120;
constexpr int s_surfMax = 60; // surfaces never land lighter than this

// The mirror rule: a flat DARK surface - a neighborhood that averages very
// dark - is chrome the remote already renders dark-mode, and must not be
// brightened. The Server 2012 Start menu paints its app list over (31, 31,
// 31); without this rule the curve flipped that panel to (242) and turned
// the menu into a glaring white slab the moment the filter learned to stay
// engaged behind it. This test is mean-based, not spread-based like the
// light rule above: panel pixels right next to text glyphs have glyph
// antialiasing inside their window (a dense text row pulls the average to
// ~85), and the mean shrugs sparse intruders off while a spread test fails
// on a single luma-94 pixel. The Start menu's dark dots still recolor: a
// dither window averages its bright dots in, well over any dark-surface
// bound. Dark-surface pixels also count as dark support for the light text
// rule below, colored-dark pixels (taskbar, wallpaper) aside: they are
// pixels that stay dark after filtering, which is exactly what white text
// needs to stand on.
constexpr int s_darkOwnMax = 100; // the pixel itself must be this dark or darker
constexpr int s_darkMean = 100; // and its window average this dark

// Light text protection, the mirror of the colored text lift below. Windows
// paints desktop icon labels and the taskbar clock as white text over the
// wallpaper, which on this remote ((0, 27, 58)) is dark before the filter
// ever runs - and the filter dutifully turned the white glyphs dark, onto a
// background it had (correctly) left dark: dark-on-dark. The signature is
// the mirror too: a thin, bright, weakly saturated pixel standing in a big
// dark region - a 15x15 window at least s_keepBroadMin dark (s_keepDarkDelta
// below the pixel), with a 9x9 core where dark neighbors still outnumber
// anything brighter still - is light text, and is left alone. Both halves
// matter: the white antialiasing pixel between a black glyph and a white
// page fails "brighter still", and the white pixel inside the counter of a
// dark title letter fails the broad window, while the white glyph over
// wallpaper passes both. The thinness bound is a touch looser here (5 rather
// than 3) because label font junction pixels read 4-5 own-tone pixels on
// both axes, and the broad-dark test already excludes the white-page cases
// that keep the other rules strict.
//
// The broad window counts dark pixels that STAY dark after filtering:
// colored dark ones (a Server taskbar ((0, 3, 8)) or this wallpaper
// ((0, 27, 58)) - saturated, preserved by the saturation rule) and flat
// dark surfaces (the Start menu's (31, 31, 31) panel, preserved by the
// dark surface rule above). Plain neutral dark pixels that are neither -
// text, or the dithered translucency of a Start menu border over a white
// window - are exactly what the filter turns light, and must not count:
// without this the dither band masquerades as white-on-wallpaper text
// (its alternating dots make every white pixel "thin" with dark
// neighbors), and the filter preserved a 530 pixel strip of it as a
// glowing white border.
//
// And the window must be mostly dark: at most s_keepBrightMax of its 225
// pixels may themselves be light. Dither dots have dark support next door
// but sit in a half-white checkerboard (measured median 195 light pixels);
// real text has only its own strokes and neighbours (menu text median 48),
// so this bound closes the last of the band leak (473 kept pixels before
// these two guards, 17 after) while Start menu text 93%, desktop labels
// 110/117 and the clock 325/330 stay bright.
constexpr int s_keepMinLuma = 150;
constexpr int s_keepMaxSat = 80;
constexpr int s_keepMaxThin = 5;
constexpr int s_keepDarkDelta = 60; // a supporting neighbor is this much darker
constexpr int s_keepBroadRadius = 7;
constexpr int s_keepBroadMin = 120; // dark pixels needed in the 15x15 window
constexpr int s_keepBrightMax = 70; // light pixels tolerated in that window
constexpr int s_keepBrightDelta = 20; // "still brighter" defeats the AA pixel
// ClearType neutralization. Windows renders text with subpixel
// antialiasing, so a black glyph on white arrives with colored pixels baked
// into every edge: warm ones ((239, 205, 75), (180, 100, 4)) on one side of
// the stroke, cool ones ((145, 212, 247)) on the other. The RDP "no font
// smoothing" connect request is only a suggestion - servers under group
// policy ignore it - and the saturation rule must preserve these pixels or
// every colorful icon goes grey too, which left them as colored sparks
// ringing the recolored text: the halo. They give themselves away by
// structure: a saturated pixel wedged between a dark glyph core (luma at
// most s_fringeCore) and flat background white (luma at least s_fringeWhite,
// nearly unsaturated) within s_fringeRadius pixels. The core must be a near
// neutral black (saturation at most s_fringeCoreSat): ClearType fringes
// always ring a black glyph, whereas a colorful icon's dark outline is
// itself saturated - without that bound the fringe rule flattened the detail
// pixels inside small icons. Matched pixels are neutralized to their luma -
// the plain grayscale antialiasing ClearType was imitating - and recolor
// with their glyph, halo-free.
constexpr int s_fringeCore = 60;
constexpr int s_fringeCoreSat = 40;
constexpr int s_fringeWhite = 200;
constexpr int s_fringeWhiteSat = 50;
constexpr int s_fringeRadius = 3;

// Colored text lift. Not all text is black: GPMC's blue section headers
// arrive as (49, 49, 209) and its tree links as (0, 0, 46) - dark pixels
// with full color, which the saturation rule protects as content (it must,
// or every colorful icon goes grey). But protected means untouched, and a
// dark blue glyph on a surface that just went dark is invisible. The
// darkest pixel of such a stroke sits in the middle of the stroke itself, so
// "is there light chrome nearby" fails exactly where the text is darkest.
// The reliable signature is thinness: a text stroke is a few pixels wide, so
// along at least one axis a 9-pixel line through it is mostly not its own
// color, while any pixel of a solid icon - interior or edge - has a thick run
// of its own color along both axes. The same thinness gate now also guards
// the ClearType fringe rule: without it, colorful icon detail pixels sitting
// between a dark outline pixel and light chrome were being flattened to
// grey. A dark saturated pixel (luma at most s_liftMaxLuma) that passes the
// thinness test, and that has at least s_liftLightMin neighbors clearly
// brighter than itself (s_liftBgDelta luma) and not strongly colored
// (s_liftBgSat), is colored text. The background test is deliberately
// relative to the pixel rather than absolute: in dense text the window is
// full of other glyphs' antialiasing and those count too - what matters is
// that light surrounds dark - and a mid-tone surround (GPMC's luma ~115
// rows) fails the delta, so dark text standing on a band that stays light
// correctly stays dark itself. A matched pixel recolors through the
// brightness curve while keeping each channel's offset from the luma, so
// blue text lands as a readable light blue instead of vanishing.
constexpr int s_liftMaxLuma = 140;
constexpr int s_liftOwnColor = 50; // luma within this of the pixel is its own color
constexpr int s_liftOwnSat = 60; // ...and saturation within this much of it
constexpr int s_liftMaxThin = 3; // max own-color pixels on the thinner axis of 9
constexpr int s_liftLightMin = 25; // clearly-brighter neighbors needed in 9x9
constexpr int s_liftBgDelta = 60; // a background pixel is this much brighter...
constexpr int s_liftBgSat = 120; // ...and no more colored than this

// The dark edge strip - the taskbar rule. A Windows taskbar is a band of
// flat dark fill docked to one screen edge, carrying small bright islands:
// the Start button, pinned-app icons, the system tray clock. Those are
// light-on-dark pixels, and the per-pixel rules mangle exactly that: the
// ClearType fringe rule (built for dark text on white chrome) sees the
// saturated antialiased edge of a white clock glyph as a subpixel fringe and
// flips it to a white dot, while the light-text keep rule does not fire
// because the taskbar's own fill is saturated dark, not a flat dark surface.
// On one real capture that scribbled 1764 of the taskbar's pixels: 927 dark
// edges flipped white, 409 white glyph pixels flipped dark, 383 mid. Rather
// than tune two rules to cooperate on light-on-dark text, pass the whole
// already-dark band through untouched - the remote already renders it
// dark-mode.
//
// A band qualifies per edge: it starts at the edge and runs inward over rows
// (or columns) that are each at least s_stripDarkFrac percent pixels of luma
// at most s_stripDarkLuma, and it stops at the first bright row or after
// s_stripMaxPx. That condition is self-limiting and is what makes the rule
// safe: a light window docked to the edge makes those rows bright, so the
// run stops before it and the window is still filtered whole - the band can
// never leave a bright window's edge raw. And a row that is 85% dark holds
// only sparse bright pixels (text and icons on dark), never white window
// chrome, so passing it raw darkens nothing that needed darkening. The
// wallpaper margin the band soaks up past the taskbar was already preserved
// by the saturation rule, so it costs nothing to include it.
constexpr int s_stripDarkLuma = 100; // a pixel counts as dark background
constexpr int s_stripDarkFrac = 85; // percent of a row that must be dark
constexpr int s_stripMaxPx = 90; // never protect more than this many px
}

namespace SmartInvert
{
int medianLuma(const QImage &image)
{
    if (image.isNull()) {
        return 0;
    }

    // A 256-bin histogram on a subsample: full precision is not needed to
    // tell "mostly light" from "mostly dark", and this stays under a
    // millisecond even at 4K.
    std::array<int, 256> histogram{};
    int samples = 0;

    const int stepX = std::max(1, image.width() / 256);
    const int stepY = std::max(1, image.height() / 256);

    for (int y = 0; y < image.height(); y += stepY) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x += stepX) {
            const QRgb c = line[x];
            // Integer Rec. 601 luma.
            const int luma = (qRed(c) * 77 + qGreen(c) * 150 + qBlue(c) * 29) >> 8;
            ++histogram[luma];
            ++samples;
        }
    }

    if (samples == 0) {
        return 0;
    }

    int half = samples / 2;
    for (int v = 0; v < 256; ++v) {
        half -= histogram[v];
        if (half < 0) {
            return v;
        }
    }
    return 255;
}

int brightFraction(const QImage &image)
{
    if (image.isNull()) {
        return 0;
    }

    // Same subsample as medianLuma(). White window chrome (luma above 200)
    // is what a native dark screen or a fullscreen video does not have much
    // of, however dark its median is - text and icons keep the fraction
    // under ~10%, while a light desktop under a dark overlay sits near 30%.
    int samples = 0;
    int bright = 0;

    const int stepX = std::max(1, image.width() / 256);
    const int stepY = std::max(1, image.height() / 256);

    for (int y = 0; y < image.height(); y += stepY) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); x += stepX) {
            const QRgb c = line[x];
            const int luma = (qRed(c) * 77 + qGreen(c) * 150 + qBlue(c) * 29) >> 8;
            bright += (luma > 200) ? 1 : 0;
            ++samples;
        }
    }

    return samples == 0 ? 0 : bright * 100 / samples;
}

void apply(QImage &image)
{
    if (image.isNull() || image.format() != QImage::Format_RGB32) {
        // The RDP framebuffer is RGB32; anything else is passed through
        // untouched rather than converted behind the caller's back.
        return;
    }

    const int w = image.width();
    const int h = image.height();
    const int n = w * h;

    // Pass 1: luma and saturation maps, shared by every later test.
    QVector<uint8_t> luma(n);
    QVector<uint8_t> saturation(n);
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int r = qRed(c);
            const int g = qGreen(c);
            const int b = qBlue(c);
            const int mx = std::max(r, std::max(g, b));
            const int mn = std::min(r, std::min(g, b));
            luma[y * w + x] = uint8_t((r * 77 + g * 150 + b * 29) >> 8);
            saturation[y * w + x] = uint8_t(mx == 0 ? 0 : (mx - mn) * 255 / mx);
        }
    }

    // Pass 1b: the dark edge strip map (the taskbar). For each of the four
    // edges, walk inward from the edge while each row (or column) is mostly
    // dark; those pixels are the remote's already-dark docked chrome and are
    // painted raw. See the constants for why the mostly-dark-row condition
    // makes this safe against bright windows.
    QVector<uint8_t> protectedStrip(n, 0);
    const auto mostly_dark_row = [&](int y) {
        int dark = 0;
        const uint8_t *row = luma.constData() + y * w;
        for (int x = 0; x < w; ++x) {
            dark += (row[x] <= s_stripDarkLuma) ? 1 : 0;
        }
        return dark * 100 >= w * s_stripDarkFrac;
    };
    const auto mostly_dark_col = [&](int x) {
        int dark = 0;
        for (int y = 0; y < h; ++y) {
            dark += (luma[y * w + x] <= s_stripDarkLuma) ? 1 : 0;
        }
        return dark * 100 >= h * s_stripDarkFrac;
    };
    const int stripBottom = std::min(s_stripMaxPx, h);
    for (int y = h - 1, run = 0; run < stripBottom && mostly_dark_row(y); --y, ++run) {
        std::fill_n(protectedStrip.begin() + y * w, w, uint8_t(1));
    }
    const int stripTop = std::min(s_stripMaxPx, h);
    for (int y = 0, run = 0; run < stripTop && mostly_dark_row(y); ++y, ++run) {
        std::fill_n(protectedStrip.begin() + y * w, w, uint8_t(1));
    }
    const int stripLeft = std::min(s_stripMaxPx, w);
    for (int x = 0, run = 0; run < stripLeft && mostly_dark_col(x); ++x, ++run) {
        for (int y = 0; y < h; ++y) {
            protectedStrip[y * w + x] = 1;
        }
    }
    const int stripRight = std::min(s_stripMaxPx, w);
    for (int x = w - 1, run = 0; run < stripRight && mostly_dark_col(x); --x, ++run) {
        for (int y = 0; y < h; ++y) {
            protectedStrip[y * w + x] = 1;
        }
    }

    // Pass 2: the flat surface map, as two separable passes so the window
    // costs nine lookups per pixel per axis instead of eighty-one.
    QVector<uint8_t> winMin(n);
    QVector<uint8_t> winMax(n);
    QVector<uint16_t> winSum(n);
    for (int y = 0; y < h; ++y) {
        const uint8_t *row = luma.constData() + y * w;
        uint8_t *rowMin = winMin.data() + y * w;
        uint8_t *rowMax = winMax.data() + y * w;
        uint16_t *rowSum = winSum.data() + y * w;
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(x - s_flatRadius, 0);
            const int x1 = std::min(x + s_flatRadius, w - 1);
            uint8_t mn = 255;
            uint8_t mx = 0;
            unsigned sum = 0;
            for (int xx = x0; xx <= x1; ++xx) {
                mn = std::min(mn, row[xx]);
                mx = std::max(mx, row[xx]);
                sum += row[xx];
            }
            rowMin[x] = mn;
            rowMax[x] = mx;
            rowSum[x] = uint16_t(sum);
        }
    }

    QVector<uint8_t> surface(n, 0);
    QVector<uint8_t> darkSurface(n, 0);
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(y - s_flatRadius, 0);
        const int y1 = std::min(y + s_flatRadius, h - 1);
        for (int x = 0; x < w; ++x) {
            uint8_t mn = 255;
            uint8_t mx = 0;
            unsigned sum = 0;
            for (int yy = y0; yy <= y1; ++yy) {
                mn = std::min(mn, winMin[yy * w + x]);
                mx = std::max(mx, winMax[yy * w + x]);
                sum += winSum[yy * w + x];
            }
            const int count = (y1 - y0 + 1) * (std::min(x + s_flatRadius, w - 1) - std::max(x - s_flatRadius, 0) + 1);
            if (mx - mn <= s_flatSpread && int(sum / count) >= s_flatMean
                && saturation[y * w + x] <= s_flatOwnSat) {
                surface[y * w + x] = 1;
            }
            // The dark mirror, mean-based (see the constants): full windows
            // only, so a one-pixel-tall strip at an image edge - or the
            // brightness ramp of a test - is never a surface.
            if (count == (2 * s_flatRadius + 1) * (2 * s_flatRadius + 1)
                && luma[y * w + x] <= s_darkOwnMax && int(sum / count) <= s_darkMean
                && saturation[y * w + x] <= s_flatOwnSat) {
                darkSurface[y * w + x] = 1;
            }
        }
    }

    // Pass 3: classify pixels the plain saturation rule would get wrong:
    // ClearType fringe and colored text to lift (both saturated), and light
    // text to keep over dark surfaces (weakly saturated). All three rules
    // demand thinness, so it is computed first.
    QVector<uint8_t> fringe(n, 0);
    QVector<uint8_t> lift(n, 0);
    QVector<uint8_t> keep(n, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int self = y * w + x;
            const int selfLuma = luma[self];
            const int selfSat = saturation[self];

            const bool saturated = selfSat > s_satKeep;
            if (!saturated && (selfLuma < s_keepMinLuma || selfSat > s_keepMaxSat)) {
                continue; // plain chrome, or content too dim for any rule
            }

            // Thinness: how far the pixel's own tone reaches along each
            // axis. Text strokes and ClearType fringes are thin on one
            // axis; icon bodies and icon detail are thick on both. "Own
            // tone" must match saturation as well as luma: a bright cool
            // fringe (luma ~195) on MMC's light-grey chrome (luma ~205) is
            // a one-pixel band, but a luma-only test reads the grey as its
            // own color and calls it thick.
            int horiz = 0;
            for (int xx = std::max(x - 4, 0); xx <= std::min(x + 4, w - 1); ++xx) {
                const int j = y * w + xx;
                horiz += (std::abs(int(luma[j]) - selfLuma) <= s_liftOwnColor
                          && std::abs(int(saturation[j]) - selfSat) <= s_liftOwnSat)
                    ? 1
                    : 0;
            }
            int vert = 0;
            for (int yy = std::max(y - 4, 0); yy <= std::min(y + 4, h - 1); ++yy) {
                const int j = yy * w + x;
                vert += (std::abs(int(luma[j]) - selfLuma) <= s_liftOwnColor
                         && std::abs(int(saturation[j]) - selfSat) <= s_liftOwnSat)
                    ? 1
                    : 0;
            }
            const bool thin = std::min(horiz, vert) <= s_liftMaxThin;

            if (!saturated) {
                // Light text protection: a thin bright stroke standing in a
                // big dark region - desktop labels, taskbar clock text -
                // stays bright. See the constants for the two guards that
                // keep white-on-chrome pixels out.
                if (std::min(horiz, vert) > s_keepMaxThin) {
                    continue;
                }
                int dark = 0;
                int brighter = 0;
                for (int yy = std::max(y - 4, 0); yy <= std::min(y + 4, h - 1); ++yy) {
                    for (int xx = std::max(x - 4, 0); xx <= std::min(x + 4, w - 1); ++xx) {
                        const int l = luma[yy * w + xx];
                        dark += (l <= selfLuma - s_keepDarkDelta) ? 1 : 0;
                        brighter += (l >= selfLuma + s_keepBrightDelta) ? 1 : 0;
                    }
                }
                if (dark <= brighter) {
                    continue;
                }
                int broad = 0;
                int neighboursLight = 0;
                for (int yy = std::max(y - s_keepBroadRadius, 0); yy <= std::min(y + s_keepBroadRadius, h - 1); ++yy) {
                    for (int xx = std::max(x - s_keepBroadRadius, 0); xx <= std::min(x + s_keepBroadRadius, w - 1); ++xx) {
                        const int j = yy * w + xx;
                        // Dark support = pixels that stay dark after
                        // filtering: colored dark ones (taskbar, wallpaper -
                        // the saturation rule preserves them) and flat dark
                        // surfaces (Start menu panel - that rule preserves
                        // them). Neutral dark pixels the curve flips light
                        // do not count: that is what keeps the dithered
                        // Start menu border from glowing. See the constants.
                        broad += (luma[j] <= selfLuma - s_keepDarkDelta
                                  && (saturation[j] > s_satKeep || darkSurface[j]))
                            ? 1
                            : 0;
                        neighboursLight += (luma[j] >= s_keepMinLuma) ? 1 : 0;
                    }
                }
                // And the region must be mostly dark, not a checkerboard:
                // white text on a white page or a dithered translucent band
                // both have dark support nearby, but half their 15x15
                // window is light pixels; real text has only its own
                // strokes (measured: menu text median 48 of 225, dither
                // band median 195).
                keep[self] = (broad >= s_keepBroadMin && neighboursLight <= s_keepBrightMax) ? 1 : 0;
                continue;
            }

            bool core = false;
            bool white = false;
            for (int yy = std::max(y - s_fringeRadius, 0); yy <= std::min(y + s_fringeRadius, h - 1); ++yy) {
                for (int xx = std::max(x - s_fringeRadius, 0); xx <= std::min(x + s_fringeRadius, w - 1); ++xx) {
                    const int l = luma[yy * w + xx];
                    core = core || (l <= s_fringeCore && saturation[yy * w + xx] <= s_fringeCoreSat);
                    white = white || (l >= s_fringeWhite && saturation[yy * w + xx] <= s_fringeWhiteSat);
                }
            }
            if (core && white && thin) {
                fringe[self] = 1;
                continue;
            }

            if (selfLuma > s_liftMaxLuma || !thin) {
                continue;
            }

            int light = 0;
            for (int yy = std::max(y - 4, 0); yy <= std::min(y + 4, h - 1); ++yy) {
                for (int xx = std::max(x - 4, 0); xx <= std::min(x + 4, w - 1); ++xx) {
                    const int j = yy * w + xx;
                    light += (int(luma[j]) >= selfLuma + s_liftBgDelta && saturation[j] <= s_liftBgSat) ? 1 : 0;
                }
            }
            lift[self] = (light >= s_liftLightMin) ? 1 : 0;
        }
    }

    // Pass 4: recolor.
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int self = y * w + x;
            const QRgb c = line[x];
            const int r = qRed(c);
            const int g = qGreen(c);
            const int b = qBlue(c);

            if (protectedStrip[self]) {
                // Already-dark docked chrome (taskbar, its icons, its
                // clock): passed through raw. See the constants.
                continue;
            }

            if (fringe[self]) {
                // Subpixel antialiasing: recolor from luma as plain grey.
                const int recolored = s_curve[luma[self]];
                line[x] = qRgb(recolored, recolored, recolored);
                continue;
            }

            if (surface[self]) {
                // Flat light chrome, tinted or not: fully recolored, and
                // clamped dark, so a tinted row goes as dark as the white
                // around it rather than resting at "mid grey".
                const int value = (std::max(r, std::max(g, b)) + std::min(r, std::min(g, b))) / 2;
                const int recolored = std::min(s_curve[value], s_surfMax);
                line[x] = qRgb(recolored, recolored, recolored);
                continue;
            }

            if (darkSurface[self]) {
                // Flat dark surface - the remote's own dark chrome (a Start
                // menu panel at (31, 31, 31)): already dark-mode, and the
                // curve would flip it into a glowing slab. Untouched; the
                // light text standing on it is preserved by the keep rule.
                continue;
            }

            if (keep[self]) {
                // White text over an already-dark surface (wallpaper,
                // taskbar): recoloring it dark would hide it. Untouched.
                continue;
            }

            if (lift[self]) {
                // Colored text on a surface that went dark: keep the hue,
                // lift the brightness, the way dark themes restyle links.
                const int l = luma[self];
                const int target = s_curve[l];
                line[x] = qRgb(std::clamp(target + (r - l), 0, 255),
                               std::clamp(target + (g - l), 0, 255),
                               std::clamp(target + (b - l), 0, 255));
                continue;
            }

            const int mx = std::max(r, std::max(g, b));
            const int mn = std::min(r, std::min(g, b));
            const int value = (mx + mn) / 2;
            const int sat = saturation[self];

            // How much of the original pixel survives: 0 for grey chrome
            // (fully recolored), 255 for saturated content (untouched).
            const int keep = std::clamp((sat - s_satKeep) * 255 / s_satKeepRange, 0, 255);
            if (keep == 255) {
                continue;
            }

            const int recolored = s_curve[value];

            // Grey chrome stays grey; partially protected pixels keep a
            // fraction of their channels, which desaturates them towards the
            // recolored brightness instead of shifting their hue.
            if (keep == 0) {
                line[x] = qRgb(recolored, recolored, recolored);
            } else {
                const int blendR = (recolored * (255 - keep) + r * keep) / 255;
                const int blendG = (recolored * (255 - keep) + g * keep) / 255;
                const int blendB = (recolored * (255 - keep) + b * keep) / 255;
                line[x] = qRgb(blendR, blendG, blendB);
            }
        }
    }
}
}
