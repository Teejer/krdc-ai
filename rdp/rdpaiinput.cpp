/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "rdpaiinput.h"

#include <QHash>
#include <Qt>

namespace
{
// Qt::Key values for the function keys are contiguous, as are the matching
// Windows virtual key codes, so F1..F24 are handled with arithmetic.
constexpr int FirstFunctionKey = Qt::Key_F1;
constexpr int LastFunctionKey = Qt::Key_F24;
constexpr int FirstFunctionVk = VK_F1;

// Numpad digits are separate keys on Windows, so Qt's Key_0..Key_9 (which are
// the same values as the ASCII digits) map to the main row.
QHash<int, DWORD> qtKeyTable()
{
    static const QHash<int, DWORD> table = {
        {Qt::Key_Escape, VK_ESCAPE},
        {Qt::Key_Tab, VK_TAB},
        {Qt::Key_Backtab, VK_TAB},
        {Qt::Key_Backspace, VK_BACK},
        {Qt::Key_Return, VK_RETURN},
        {Qt::Key_Enter, VK_RETURN}, // numpad enter
        {Qt::Key_Insert, VK_INSERT},
        {Qt::Key_Delete, VK_DELETE},
        {Qt::Key_Pause, VK_PAUSE},
        {Qt::Key_Print, VK_SNAPSHOT},
        {Qt::Key_SysReq, VK_SNAPSHOT},
        {Qt::Key_Clear, VK_CLEAR},
        {Qt::Key_Home, VK_HOME},
        {Qt::Key_End, VK_END},
        {Qt::Key_Left, VK_LEFT},
        {Qt::Key_Up, VK_UP},
        {Qt::Key_Right, VK_RIGHT},
        {Qt::Key_Down, VK_DOWN},
        {Qt::Key_PageUp, VK_PRIOR},
        {Qt::Key_PageDown, VK_NEXT},
        {Qt::Key_Shift, VK_LSHIFT},
        {Qt::Key_Control, VK_LCONTROL},
        {Qt::Key_Meta, VK_LWIN},
        {Qt::Key_Alt, VK_LMENU},
        {Qt::Key_AltGr, VK_RMENU},
        {Qt::Key_CapsLock, VK_CAPITAL},
        {Qt::Key_NumLock, VK_NUMLOCK},
        {Qt::Key_ScrollLock, VK_SCROLL},
        {Qt::Key_Super_L, VK_LWIN},
        {Qt::Key_Super_R, VK_RWIN},
        {Qt::Key_Menu, VK_APPS},
        {Qt::Key_Help, VK_HELP},
        {Qt::Key_Back, VK_BROWSER_BACK},
        {Qt::Key_Forward, VK_BROWSER_FORWARD},
        {Qt::Key_Refresh, VK_BROWSER_REFRESH},
        {Qt::Key_Stop, VK_BROWSER_STOP},
        {Qt::Key_HomePage, VK_BROWSER_HOME},
        {Qt::Key_VolumeDown, VK_VOLUME_DOWN},
        {Qt::Key_VolumeMute, VK_VOLUME_MUTE},
        {Qt::Key_VolumeUp, VK_VOLUME_UP},
        {Qt::Key_MediaPlay, VK_MEDIA_PLAY_PAUSE},
        {Qt::Key_MediaStop, VK_MEDIA_STOP},
        {Qt::Key_MediaNext, VK_MEDIA_NEXT_TRACK},
        {Qt::Key_MediaPrevious, VK_MEDIA_PREV_TRACK},
        {Qt::Key_Space, VK_SPACE},
        // Unshifted US layout punctuation
        {Qt::Key_Minus, VK_OEM_MINUS},
        {Qt::Key_Equal, VK_OEM_PLUS},
        {Qt::Key_BracketLeft, VK_OEM_4},
        {Qt::Key_BracketRight, VK_OEM_6},
        {Qt::Key_Backslash, VK_OEM_5},
        {Qt::Key_Semicolon, VK_OEM_1},
        {Qt::Key_Apostrophe, VK_OEM_7},
        {Qt::Key_Comma, VK_OEM_COMMA},
        {Qt::Key_Period, VK_OEM_PERIOD},
        {Qt::Key_Slash, VK_OEM_2},
        {Qt::Key_QuoteLeft, VK_OEM_3},
    };
    return table;
}

// US layout characters that share a key with another character, mapped to the
// key that has to be pressed together with Shift.
QHash<ushort, DWORD> shiftedCharTable()
{
    static const QHash<ushort, DWORD> table = {
        {u')', VK_KEY_0},      {u'!', VK_KEY_1},     {u'@', VK_KEY_2},      {u'#', VK_KEY_3},
        {u'$', VK_KEY_4},      {u'%', VK_KEY_5},     {u'^', VK_KEY_6},      {u'&', VK_KEY_7},
        {u'*', VK_KEY_8},      {u'(', VK_KEY_9},     {u'_', VK_OEM_MINUS},  {u'+', VK_OEM_PLUS},
        {u'{', VK_OEM_4},      {u'}', VK_OEM_6},     {u'|', VK_OEM_5},      {u':', VK_OEM_1},
        {u'"', VK_OEM_7},      {u'<', VK_OEM_COMMA}, {u'>', VK_OEM_PERIOD}, {u'?', VK_OEM_2},
        {u'~', VK_OEM_3},
    };
    return table;
}
}

