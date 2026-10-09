/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "aiconfig.h"
#include "aihttpclient.h"
#include "aitools.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class RemoteView;

/**
 * One conversation with the assistant, belonging to exactly one connection.
 *
 * The conversation owns the message history, runs the "ask the model, execute
 * the tools it asked for, tell it the result" loop and persists its history to
 * disk so that reconnecting to the same server picks up where it left off.
 * Everything runs on the GUI thread; slow steps (waiting, paced typing) are
 * continued through timers instead of blocking.
 */
class AiConversation : public QObject
{
    Q_OBJECT

public:
    enum class Role {
        User,      ///< typed by the user, possibly with a screenshot attached
        Assistant, ///< produced by the model, possibly with tool calls
        Tool,      ///< the result of a tool call
        Note,      ///< status or error line shown to the user but never sent
    };

    struct Entry {
        Role role = Role::User;
        QString text;
        QString toolCallId; ///< Role::Tool: which call this answers
        QString toolName; ///< display only, for Role::Tool and Role::Assistant
        QByteArray image; ///< Role::User: JPEG bytes
        QList<AiToolCall> toolCalls; ///< Role::Assistant
        QString detail; ///< Role::Note: extra explanation
        /** When this happened, for the chat log. Defaults to "now"; load()
         * overwrites it from the saved history. */
        QDateTime timestamp = QDateTime::currentDateTime();
    };

    explicit AiConversation(const QString &key, QObject *parent = nullptr);

    /** Identifies the connection this conversation belongs to. */
    QString key() const;

    QString title() const;
    void setTitle(const QString &title);

    /** The connection this conversation can act on, or null when detached. */
    RemoteView *view() const;
    void setView(RemoteView *view);

    bool busy() const;
    const QList<Entry> &entries() const;

    /** True while a tool call is waiting for the user to approve it. */
    bool awaitingApproval() const;
    /** What the pending tool call would do. */
    QString pendingSummary() const;
    /** Why the pending tool call needs approval even with auto-execute on. */
    QString pendingReason() const;

    void setConfig(const AiConfig &config);
    const AiConfig &config() const;

    /** Remembers the panel's "attach a screenshot" toggle for future turns. */
    void setAttachScreenshotDefault(bool attach);

    /** Starts a new turn. @p attachScreenshot grabs the remote screen first. */
    void ask(const QString &question, bool attachScreenshot);

    void approve();
    void deny();
    /** Abandons the current turn, including anything waiting for approval. */
    void cancel();
    void clear();

    /** @return a JPEG of the remote screen, scaled down for the model. */
    QByteArray grabScreenshot() const;

    /** @return the size of the remote screen, or an invalid size. */
    QSize screenSize() const;

Q_SIGNALS:
    void entriesChanged();
    void busyChanged();
    void approvalRequested();

private Q_SLOTS:
    void onReply(const QString &content, const QList<AiToolCall> &toolCalls);
    void onFailed(const QString &message);
    void typeNextChunk();

private:
    struct PendingTool {
        AiToolCall call;
        AiTools::Risk risk = AiTools::Risk::Action;
    };

    QJsonArray buildMessages() const;
    void sendToModel();
    void processQueue();
    void runTool(const PendingTool &pending);
    void finishTool(const AiToolCall &call, const QString &result, const QByteArray &image = QByteArray());
    void failTool(const AiToolCall &call, const QString &message);

    /**
     * Holds every name but the last down, taps the last one, then releases
     * the held keys on a short stagger. @p names is the combination in order,
     * modifiers first ("ctrl", "a"). Used by both press_key and hotkey.
     */
    bool sendChord(RemoteView *view, const QStringList &names, QString *error);
    void setBusy(bool busy);

    int startTyping(const AiToolCall &call, const QString &text, int delayMs);

    void load();
    void save() const;
    QString historyFilePath() const;

    AiHttpClient m_client;
    QString m_key;
    AiConfig m_config;
    QList<Entry> m_entries;
    QPointer<RemoteView> m_view;
    QString m_title;

    QList<PendingTool> m_queue;
    int m_queueIndex = 0;
    bool m_awaiting = false;
    bool m_denied = false;
    bool m_busy = false;
    int m_rounds = 0;

    // paced typing state
    QString m_typeRemainder;
    int m_typeDelay = 0;
    AiToolCall m_typeCall;

    // wait tool state
    bool m_waitActive = false;
    AiToolCall m_waitCall;
};
