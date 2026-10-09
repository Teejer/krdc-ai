/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aipanel.h"

#include "aiconversationpage.h"
#include "aisettingsdialog.h"
#include "krdc_debug.h"

#include "../core/remoteview.h"

#include <KActionCollection>
#include <KLocalizedString>

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QStackedWidget>
#include <QVBoxLayout>

AiPanel::AiPanel(QWidget *parent)
    : QWidget(parent)
{
    m_dock = new QDockWidget(parent);
    m_dock->setObjectName(QStringLiteral("aiAssistantDockWidget")); // required for saving position / state
    m_dock->setWindowTitle(i18n("AI Assistant"));
    m_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_dock->setMinimumWidth(320);

    setWindowTitle(i18n("AI Assistant"));

    // The stack belongs to this widget; the dock's widget is this widget.
    auto *panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);
    panelLayout->addWidget(m_stack);

    // Placeholder shown when the current tab has no connection.
    auto *startConversation = new AiConversation(QStringLiteral("start"), this);
    m_startPage = new AiConversationPage(startConversation, m_stack);
    connect(m_startPage, &AiConversationPage::settingsRequested, this, &AiPanel::openSettings);
    m_stack->addWidget(m_startPage);
    m_stack->setCurrentWidget(m_startPage);

    m_dock->setWidget(this);

    // Watch focus so the remote view's exclusive keyboard grab is released
    // while the user types in the panel, and restored afterwards.
    connect(qApp, &QApplication::focusChanged, this, &AiPanel::onFocusChanged);
}

QDockWidget *AiPanel::dock() const
{
    return m_dock;
}

void AiPanel::registerAction(KActionCollection *collection)
{
    QAction *action = collection->addAction(QStringLiteral("ai_assistant"), m_dock->toggleViewAction());
    action->setText(i18n("AI Assistant"));
    action->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
    // A shortcut is the only reliable way to reach the panel while a session
    // has the keyboard: the menu-bar toggle (Ctrl+M) is swallowed by the
    // remote view's key handling, and the dock itself has no other control
    // once hidden. WindowShortcut is matched before the remote view sees the
    // key, so it works even while connected.
    collection->setDefaultShortcut(action, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    action->setToolTip(i18n("Show or hide the AI Assistant panel (Ctrl+Shift+A)"));
}

AiConversationPage *AiPanel::pageFor(RemoteView *view, const QString &title)
{
    const QString key = view->connectionKey();

    if (auto *existing = m_pages.value(key, nullptr)) {
        existing->conversation()->setView(view);
        if (!title.isEmpty()) {
            existing->conversation()->setTitle(title);
        }
        return existing;
    }

    auto *conversation = new AiConversation(key, this);
    conversation->setView(view);
    conversation->setTitle(title.isEmpty() ? view->host() : title);

    auto *page = new AiConversationPage(conversation, m_stack);
    connect(page, &AiConversationPage::settingsRequested, this, &AiPanel::openSettings);

    m_pages.insert(key, page);
    m_stack->addWidget(page);
    return page;
}

void AiPanel::openSettings()
{
    AiSettingsDialog dialog(this);
    dialog.setConfig(AiConfig::load());
    if (dialog.exec() == QDialog::Accepted) {
        AiConfig config = dialog.config();
        config.save();
        // Live conversations pick the new settings up immediately.
        for (AiConversationPage *other : std::as_const(m_pages)) {
            other->conversation()->setConfig(config);
            other->refresh();
        }
        m_startPage->conversation()->setConfig(config);
        m_startPage->refresh();
    }
}

void AiPanel::setActiveView(RemoteView *view, const QString &title)
{
    m_activeView = view;

    if (!view) {
        m_current = m_startPage;
        m_stack->setCurrentWidget(m_startPage);
        return;
    }

    AiConversationPage *page = pageFor(view, title);
    m_current = page;
    m_stack->setCurrentWidget(page);
    page->refresh();
}

void AiPanel::removeConnection(const QString &key)
{
    auto it = m_pages.find(key);
    if (it == m_pages.end()) {
        return;
    }

    AiConversationPage *page = it.value();
    AiConversation *conversation = page->conversation();
    if (conversation && conversation->busy()) {
        conversation->cancel();
    }

    m_stack->removeWidget(page);
    if (m_current == page) {
        m_current = m_startPage;
        m_stack->setCurrentWidget(m_startPage);
    }
    page->deleteLater();
    // The page does not own the conversation, so free it here; the history it
    // saved to disk is reloaded the next time this connection is opened.
    delete conversation;
    m_pages.erase(it);
}

void AiPanel::onFocusChanged(QWidget *old, QWidget *now)
{
    Q_UNUSED(old);

    const bool focusInPanel = now && (now == this || isAncestorOf(now));
    // The main window's own widget tree, i.e. not the panel and not a
    // floating dock. A top-level widget's window() is itself.
    QWidget *mainWindow = nullptr;
    for (QWidget *w = now; w; w = w->parentWidget()) {
        if (!w->isWindow()) {
            mainWindow = w;
            continue;
        }
        // Reached a top-level widget: the main window if it is not the dock.
        mainWindow = (w == m_dock || w == m_dock->widget()) ? nullptr : w;
        break;
    }
    const bool focusInMainWindow = mainWindow && mainWindow->inherits("QMainWindow");

    if (focusInPanel && !m_grabSuspended) {
        // Give the keys to the chat input: drop the remote view's exclusive
        // keyboard grab for now. setKeyboardGrabSuspended keeps the user's
        // "grab all keys" setting untouched and restores the grab as soon as
        // the view is focused again.
        if (m_activeView) {
            m_activeView->setKeyboardGrabSuspended(true);
        }
        m_grabSuspended = true;
    } else if (!focusInPanel && focusInMainWindow && m_grabSuspended) {
        if (m_activeView) {
            m_activeView->setKeyboardGrabSuspended(false);
        }
        m_grabSuspended = false;
    }
}
