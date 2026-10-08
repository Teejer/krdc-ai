/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <Qt>

/**
 * Translates the key names the assistant is allowed to use into Qt key codes.
 * Deliberately explicit, because QKeySequence's own parser depends on the
 * translation catalogue.
 */
namespace AiKeys
{
/**
 * @param name a key name such as "return", "f5", "delete" or "a"
 * @return the matching Qt::Key value, or 0 when the name is unknown
 */
int qtKey(const QString &name);

/**
 * @param names modifier names as used in tool arguments
 * @return the matching Qt modifier mask
 */
Qt::KeyboardModifiers modifiers(const QStringList &names, QStringList &unknown);
}
