// SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>

/**
 * Helpers for showing untrusted text (a model answer, a model id, a URL, a
 * network error string) next to a translated label.
 *
 * Two KLocalizedString traps make this worth a helper:
 *
 * - KRDC does not define TRANSLATION_DOMAIN, so i18n("...") resolves to the
 *   convenience overload that finalises to a QString immediately. Chaining
 *   .arg() onto the result fills the placeholder after finalisation and
 *   leaves the "(I18N_ARGUMENT_MISSING)" marker ki18n inserted for the
 *   unfilled placeholder: i18n("Answer: %1").arg(answer) prints
 *   "Answer: ok(I18N_ARGUMENT_MISSING)". Fill trusted placeholders with the
 *   two-argument form, i18n("Answer: %1", count), and append anything
 *   untrusted with these helpers instead.
 * - Even the two-argument form must not be filled with arbitrary text: the
 *   finalisation scans the result for %N patterns, so a model that answers
 *   "ok%1" would be shown as "ok(I18N_ARGUMENT_MISSING)".
 *
 * Labels passed here must contain no placeholders; the value is appended as
 * plain data that is never re-scanned.
 */
namespace AiText {
inline QString after(const QString &label, const QString &value)
{
    return label + QLatin1Char(' ') + value;
}

inline QString quotedAfter(const QString &label, const QString &value)
{
    return label + QStringLiteral(" “%1”").arg(value);
}
}
