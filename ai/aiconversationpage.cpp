/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aiconversationpage.h"

#include "aisettingsdialog.h"
#include "krdc_debug.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

#include "remoteview.h"

namespace
{
// Colors are fixed rather than palette-based for the message bubbles; they
// read well on both light and dark themes.
QString bubbleStyle(const QString &background)
{
    return QStringLiteral("background:%1;border-radius:6px;padding:6px 9px;margin:2px 0;").arg(background);
}

const QString UserBubble = QStringLiteral("#2d5f8c");
const QString AssistantBubble = QStringLiteral("#3a3f46");
const QString ToolBubble = QStringLiteral("#2e3238");
const QString NoteBubble = QStringLiteral("#6b4a2f");

/** Input box where Enter sends the question and Shift+Enter adds a line. */
class SubmitOnEnterEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    using QPlainTextEdit::QPlainTextEdit;

Q_SIGNALS:
    void submit();

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->modifiers().testFlag(Qt::ShiftModifier)) {
            Q_EMIT submit();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
};
}

AiConversationPage::AiConversationPage(AiConversation *conversation, QWidget *parent)
    : QWidget(parent)
    , m_conversation(conversation)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_log = new QTextBrowser(this);
    m_log->setOpenExternalLinks(false);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->setReadOnly(true);
    layout->addWidget(m_log, 1);

    // Approval prompt: shown when a tool call needs the user's OK.
    m_approvalBar = new QWidget(this);
    auto *approvalLayout = new QVBoxLayout(m_approvalBar);
    approvalLayout->setContentsMargins(8, 6, 8, 6);
    approvalLayout->setSpacing(4);

    m_approvalText = new QLabel(m_approvalBar);
    m_approvalText->setWordWrap(true);
    m_approvalReason = new QLabel(m_approvalBar);
    m_approvalReason->setWordWrap(true);
    m_approvalReason->setStyleSheet(QStringLiteral("font-style: italic;"));

    auto *approvalButtons = new QHBoxLayout;
    m_approveButton = new QPushButton(i18n("Approve"), m_approvalBar);
    m_denyButton = new QPushButton(i18n("Deny"), m_approvalBar);
    approvalButtons->addStretch();
    approvalButtons->addWidget(m_denyButton);
    approvalButtons->addWidget(m_approveButton);

    approvalLayout->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(i18n("The assistant wants to:")), m_approvalBar));
    approvalLayout->addWidget(m_approvalText);
    approvalLayout->addWidget(m_approvalReason);

    // A zoomed crop of the screen around the point a pending click targets,
    // so the user sees what is there, not just a coordinate.
    m_approvalPreview = new QLabel(m_approvalBar);
    m_approvalPreview->setAlignment(Qt::AlignCenter);
    m_approvalPreview->setStyleSheet(QStringLiteral("border:1px solid palette(mid);"));
    m_approvalPreview->setVisible(false);
    approvalLayout->addWidget(m_approvalPreview);
    approvalLayout->addLayout(approvalButtons);

    m_approvalBar->setStyleSheet(QStringLiteral("background:#54411f;border-radius:6px;"));
    m_approvalBar->setVisible(false);
    layout->addWidget(m_approvalBar);

    // Status line (connection state, model name).
    m_statusLabel = new QLabel(m_conversation->title(), this);
    m_statusLabel->setStyleSheet(QStringLiteral("color:palette(mid);"));
    layout->addWidget(m_statusLabel);

    // Input row.
    auto *inputRow = new QHBoxLayout;
    auto *inputEdit = new SubmitOnEnterEdit(this);
    m_input = inputEdit;
    m_input->setPlaceholderText(i18n("Ask the assistant… (Enter sends, Shift+Enter adds a line)"));
    m_input->setFixedHeight(64);
    inputRow->addWidget(m_input, 1);

    auto *inputButtons = new QVBoxLayout;
    m_sendButton = new QPushButton(i18n("Send"), this);
    m_stopButton = new QPushButton(i18n("Stop"), this);
    m_stopButton->setVisible(false);
    inputButtons->addWidget(m_sendButton);
    inputButtons->addWidget(m_stopButton);
    inputRow->addLayout(inputButtons);
    layout->addLayout(inputRow);

    // Options row.
    auto *optionsRow = new QHBoxLayout;
    m_screenshotButton = new QToolButton(this);
    m_screenshotButton->setText(i18n("📷"));
    m_screenshotButton->setCheckable(true);
    m_screenshotButton->setChecked(true);
    m_screenshotButton->setToolTip(i18n("Attach a screenshot of the remote screen to the next question"));
    optionsRow->addWidget(m_screenshotButton);
    optionsRow->addStretch();

    auto *clearButton = new QToolButton(this);
    clearButton->setText(i18n("🧹"));
    clearButton->setToolTip(i18n("Clear this conversation"));
    optionsRow->addWidget(clearButton);

    auto *settingsButton = new QToolButton(this);
    settingsButton->setText(i18n("⚙"));
    settingsButton->setToolTip(i18n("Assistant settings"));
    optionsRow->addWidget(settingsButton);
    layout->addLayout(optionsRow);

    connect(m_approveButton, &QPushButton::clicked, this, [this] {
        m_conversation->approve();
    });
    connect(m_denyButton, &QPushButton::clicked, this, [this] {
        m_conversation->deny();
    });
    connect(m_sendButton, &QPushButton::clicked, this, &AiConversationPage::send);
    connect(inputEdit, &SubmitOnEnterEdit::submit, this, &AiConversationPage::send);
    connect(m_stopButton, &QPushButton::clicked, this, [this] {
        m_conversation->cancel();
    });
    connect(clearButton, &QToolButton::clicked, this, [this] {
        m_conversation->clear();
    });
    connect(settingsButton, &QToolButton::clicked, this, &AiConversationPage::settingsRequested);

    // The approval bar and log must react to every conversation change, not
    // just the signal that opens the bar: approving, denying and cancelling all
    // report through entriesChanged/busyChanged. refresh() coalesces the burst
    // of signals into a single rebuild.
    connect(m_conversation, &AiConversation::entriesChanged, this, &AiConversationPage::scheduleRefresh);
    connect(m_conversation, &AiConversation::busyChanged, this, &AiConversationPage::scheduleRefresh);
    connect(m_conversation, &AiConversation::approvalRequested, this, &AiConversationPage::scheduleRefresh);

    // Keep the screenshot toggle in sync with the shared configuration.
    m_screenshotButton->setChecked(m_conversation->config().attachScreenshot);
    connect(m_screenshotButton, &QToolButton::toggled, this, [this](bool checked) {
        m_conversation->setAttachScreenshotDefault(checked);
    });

    reloadLog();
    reloadApprovalBar();
}

