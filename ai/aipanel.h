/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "aiconversation.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class AiConversationPage;
class KActionCollection;
class QDockWidget;
class QStackedWidget;
class QToolButton;
class RemoteView;

/**
 * The AI assistant chat panel.
 *
 * Lives in a dock widget on the right of the main window. It owns one
 * AiConversation per connection, keyed by RemoteView::connectionKey(), and
 * shows the conversation that belongs to whichever tab is current, so each
 * remote machine keeps its own chat.
 *
 * While the keyboard focus is inside the panel, the remote view's exclusive
 * keyboard grab (the "grab all keys" mode) is suspended so that typing goes
 * to the chat instead of the remote machine.
 */
class AiPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AiPanel(QWidget *parent = nullptr);

    /** The panel's dock widget; the main window places and saves it. */
    QDockWidget *dock() const;

    /** Registers the show/hide action in @p collection. */
    void registerAction(KActionCollection *collection);

    /**
     * Called when the current tab changed. @p view is null on tabs without a
     * connection (e.g. the new connection page).
     */
    void setActiveView(RemoteView *view, const QString &title);

    /** Forgets the conversation of a connection that was closed. */
    void removeConnection(const QString &key);

private Q_SLOTS:
    void openSettings();
    void onFocusChanged(QWidget *old, QWidget *now);

private:
    AiConversationPage *pageFor(RemoteView *view, const QString &title);

    QDockWidget *m_dock;
    QStackedWidget *m_stack;
    AiConversationPage *m_startPage = nullptr;
    QHash<QString, AiConversationPage *> m_pages;
    QPointer<AiConversationPage> m_current;
    QPointer<RemoteView> m_activeView;
    bool m_grabSuspended = false;
};
