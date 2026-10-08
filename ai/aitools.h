/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/**
 * A function call the model asked for.
 */
struct AiToolCall {
    QString id;
    QString name;
    QJsonObject arguments;
    /** Set when the arguments were not valid JSON or named an unknown tool. */
    QString error;
};

namespace AiTools
{
/**
 * How much damage a tool call can do, which decides whether the panel asks
 * the user first.
 */
enum class Risk {
    /** Only reads state, e.g. a screenshot. Never asks. */
    Safe,
    /** Changes the remote machine. Asks unless auto-execute is on. */
    Action,
    /** Destructive or hard to undo. Always asks. */
    Dangerous,
};

/** @return the tool definitions in the format chat completions expects. */
QJsonArray schemas();

/** @return true if @p name is one of the tools this panel implements. */
bool isKnownTool(const QString &name);

/** @return how risky @p name with @p arguments is. */
Risk risk(const QString &name, const QJsonObject &arguments);

/** @return a one line description of the call for the approval prompt. */
QString describe(const QString &name, const QJsonObject &arguments);

/** @return why a call was classified as dangerous, for the approval prompt. */
QString dangerReason(const QString &name, const QJsonObject &arguments);
}