DWORD RdpAiInput::virtualKeyForQtKey(int key)
{
    if (key >= FirstFunctionKey && key <= LastFunctionKey) {
        return FirstFunctionVk + (key - FirstFunctionKey);
    }

    // Qt::Key_A..Key_Z and Key_0..Key_9 share their values with ASCII, and so
    // do the corresponding virtual key codes.
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return VK_KEY_A + (key - Qt::Key_A);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return VK_KEY_0 + (key - Qt::Key_0);
    }

    return qtKeyTable().value(key, 0);
}

DWORD RdpAiInput::virtualKeyForChar(QChar ch, Qt::KeyboardModifiers &modifiers)
{
    modifiers = Qt::NoModifier;

    const ushort code = ch.unicode();

    if (ch == u' ') {
        return VK_SPACE;
    }
    if (ch == u'\t') {
        return VK_TAB;
    }
    if (ch == u'\n' || ch == u'\r') {
        return VK_RETURN;
    }
    if (ch == u'\b') {
        return VK_BACK;
    }
    if (ch.unicode() == 0x1b) {
        return VK_ESCAPE;
    }

    if (ch.isLetter() && code < 128) {
        const QChar upper = ch.toUpper();
        if (upper.unicode() >= u'A' && upper.unicode() <= u'Z') {
            if (ch.isUpper()) {
                modifiers = Qt::ShiftModifier;
            }
            return VK_KEY_A + (upper.unicode() - u'A');
        }
    }

    if (code >= u'0' && code <= u'9') {
        return VK_KEY_0 + (code - u'0');
    }

    // Unshifted printable ASCII
    if (code >= 0x21 && code <= 0x7e) {
        static const QHash<ushort, DWORD> printable = {
            {u'-', VK_OEM_MINUS},  {u'=', VK_OEM_PLUS},   {u'[', VK_OEM_4},      {u']', VK_OEM_6},
            {u'\\', VK_OEM_5},     {u';', VK_OEM_1},      {u'\'', VK_OEM_7},     {u',', VK_OEM_COMMA},
            {u'.', VK_OEM_PERIOD}, {u'/', VK_OEM_2},      {u'`', VK_OEM_3},
        };
        static const QHash<ushort, DWORD> shifted = shiftedCharTable();

        if (printable.contains(code)) {
            return printable.value(code);
        }
        if (shifted.contains(code)) {
            modifiers = Qt::ShiftModifier;
            return shifted.value(code);
        }
    }

    return 0;
}

QVector<DWORD> RdpAiInput::modifierVirtualKeys(Qt::KeyboardModifiers modifiers)
{
    // Left-side specific virtual keys, not the generic VK_SHIFT/VK_CONTROL/
    // VK_MENU aliases: winpr's virtual-key-to-scancode tables (and the RDP
    // protocol itself) only know the specific keys, so the generic codes
    // convert to scancode 0 and every injected combination loses its
    // modifiers. Injecting the specific key is also what a physical
    // left-hand keyboard press produces, which the manual input path shows.
    QVector<DWORD> keys;

    if (modifiers & Qt::ShiftModifier) {
        keys.append(VK_LSHIFT);
    }
    if (modifiers & Qt::ControlModifier) {
        keys.append(VK_LCONTROL);
    }
    if (modifiers & Qt::AltModifier) {
        keys.append(VK_LMENU);
    }
    if (modifiers & Qt::MetaModifier) {
        keys.append(VK_LWIN);
    }

    return keys;
}
