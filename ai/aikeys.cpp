/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aikeys.h"

#include <QHash>
#include <QChar>

int AiKeys::qtKey(const QString &name)
{
    static const QHash<QString, int> table = {
        {QStringLiteral("ctrl"), Qt::Key_Control},
        {QStringLiteral("control"), Qt::Key_Control},
        {QStringLiteral("shift"), Qt::Key_Shift},
        {QStringLiteral("alt"), Qt::Key_Alt},
        {QStringLiteral("option"), Qt::Key_Alt},
        {QStringLiteral("opt"), Qt::Key_Alt},
        {QStringLiteral("return"), Qt::Key_Return},
        {QStringLiteral("enter"), Qt::Key_Return},
        {QStringLiteral("numpadenter"), Qt::Key_Enter},
        {QStringLiteral("tab"), Qt::Key_Tab},
        {QStringLiteral("backtab"), Qt::Key_Backtab},
        {QStringLiteral("escape"), Qt::Key_Escape},
        {QStringLiteral("esc"), Qt::Key_Escape},
        {QStringLiteral("backspace"), Qt::Key_Backspace},
        {QStringLiteral("delete"), Qt::Key_Delete},
        {QStringLiteral("del"), Qt::Key_Delete},
        {QStringLiteral("insert"), Qt::Key_Insert},
        {QStringLiteral("ins"), Qt::Key_Insert},
        {QStringLiteral("space"), Qt::Key_Space},
        {QStringLiteral("up"), Qt::Key_Up},
        {QStringLiteral("down"), Qt::Key_Down},
        {QStringLiteral("left"), Qt::Key_Left},
        {QStringLiteral("right"), Qt::Key_Right},
        {QStringLiteral("home"), Qt::Key_Home},
        {QStringLiteral("end"), Qt::Key_End},
        {QStringLiteral("pageup"), Qt::Key_PageUp},
        {QStringLiteral("pgup"), Qt::Key_PageUp},
        {QStringLiteral("pagedown"), Qt::Key_PageDown},
        {QStringLiteral("pgdn"), Qt::Key_PageDown},
        {QStringLiteral("printscreen"), Qt::Key_Print},
        {QStringLiteral("print"), Qt::Key_Print},
        {QStringLiteral("prtsc"), Qt::Key_Print},
        {QStringLiteral("pause"), Qt::Key_Pause},
        {QStringLiteral("capslock"), Qt::Key_CapsLock},
        {QStringLiteral("numlock"), Qt::Key_NumLock},
        {QStringLiteral("scrolllock"), Qt::Key_ScrollLock},
        {QStringLiteral("menu"), Qt::Key_Menu},
        {QStringLiteral("contextmenu"), Qt::Key_Menu},
        {QStringLiteral("win"), Qt::Key_Super_L},
        {QStringLiteral("lwin"), Qt::Key_Super_L},
        {QStringLiteral("rwin"), Qt::Key_Super_R},
        {QStringLiteral("super"), Qt::Key_Super_L},
        {QStringLiteral("meta"), Qt::Key_Super_L},
        {QStringLiteral("cmd"), Qt::Key_Super_L},
        {QStringLiteral("volumeup"), Qt::Key_VolumeUp},
        {QStringLiteral("volumedown"), Qt::Key_VolumeDown},
        {QStringLiteral("volumemute"), Qt::Key_VolumeMute},
        {QStringLiteral("mediaplay"), Qt::Key_MediaPlay},
        {QStringLiteral("mediastop"), Qt::Key_MediaStop},
        {QStringLiteral("medianext"), Qt::Key_MediaNext},
        {QStringLiteral("mediaprevious"), Qt::Key_MediaPrevious},
    };

    const QString normalized = name.trimmed().toLower();
    if (normalized.isEmpty()) {
        return 0;
    }

    if (const auto it = table.constFind(normalized); it != table.constEnd()) {
        return it.value();
    }

    if (normalized.startsWith(QLatin1Char('f')) && normalized.length() <= 3) {
        const int number = normalized.mid(1).toInt();
        if (number >= 1 && number <= 24) {
            return Qt::Key_F1 + (number - 1);
        }
    }

    // Single characters: Qt uses the upper case ASCII value as the key code.
    if (normalized.length() == 1) {
        const QChar ch = normalized.at(0);
        if (ch.isLetterOrNumber()) {
            return ch.toUpper().unicode();
        }
        return ch.unicode();
    }

    return 0;
}

Qt::KeyboardModifiers AiKeys::modifiers(const QStringList &names, QStringList &unknown)
{
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    for (const QString &raw : names) {
        const QString name = raw.trimmed().toLower();
        if (name == QLatin1String("ctrl") || name == QLatin1String("control")) {
            modifiers |= Qt::ControlModifier;
        } else if (name == QLatin1String("alt") || name == QLatin1String("option") || name == QLatin1String("opt")) {
            modifiers |= Qt::AltModifier;
        } else if (name == QLatin1String("shift")) {
            modifiers |= Qt::ShiftModifier;
        } else if (name == QLatin1String("meta") || name == QLatin1String("win") || name == QLatin1String("super")
                   || name == QLatin1String("cmd")) {
            modifiers |= Qt::MetaModifier;
        } else {
            unknown.append(raw);
        }
    }

    return modifiers;
}

QStringList AiKeys::splitChord(const QString &chord)
{
    QStringList names = chord.toLower().split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (QString &name : names) {
        name = name.trimmed();
    }
    names.removeAll(QString());
    return names;
}