AiConversation *AiConversationPage::conversation() const
{
    return m_conversation;
}

void AiConversationPage::refresh()
{
    m_statusLabel->setText(m_conversation->title());
    reloadLog();
    reloadApprovalBar();
}

void AiConversationPage::send()
{
    const QString question = m_input->toPlainText().trimmed();
    if (question.isEmpty() || !m_conversation) {
        return;
    }

    const bool attach = m_screenshotButton->isChecked();
    m_input->clear();
    m_conversation->ask(question, attach);
}

void AiConversationPage::reloadApprovalBar()
{
    // Deliberately idempotent: this runs on every change to the conversation,
    // not just when approval starts, so the bar can never be left showing a
    // call that has already been approved, denied or cancelled.
    const bool awaiting = m_conversation && m_conversation->awaitingApproval();

    if (awaiting) {
        m_approvalText->setText(m_conversation->pendingSummary());
        m_approvalReason->setText(m_conversation->pendingReason());

        const QPixmap preview = m_conversation->pendingPreview();
        if (preview.isNull()) {
            m_approvalPreview->setVisible(false);
        } else {
            // Show the crop no larger than it was captured: the panel shows
            // the spot in context rather than a magnified close-up, which
            // keeps the approval bar compact.
            const int widest = qMin(preview.width(), 220);
            m_approvalPreview->setPixmap(
                preview.scaled(widest, 165, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            m_approvalPreview->setVisible(true);
        }
    } else {
        m_approvalPreview->setVisible(false);
    }

    m_approvalBar->setVisible(awaiting);
    m_input->setEnabled(m_conversation && !m_conversation->busy() && !awaiting);
}

void AiConversationPage::reloadLog()
{
    if (!m_conversation) {
        m_log->clear();
        return;
    }

    // Remember whether the user was scrolled to the bottom; only follow
    // output then.
    QScrollBar *bar = m_log->verticalScrollBar();
    const bool atBottom = bar->maximum() - bar->value() <= 4;

    m_log->setHtml(renderLog());

    if (atBottom) {
        scrollToBottom();
    }

    m_sendButton->setVisible(!m_conversation->busy());
    m_stopButton->setVisible(m_conversation->busy());
    m_input->setEnabled(!m_conversation->busy() && !m_conversation->awaitingApproval());
}

void AiConversationPage::scrollToBottom()
{
    // setHtml() scrolls the view back to the top, and the new document is only
    // laid out during the next layout pass, so the scrollbar maximum is still
    // stale right now. Scrolling with the cursor is Qt's supported way to pin
    // the view to the end; doing it again on the next event loop iteration
    // covers the case where the layout (or the approval bar appearing)
    // changes the height afterwards. Without this the log jumped to the top of
    // the conversation after every approved action.
    m_log->moveCursor(QTextCursor::End);
    m_log->ensureCursorVisible();

    if (!m_scrollPending) {
        m_scrollPending = true;
        QMetaObject::invokeMethod(
            this,
            [this] {
                m_scrollPending = false;
                m_log->moveCursor(QTextCursor::End);
                m_log->ensureCursorVisible();
            },
            Qt::QueuedConnection);
    }
}

void AiConversationPage::scheduleRefresh()
{
    // Approving or denying fires entriesChanged() and busyChanged() in quick
    // succession; rebuilding the log once per signal would re-run setHtml()
    // several times per action and fight over the scroll position. Coalesce
    // the refreshes into one per event loop turn.
    if (m_refreshPending) {
        return;
    }
    m_refreshPending = true;

    QMetaObject::invokeMethod(
        this,
        [this] {
            m_refreshPending = false;
            reloadLog();
            reloadApprovalBar();
        },
        Qt::QueuedConnection);
}

QString AiConversationPage::escape(const QString &text) const
{
    QString result = text;
    result.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    result.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    result.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    result.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    return result;
}

QString AiConversationPage::renderLog() const
{
    QString html = QStringLiteral("<body style='margin:0;'>");

    // Muted timestamp line under a bubble: right for the user, left for the
    // assistant. Uses HTML attributes Qt's rich text engine definitely
    // implements (align, <small>, <font color>) rather than CSS the QTextBrowser
    // silently drops - an earlier version used "font-size:smaller" and a
    // palette() colour, which Qt ignored, so the stamp rendered but was
    // effectively invisible against the bubble.
    const auto stamp = [this](const QDateTime &timestamp, const QString &alignment) {
        if (!timestamp.isValid()) {
            return QString();
        }
        const QString time = QLocale().toString(timestamp.time(), QLocale::ShortFormat);
        return QStringLiteral("<div align='%1'><small><font color='#8a8f98'>%2</font></small></div>")
            .arg(alignment, escape(time));
    };

    const QList<AiConversation::Entry> entries = m_conversation->entries();
    if (entries.isEmpty()) {
        html += QStringLiteral("<p style='color:palette(mid);padding:8px;'>%1</p>")
                    .arg(escape(i18n("Ask me about what you see on the remote machine. I can look at the screen, click, "
                                     "type and scroll for you.")));
    }

    for (const AiConversation::Entry &entry : entries) {
        switch (entry.role) {
        case AiConversation::Role::User: {
            html += QStringLiteral("<div style='%1'><b>%2</b><br/>%3</div>%4")
                        .arg(bubbleStyle(UserBubble), escape(i18n("You")), escape(entry.text), stamp(entry.timestamp, QStringLiteral("right")));
            if (!entry.image.isEmpty()) {
                html += QStringLiteral("<div style='%1'><b>%2</b><br/>%3</div>")
                            .arg(bubbleStyle(ToolBubble), escape(i18n("Assistant")),
                                 escape(i18n("Took a screenshot (shown when I report what I see).")));
            }
            break;
        }
        case AiConversation::Role::Assistant: {
            QString block = QStringLiteral("<div style='%1'><b>%2</b>").arg(bubbleStyle(AssistantBubble), escape(i18n("Assistant")));
            if (!entry.text.isEmpty()) {
                block += QStringLiteral("<br/>%1").arg(escape(entry.text));
            }
            for (const AiToolCall &call : entry.toolCalls) {
                block += QStringLiteral("<br/><small>→ %1</small>").arg(escape(AiTools::describe(call.name, call.arguments)));
            }
            block += QStringLiteral("</div>");
            html += block;
            // The assistant's timestamp goes bottom-left of its response. Only
            // stamp the final message of a tool round, not the intermediate
            // tool-call-only steps.
            if (!entry.text.isEmpty()) {
                html += stamp(entry.timestamp, QStringLiteral("left"));
            }
            break;
        }
        case AiConversation::Role::Tool: {
            html += QStringLiteral("<div style='%1'><small>%2</small></div>")
                        .arg(bubbleStyle(ToolBubble), escape(entry.text.left(400)));
            break;
        }
        case AiConversation::Role::Note: {
            QString block = QStringLiteral("<div style='%1'><b>%2</b><br/>%3").arg(bubbleStyle(NoteBubble), escape(i18n("Note")),
                                                                                 escape(entry.text));
            if (!entry.detail.isEmpty()) {
                block += QStringLiteral("<br/><small>%1</small>").arg(escape(entry.detail));
            }
            block += QStringLiteral("</div>");
            html += block;
            break;
        }
        }
    }

    html += QStringLiteral("</body>");
    return html;
}

#include "aiconversationpage.moc"
