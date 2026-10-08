/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QChar>
#include <QVector>
#include <Qt>

#include <winpr/input.h>

/**
 * Maps Qt key codes to Windows virtual key codes so the AI assistant can
 * inject keyboard input without a real QKeyEvent (and therefore without a
 * native scan code, which synthesized events do not have).
 *
 * The mapping follows the US layout, which is what the RDP scancode tables in
 * WinPR assume. Keys that cannot be expressed as a virtual key code return 0
 * and must be reported as unsupported by the caller.
 */
namespace RdpAiInput {
/**
 * @return the virtual key code for a Qt::Key value, or 0 if it is unknown.
 */
DWORD virtualKeyForQtKey(int key);

/**
 * @return the virtual key code that produces @p ch on a US layout.
 * @p modifiers is set to the modifier keys that have to be held, if any.
 * Returns 0 for characters that cannot be produced this way.
 */
DWORD virtualKeyForChar(QChar ch, Qt::KeyboardModifiers &modifiers);

/**
 * @return @p modifiers as virtual key codes, in the order they should be
 * pressed. Releasing happens in reverse order.
 */
QVector<DWORD> modifierVirtualKeys(Qt::KeyboardModifiers modifiers);
}
