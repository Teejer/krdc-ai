/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "aiconversation.h"

#include <QPointer>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;
class QToolButton;
class RemoteView;

/**
 * One conversation shown in the AI panel: the message log, the approval
 * prompt and the question input. Owned by AiPanel, one per connection.
 */
class AiConversationPage : public QWidget
{
    Q_OBJECT

public:
    explicit AiConversationPage(AiConversation *conversation, QWidget *parent = nullptr);

    AiConversation *conversation() const;

    /** Call when the dock becomes visible or the page becomes the current one. */
    void refresh();

Q_SIGNALS:
    void settingsRequested();

private Q_SLOTS:
    void reloadLog();
    void reloadApprovalBar();
    void scheduleRefresh();
    void send();

private:
    QString renderLog() const;
    QString escape(const QString &text) const;
    void scrollToBottom();

    QPointer<AiConversation> m_conversation;
    bool m_scrollPending = false;
    bool m_refreshPending = false;
    QTextBrowser *m_log;
    QWidget *m_approvalBar;
    QLabel *m_approvalText;
    QLabel *m_approvalReason;
    QLabel *m_approvalPreview = nullptr;
    QPushButton *m_approveButton;
    QPushButton *m_denyButton;
    QPlainTextEdit *m_input;
    QToolButton *m_screenshotButton;
    QPushButton *m_sendButton;
    QPushButton *m_stopButton;
    QLabel *m_statusLabel;
};
