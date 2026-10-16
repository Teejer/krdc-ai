// SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

// Test for the SmartInvert dark mode filter: the expectations are measured
// from real Server 2012 captures (Explorer, GPMC/MMC consoles, the Start
// menu and taskbar), so each check describes a behavior those screenshots
// proved out.

#include <QImage>
#include <QtTest>

#include "smartinvert.h"

class SmartInvertTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /**
     * A white window with text and an icon reads as a light theme, and the
     * filter darkens the chrome, lifts the text, keeps saturated content,
     * neutralizes ClearType fringes, lifts colored text off tinted bands,
     * preserves light text on dark surfaces, and passes the taskbar through.
     */
    void testSmartInvert();
};

void SmartInvertTest::testSmartInvert()
{
    QImage img(64, 64, QImage::Format_RGB32);
    img.fill(Qt::white);
    // A realistic thin-stroked glyph, not a solid block: a filled 16x6 dark
    // rectangle is structurally a small dark panel, and the flat dark
    // surface rule (rightly) leaves panels alone. Real glyph strokes on a
    // light page window-average bright and recolor.
    for (int y = 4; y < 16; ++y) { // an "H" of 2px stems and a crossbar
        for (int x = 4; x < 6; ++x) {
            img.setPixel(x, y, qRgb(20, 20, 20));
        }
        for (int x = 10; x < 12; ++x) {
            img.setPixel(x, y, qRgb(20, 20, 20));
        }
        if (y >= 8 && y < 10) {
            for (int x = 6; x < 10; ++x) {
                img.setPixel(x, y, qRgb(20, 20, 20));
            }
        }
    }
    for (int x = 30; x < 46; ++x) {
        for (int y = 30; y < 46; ++y) {
            img.setPixel(x, y, qRgb(225, 30, 40));
        }
    }

    QVERIFY2(SmartInvert::medianLuma(img) > 150, "a white window with text and an icon reads as a light theme");

    SmartInvert::apply(img);
    const QRgb background = img.pixel(60, 60);
    QVERIFY2(qRed(background) < 70 && qGreen(background) < 70 && qBlue(background) < 70,
          "the white background becomes dark");
    const QRgb text = img.pixel(10, 6);
    QVERIFY2(qRed(text) > 170 && qGreen(text) > 170 && qBlue(text) > 170, "the dark text becomes light");
    QVERIFY2(qRed(text) > 235, "the text lands near white, not cloudy grey");
    const QRgb icon = img.pixel(38, 38);
    QVERIFY2(qRed(icon) > 180 && qGreen(icon) < 90 && qBlue(icon) < 90, "the saturated red icon keeps its color");

    // Antialiased edges are grey ramps; a smooth monotone curve keeps them
    // smooth (no banding or halos around text).
    QImage ramp(256, 16, QImage::Format_RGB32);
    ramp.fill(Qt::white);
    for (int v = 0; v < 256; ++v) {
        ramp.setPixel(v, 0, qRgb(v, v, v));
    }
    SmartInvert::apply(ramp);
    int previous = qRed(ramp.pixel(0, 0));
    bool smooth = true;
    for (int v = 1; v < 256; ++v) {
        const int out = qRed(ramp.pixel(v, 0));
        if (out > previous + 1) {
            smooth = false;
        }
        previous = out;
    }
    QVERIFY2(smooth, "the grey curve is monotone and step-free");
    QVERIFY2(qRed(ramp.pixel(255, 0)) < qRed(ramp.pixel(0, 0)), "light greys end up darker than dark greys");

    // Saturated dark content passes through: a real Server 2012 taskbar is
    // (0, 3, 8) - near-black but fully saturated - and the same for deep
    // blue wallpaper. The saturation rule alone keeps them dark.
    QImage taskbar(32, 32, QImage::Format_RGB32);
    taskbar.fill(qRgb(0, 3, 8));
    taskbar.setPixel(16, 16, qRgb(220, 170, 40)); // colorful icon
    const QRgb barBefore = taskbar.pixel(4, 4);
    const QRgb iconBefore = taskbar.pixel(16, 16);
    SmartInvert::apply(taskbar);
    QVERIFY2(taskbar.pixel(4, 4) == barBefore, "a saturated near-black taskbar is untouched");
    QVERIFY2(taskbar.pixel(16, 16) == iconBefore, "a colorful taskbar icon is untouched");

    // Picture content survives: a colorful gradient wallpaper is all
    // saturated pixels and must come out unchanged.
    QImage wallpaper(96, 96, QImage::Format_RGB32);
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 96; ++x) {
            wallpaper.setPixel(x, y, qRgb((x * 7 + y * 3) % 256, 60 + (y * 5) % 190, 255 - (x * 3) % 200));
        }
    }
    const QRgb wpBefore = wallpaper.pixel(10, 10);
    SmartInvert::apply(wallpaper);
    QVERIFY2(wallpaper.pixel(10, 10) == wpBefore, "colorful wallpaper pixels are untouched");

    // The ClearType halo regression, measured from a real Server 2012 capture:
    // a black glyph on white arrives with colored subpixel edges - warm
    // (239,205,75) and cool (145,212,247) pixels. They sit between the dark
    // core and flat white, so they flatten to grey and recolor with the
    // glyph instead of surviving as colored sparks around the text.
    // Link-blue text has no dark core within reach and keeps its color.
    QImage glyph(96, 96, QImage::Format_RGB32);
    glyph.fill(Qt::white);
    for (int y = 40; y < 52; ++y) {
        glyph.setPixel(40, y, qRgb(8, 8, 8)); // stem core
        glyph.setPixel(41, y, qRgb(145, 212, 247)); // cool subpixel fringe
        glyph.setPixel(39, y, qRgb(239, 205, 75)); // warm subpixel fringe
    }
    glyph.setPixel(70, 70, qRgb(0, 102, 204)); // link-blue text pixel on white
    // The same ClearType sandwich on MMC's light-grey dialog chrome
    // (luma ~206): the fringe must neutralize there too, not just on white.
    for (int y = 70; y < 84; ++y) {
        for (int x = 10; x < 30; ++x) {
            glyph.setPixel(x, y, qRgb(212, 208, 200));
        }
    }
    glyph.setPixel(15, 76, qRgb(8, 8, 8));
    glyph.setPixel(16, 76, qRgb(145, 212, 247));

    SmartInvert::apply(glyph);
    const QRgb cool = glyph.pixel(41, 46);
    QVERIFY2(qBlue(cool) - qRed(cool) < 30, "the cool ClearType fringe is neutralized to grey");
    const QRgb warm = glyph.pixel(39, 46);
    QVERIFY2(qRed(warm) - qBlue(warm) < 30, "the warm ClearType fringe is neutralized to grey");
    QVERIFY2(qRed(glyph.pixel(40, 46)) > 235, "the glyph core they hug turns near-white");
    const QRgb link = glyph.pixel(70, 70);
    QVERIFY2(qBlue(link) - qRed(link) > 100 && qBlue(link) > 200,
          "link-blue text lifts to a readable light blue instead of vanishing");
    const QRgb greyFringe = glyph.pixel(16, 76);
    QVERIFY2(qBlue(greyFringe) - qRed(greyFringe) < 30, "fringes on light-grey MMC chrome neutralize as well");

    // The colored text lift, measured from a real GPMC capture: blue section
    // headers ((49,49,209)) sit on cream bands ((255,246,215)); the filter
    // darkens the band, so the protected dark-blue glyph must rise with its
    // hue intact or it disappears. A big solid icon block sits in the same
    // chrome but is surrounded by its own colors, not by chrome, and must
    // keep its color.
    QImage gpmc(96, 96, QImage::Format_RGB32);
    gpmc.fill(qRgb(255, 246, 215)); // cream band
    for (int y = 20; y < 32; ++y) {
        for (int x = 10; x < 12; ++x) {
            gpmc.setPixel(x, y, qRgb(49, 49, 209)); // blue glyph stroke, 2px like real text
        }
    }
    for (int y = 56; y < 88; ++y) {
        for (int x = 56; x < 88; ++x) {
            gpmc.setPixel(x, y, qRgb(49, 49, 209)); // solid icon body
        }
    }
    SmartInvert::apply(gpmc);
    const QRgb lifted = gpmc.pixel(11, 26);
    QVERIFY2(qBlue(lifted) > 200 && qRed(lifted) > 140, "blue text on a cream band lifts to readable light blue");
    QVERIFY2(qBlue(lifted) - qRed(lifted) > 30, "and it stays blue, not grey");
    const QRgb solid = gpmc.pixel(72, 72);
    QVERIFY2(solid == qRgb(49, 49, 209), "a solid blue icon body in the same chrome keeps its color");

    // GPMC's other band flavor: a mildly tinted blue-grey surface
    // ((159, 183, 207), saturation 59) that the plain saturation rule leaves
    // light while the window around it goes dark. The flat surface rule
    // darkens it, and the near-black blue text standing on it lifts to match.
    QImage band(64, 64, QImage::Format_RGB32);
    band.fill(qRgb(159, 183, 207));
    for (int y = 20; y < 32; ++y) {
        for (int x = 20; x < 22; ++x) {
            band.setPixel(x, y, qRgb(0, 0, 46)); // "Links" text, as captured
        }
    }
    SmartInvert::apply(band);
    const QRgb tint = band.pixel(40, 40);
    QVERIFY2(qRed(tint) <= 60 && qGreen(tint) == qRed(tint), "the tinted band itself goes dark like any chrome");
    const QRgb onTint = band.pixel(20, 26);
    QVERIFY2(qBlue(onTint) > 200 && qRed(onTint) > 140, "dark blue text on the tinted band lifts to readable light blue");

    // Light text protection, measured from the same capture: Windows paints
    // desktop icon labels ((255,255,255)) and the taskbar clock over a
    // wallpaper ((0,27,58)) that is dark before the filter runs. Darkening
    // the white glyphs hid them; they must stay bright. The white page
    // mirror case must NOT fire: a light-grey antialiasing pixel between a
    // black glyph and a white page recolors dark with its page, or the
    // darkened page gets white speckle around every letter.
    QImage desktop(64, 64, QImage::Format_RGB32);
    desktop.fill(qRgb(0, 27, 58)); // this remote's wallpaper
    for (int y = 20; y < 32; ++y) {
        for (int x = 10; x < 12; ++x) {
            desktop.setPixel(x, y, qRgb(255, 255, 255)); // white label stroke
        }
    }
    SmartInvert::apply(desktop);
    QVERIFY2(qRed(desktop.pixel(10, 26)) > 200, "white label text over dark wallpaper stays bright");
    QVERIFY2(desktop.pixel(40, 40) == qRgb(0, 27, 58), "and the wallpaper itself is untouched");

    // The Start menu border trap, from a real capture: Windows paints the
    // menu's translucent top border over the white window behind it, and
    // over RDP it arrives as a dither - alternating light and dark neutral
    // dots. Every white dot looks thin with dark neighbors, so the light
    // text rule preserved 531 pixels of it as a glowing white band - until
    // the broad dark surround was required to be colored dark (wallpaper,
    // taskbar), not neutral dark (text, dither).
    QImage glass(64, 64, QImage::Format_RGB32);
    glass.fill(Qt::white);
    for (int y = 22; y < 42; ++y) {
        for (int x = 0; x < 64; ++x) {
            if ((y % 4) == 0 || (x + y) % 2 == 0) {
                glass.setPixel(x, y, qRgb(35, 35, 35));
            }
        }
    }
    SmartInvert::apply(glass);
    QVERIFY2(qRed(glass.pixel(32, 31)) < 120, "a dithered translucent band over chrome darkens instead of glowing");
    QImage page(64, 64, QImage::Format_RGB32);
    page.fill(Qt::white);
    for (int y = 20; y < 32; ++y) {
        for (int x = 10; x < 12; ++x) {
            page.setPixel(x, y, qRgb(8, 8, 8)); // black glyph stroke
        }
    }
    page.setPixel(12, 26, qRgb(200, 200, 200)); // its antialiasing edge
    SmartInvert::apply(page);
    QVERIFY2(qRed(page.pixel(12, 26)) < 120, "white chrome beside dark text still goes dark, no speckle");

    // brightFraction(), the disengage guard: a dark Start menu ((11,26,53))
    // opened over a white desktop reads dark by median while a third of the
    // screen is still white chrome - disengaging there unfiltered a wall of
    // white windows. A native dark screen (or a video) keeps its bright
    // pixels sparse: text and icons only.
    QImage menuOverDesktop(64, 64, QImage::Format_RGB32);
    menuOverDesktop.fill(qRgb(11, 26, 53));
    for (int y = 0; y < 26; ++y) {
        for (int x = 0; x < 64; ++x) {
            menuOverDesktop.setPixel(x, y, qRgb(255, 255, 255)); // white windows
        }
    }
    QVERIFY2(SmartInvert::medianLuma(menuOverDesktop) < 110, "a Start menu over a white desktop reads dark by median");
    QVERIFY2(SmartInvert::brightFraction(menuOverDesktop) >= 15, "but its white chrome fraction keeps the filter engaged");
    QImage nativeDark(64, 64, QImage::Format_RGB32);
    nativeDark.fill(qRgb(24, 24, 24));
    for (int y = 0; y < 64; ++y) {
        nativeDark.setPixel(y, (y * 7) % 64, qRgb(255, 255, 255)); // sparse text dots
    }
    QVERIFY2(SmartInvert::medianLuma(nativeDark) < 110, "a native dark screen reads dark by median");
    QVERIFY2(SmartInvert::brightFraction(nativeDark) < 15, "and its sparse whites let it pass through unfiltered");
    QVERIFY2(SmartInvert::brightFraction(QImage()) == 0, "a null image has no bright pixels");

    // The Start menu panel itself: the app list is painted over a flat
    // (31, 31, 31) panel that the curve would flip to near-white, turning
    // the menu into a glaring slab the moment the filter learned to stay
    // engaged behind it. The flat dark surface rule leaves the panel alone,
    // and the white text standing on it stays bright - while the dithered
    // translucent border above (previous test) still darkens, because a
    // dither window averages its bright dots in above any dark surface
    // bound. The panel floats in the middle of a light screen so this tests
    // the dark surface rule directly, not the edge strip.
    QImage menuPanel(96, 96, QImage::Format_RGB32);
    menuPanel.fill(Qt::white);
    for (int y = 30; y < 70; ++y) {
        for (int x = 30; x < 70; ++x) {
            menuPanel.setPixel(x, y, qRgb(31, 31, 31)); // the dark panel
        }
    }
    for (int y = 40; y < 52; ++y) {
        for (int x = 40; x < 42; ++x) {
            menuPanel.setPixel(x, y, qRgb(255, 255, 255)); // white glyph stroke
        }
    }
    SmartInvert::apply(menuPanel);
    QVERIFY2(menuPanel.pixel(60, 60) == qRgb(31, 31, 31), "a flat dark Start menu panel is left alone, not flipped to a white slab");
    QVERIFY2(qRed(menuPanel.pixel(41, 45)) >= 200, "white text standing on the dark panel stays bright");
    QVERIFY2(qRed(menuPanel.pixel(80, 80)) < 70, "the light screen around the panel is still darkened");

    // The taskbar strip: a Windows taskbar is a band of flat dark fill on a
    // screen edge with small bright islands (its clock glyphs, Start button,
    // pinned icons). Those light-on-dark pixels are exactly what the per
    // pixel rules mangle, so a mostly-dark band docked to an edge is passed
    // through raw. Its dark fill and bright clock text both survive intact,
    // while the light screen above the taskbar is still filtered - proving
    // the band is bounded to the dark rows and cannot leave a window raw.
    QImage taskbarStrip(96, 96, QImage::Format_RGB32);
    taskbarStrip.fill(Qt::white);
    for (int y = 76; y < 96; ++y) { // the (0, 3, 8) taskbar fill on the bottom edge
        for (int x = 0; x < 96; ++x) {
            taskbarStrip.setPixel(x, y, qRgb(0, 3, 8));
        }
    }
    for (int y = 80; y < 90; ++y) { // a white clock glyph standing on it
        for (int x = 40; x < 42; ++x) {
            taskbarStrip.setPixel(x, y, qRgb(255, 255, 255));
        }
    }
    taskbarStrip.setPixel(42, 85, qRgb(10, 8, 6)); // its antialiased saturated edge
    SmartInvert::apply(taskbarStrip);
    QVERIFY2(taskbarStrip.pixel(10, 90) == qRgb(0, 3, 8), "the dark taskbar fill is passed through, not recolored");
    QVERIFY2(qRed(taskbarStrip.pixel(41, 85)) >= 200, "the taskbar clock text stays bright, not scribbled dark");
    QVERIFY2(qRed(taskbarStrip.pixel(42, 85)) < 60, "the clock glyph antialiasing is not flipped to a white dot");
    QVERIFY2(qRed(taskbarStrip.pixel(48, 20)) < 70, "the light desktop above the taskbar is still darkened");
}

QTEST_GUILESS_MAIN(SmartInvertTest)

#include "smartinvert_test.moc"
