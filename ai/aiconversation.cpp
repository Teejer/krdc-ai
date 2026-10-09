/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aiconversation.h"

#include "aikeys.h"
#include "krdc_debug.h"

#include "../core/remoteview.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <KLocalizedString>

namespace
{
// Safety limits so a confused model cannot spin forever or stall the UI.
constexpr int MaxToolRounds = 25;
constexpr int MaxHistoryEntries = 200;
constexpr int TypeChunkSize = 64;
constexpr int MaxTypeDelayMs = 200;
constexpr double MaxWaitSeconds = 30.0;

// How many trailing entries survive into the saved history.
constexpr int SavedHistoryEntries = 120;

int clampInt(double value, int minimum, int maximum, int fallback)
{
    if (value <= 0) {
        return fallback;
    }
    return std::clamp(int(value), minimum, maximum);
}

// Reads one coordinate the model claims is a number. Models that have not
// looked at the screen send placeholders instead: measured in the wild, this
// model answers "[redacted]" rather than invent a number it cannot see, and
// QJsonValue::toDouble() turns that into 0.0, which used to land the click in
// the top-left corner with a confident "Clicked at (0, 0)" afterwards.
bool numberArg(const QJsonObject &args, const QString &name, int *value)
{
    const QJsonValue entry = args.value(name);
    if (entry.isDouble()) {
        *value = int(entry.toDouble());
        return true;
    }
    if (entry.isString()) {
        bool ok = false;
        const double parsed = entry.toString().trimmed().toDouble(&ok);
        if (ok) {
            *value = int(parsed);
            return true;
        }
    }
    return false;
}

// Reads a point, refusing coordinates that are not numbers or fall outside
// the screen. The error tells the model to look at the screen first, which is
// the only way it can produce a real coordinate.
bool pointArg(const QJsonObject &args, const QString &xName, const QString &yName, QPoint *point, QString *error, const QSize &screen)
{
    int x = 0;
    int y = 0;
    if (!numberArg(args, xName, &x) || !numberArg(args, yName, &y)) {
        *error = QStringLiteral("The coordinates %1 and %2 must be numbers taken from a screenshot; what was given is not one. "
                                "Call take_screenshot first and read the position off that image.")
                     .arg(xName, yName);
        return false;
    }
    if (screen.isValid() && !screen.isEmpty() && (x < 0 || y < 0 || x > screen.width() || y > screen.height())) {
        *error = QStringLiteral("(%1, %2) is outside the screen, which is %3x%4. Take a screenshot and read the position "
                                "off that image.")
                     .arg(x)
                     .arg(y)
                     .arg(screen.width())
                     .arg(screen.height());
        return false;
    }
    *point = QPoint{x, y};
    return true;
}

// Strips special-token markup that leaks into visible text. Observed from
// mlx-serve with thinking enabled: the end-of-turn token arrives as literal
// text at the end of a reply, sometimes instead of the tool call the model
// meant to emit. Leaving it in the history teaches the model to repeat it, so
// it is removed on the way in.
QString stripStrayTokens(const QString &text)
{
    static const QRegularExpression token(QStringLiteral("<\\/?s>|<\\|im_end\\|>|<\\|endoftext\\|>"));
    QString out = text;
    out.remove(token);
    return out.trimmed();
}

Qt::MouseButton buttonFromName(const QString &name, bool *ok = nullptr)
{
    if (ok) {
        *ok = true;
    }
    if (name.isEmpty() || name == QLatin1String("left")) {
        return Qt::LeftButton;
    }
    if (name == QLatin1String("right")) {
        return Qt::RightButton;
    }
    if (name == QLatin1String("middle")) {
        return Qt::MiddleButton;
    }
    if (ok) {
        *ok = false;
    }
    return Qt::LeftButton;
}
}

AiConversation::AiConversation(const QString &key, QObject *parent)
    : QObject(parent)
    , m_key(key)
    , m_config(AiConfig::load())
{
    m_client.setConfig(m_config);
    connect(&m_client, &AiHttpClient::replyReceived, this, &AiConversation::onReply);
    connect(&m_client, &AiHttpClient::requestFailed, this, &AiConversation::onFailed);

    if (m_config.rememberHistory) {
        load();
    }
}

QString AiConversation::key() const
{
    return m_key;
}

QString AiConversation::title() const
{
    return m_title;
}

void AiConversation::setTitle(const QString &title)
{
    m_title = title;
}

RemoteView *AiConversation::view() const
{
    return m_view;
}

void AiConversation::setView(RemoteView *view)
{
    m_view = view;
}

bool AiConversation::busy() const
{
    return m_busy;
}

const QList<AiConversation::Entry> &AiConversation::entries() const
{
    return m_entries;
}

bool AiConversation::awaitingApproval() const
{
    return m_awaiting;
}

QString AiConversation::pendingSummary() const
{
    if (m_queueIndex < m_queue.size()) {
        const PendingTool &pending = m_queue.at(m_queueIndex);
        return AiTools::describe(pending.call.name, pending.call.arguments);
    }
    return {};
}

QString AiConversation::pendingReason() const
{
    if (m_queueIndex < m_queue.size()) {
        const PendingTool &pending = m_queue.at(m_queueIndex);
        return AiTools::dangerReason(pending.call.name, pending.call.arguments);
    }
    return {};
}

QPixmap AiConversation::pendingPreview() const
{
    if (m_queueIndex >= m_queue.size()) {
        return {};
    }
    const PendingTool &pending = m_queue.at(m_queueIndex);
    const QString name = pending.call.name;
    if (name != QLatin1String("mouse_click") && name != QLatin1String("mouse_drag") && name != QLatin1String("mouse_scroll")) {
        return {};
    }
    if (!m_view || m_view->status() != RemoteView::Connected) {
        return {};
    }

    // Work in screen pixels: the full-resolution grab, with the model's
    // screenshot coordinates mapped in the same way runTool will map the
    // action itself, so the crosshair marks exactly where the click lands.
    const QPixmap screen = m_view->takeScreenshot();
    if (screen.isNull()) {
        return {};
    }

    const QJsonObject args = pending.call.arguments;
    QList<QPoint> points;
    const QSize aim = aimSize();
    auto read = [&args, &aim](const QString &xName, const QString &yName) -> QPoint {
        int x = 0;
        int y = 0;
        if (!numberArg(args, xName, &x) || !numberArg(args, yName, &y)) {
            return QPoint{-1, -1};
        }
        if (aim.isValid() && !aim.isEmpty() && (x < 0 || y < 0 || x > aim.width() || y > aim.height())) {
            return QPoint{-1, -1};
        }
        return QPoint{x, y};
    };

    if (name == QLatin1String("mouse_drag")) {
        const QPoint from = read(QStringLiteral("from_x"), QStringLiteral("from_y"));
        const QPoint to = read(QStringLiteral("to_x"), QStringLiteral("to_y"));
        if (from.x() < 0 || to.x() < 0) {
            return {};
        }
        points.append(toScreen(from));
        points.append(toScreen(to));
    } else {
        const QPoint pos = read(QStringLiteral("x"), QStringLiteral("y"));
        if (pos.x() < 0) {
            return {};
        }
        points.append(toScreen(pos));
    }

    // A crop that holds every marked point with margin, so a drag shows both
    // ends, capped so a huge drag does not shrink the target to nothing.
    QRect bounds{QRect(points.first(), QSize{1, 1})};
    for (const QPoint &point : points) {
        bounds = bounds.united(QRect{point, QSize{1, 1}});
    }
    constexpr int Margin = 90;
    bounds = bounds.adjusted(-Margin, -Margin, Margin, Margin).intersected(screen.rect());
    if (bounds.isEmpty()) {
        return {};
    }
    if (bounds.width() > 480 || bounds.height() > 360) {
        // Too big to read a control in: fall back to a fixed crop on the
        // first point (the click itself), which is always a single point.
        const QPoint anchor = points.first();
        bounds = QRect{anchor.x() - 160, anchor.y() - 120, 320, 240}.intersected(screen.rect());
    }

    QPixmap crop = screen.copy(bounds);

    QPainter painter{&crop};
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen{QColor{255, 60, 60}, 2.0};
    painter.setPen(pen);
    for (const QPoint &point : points) {
        const QPoint local = point - bounds.topLeft();
        if (!bounds.contains(point)) {
            continue;
        }
        constexpr int Arm = 12;
        painter.drawLine(local.x() - Arm, local.y(), local.x() + Arm, local.y());
        painter.drawLine(local.x(), local.y() - Arm, local.x(), local.y() + Arm);
        painter.drawEllipse(local, 8, 8);
    }
    painter.end();

    return crop;
}

void AiConversation::setConfig(const AiConfig &config)
{
    m_config = config;
    m_client.setConfig(config);
}

const AiConfig &AiConversation::config() const
{
    return m_config;
}

void AiConversation::setAttachScreenshotDefault(bool attach)
{
    if (m_config.attachScreenshot == attach) {
        return;
    }
    m_config.attachScreenshot = attach;
    m_config.save();
}

void AiConversation::ask(const QString &question, bool attachScreenshot)
{
    if (m_busy) {
        return;
    }

    Entry entry;
    entry.role = Role::User;
    entry.text = question;
    if (attachScreenshot) {
        entry.image = grabScreenshot();
    }
    m_entries.append(entry);

    m_rounds = 0;
    setBusy(true);
    Q_EMIT entriesChanged();
    sendToModel();
}

void AiConversation::approve()
{
    if (!m_awaiting) {
        return;
    }
    m_awaiting = false;

    const PendingTool pending = m_queue.at(m_queueIndex);
    Q_EMIT entriesChanged();

    runTool(pending);

    // Same continuation rule as processQueue(): if the tool handed work to an
    // async step, its slot advances the queue when it reports back.
    if (m_client.busy() || !m_typeRemainder.isEmpty() || m_waitActive) {
        return;
    }
    m_queueIndex++;
    processQueue();
}

void AiConversation::deny()
{
    if (!m_awaiting) {
        return;
    }
    m_awaiting = false;
    m_denied = true;

    // processQueue() sees m_denied and reports the rejection to the model as
    // this call's tool result, then continues with the remaining calls.
    Q_EMIT entriesChanged();
    processQueue();
}

void AiConversation::cancel()
{
    if (!m_busy) {
        return;
    }

    m_awaiting = false;
    m_denied = false;
    m_queue.clear();
    m_queueIndex = 0;
    m_typeRemainder.clear();
    m_waitActive = false;
    m_client.cancel();

    Entry note;
    note.role = Role::Note;
    note.text = i18n("Stopped.");
    m_entries.append(note);

    setBusy(false);
    Q_EMIT entriesChanged();
    save();
}

void AiConversation::clear()
{
    if (m_busy) {
        cancel();
    }
    m_entries.clear();
    save();
    Q_EMIT entriesChanged();
}

QByteArray AiConversation::grabScreenshot()
{
    if (!m_view || m_view->status() != RemoteView::Connected) {
        return {};
    }

    QPixmap pixmap = m_view->takeScreenshot();
    if (pixmap.isNull()) {
        return {};
    }

    const int maxSize = qMax(320, m_config.imageMaxSize);
    if (pixmap.width() > maxSize || pixmap.height() > maxSize) {
        pixmap = pixmap.scaled(QSize{maxSize, maxSize}, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    QByteArray bytes;
    QBuffer buffer{&bytes};
    buffer.open(QIODevice::WriteOnly);
    if (!pixmap.save(&buffer, "JPEG", std::clamp(m_config.imageQuality, 10, 100))) {
        return {};
    }
    m_sentImageSize = pixmap.size();
    return bytes;
}

QPoint AiConversation::toScreen(const QPoint &pos) const
{
    const QSize screen = screenSize();
    if (screen.isEmpty() || !m_sentImageSize.isValid() || m_sentImageSize.isEmpty()
        || m_sentImageSize == screen) {
        return pos;
    }

    // Integer math with rounding half a pixel either way is far below the
    // size of a button; the model's own reading of the image is the coarse
    // step here, not this conversion.
    const int x = qRound(qreal(pos.x()) * screen.width() / m_sentImageSize.width());
    const int y = qRound(qreal(pos.y()) * screen.height() / m_sentImageSize.height());
    return QPoint{std::clamp(x, 0, screen.width() - 1), std::clamp(y, 0, screen.height() - 1)};
}

QSize AiConversation::aimSize() const
{
    // Coordinates are quoted in the pixels of the screenshot the model was
    // last given, which is the only picture it can read a position from;
    // before any screenshot has been sent, the screen itself is the bound.
    if (m_sentImageSize.isValid() && !m_sentImageSize.isEmpty()) {
        return m_sentImageSize;
    }
    return screenSize();
}

QSize AiConversation::screenSize() const
{
    if (!m_view) {
        return {};
    }
    const QSize size = m_view->framebufferSize();
    return size.isValid() && !size.isEmpty() ? size : m_view->size();
}

void AiConversation::sendToModel()
{
    m_client.request(buildMessages(), AiTools::schemas());
}

QJsonArray AiConversation::buildMessages() const
{
    QJsonArray messages;

    QString system = m_config.systemPrompt.trimmed();
    if (system.isEmpty()) {
        system = AiConfig::defaultSystemPrompt();
    }

    QJsonObject systemMessage;
    systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMessage.insert(QStringLiteral("content"), system);
    messages.append(systemMessage);

    for (const Entry &entry : m_entries) {
        switch (entry.role) {
        case Role::User: {
            QJsonObject message;
            message.insert(QStringLiteral("role"), QStringLiteral("user"));
            if (entry.image.isEmpty()) {
                message.insert(QStringLiteral("content"), entry.text);
            } else {
                QJsonArray parts;

                QJsonObject text;
                text.insert(QStringLiteral("type"), QStringLiteral("text"));
                text.insert(QStringLiteral("text"), entry.text);
                parts.append(text);

                QJsonObject image;
                image.insert(QStringLiteral("type"), QStringLiteral("image_url"));
                QJsonObject imageUrl;
                imageUrl.insert(QStringLiteral("url"), QStringLiteral("data:image/jpeg;base64,%1").arg(QString::fromLatin1(entry.image.toBase64())));
                image.insert(QStringLiteral("image_url"), imageUrl);
                parts.append(image);

                message.insert(QStringLiteral("content"), parts);
            }
            messages.append(message);
            break;
        }
        case Role::Assistant: {
            QJsonObject message;
            message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            message.insert(QStringLiteral("content"), entry.text.isEmpty() ? QString() : entry.text);

            if (!entry.toolCalls.isEmpty()) {
                QJsonArray calls;
                for (const AiToolCall &call : entry.toolCalls) {
                    QJsonObject object;
                    object.insert(QStringLiteral("id"), call.id);
                    object.insert(QStringLiteral("type"), QStringLiteral("function"));
                    QJsonObject function;
                    function.insert(QStringLiteral("name"), call.name);
                    function.insert(QStringLiteral("arguments"), QString::fromUtf8(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact)));
                    object.insert(QStringLiteral("function"), function);
                    calls.append(object);
                }
                message.insert(QStringLiteral("tool_calls"), calls);
            }
            messages.append(message);
            break;
        }
        case Role::Tool: {
            QJsonObject message;
            message.insert(QStringLiteral("role"), QStringLiteral("tool"));
            message.insert(QStringLiteral("tool_call_id"), entry.toolCallId);
            if (entry.image.isEmpty()) {
                message.insert(QStringLiteral("content"), entry.text);
            } else {
                // A screenshot result: text plus image, which several OpenAI
                // compatible servers accept in a tool message.
                QJsonArray parts;

                QJsonObject text;
                text.insert(QStringLiteral("type"), QStringLiteral("text"));
                text.insert(QStringLiteral("text"), entry.text);
                parts.append(text);

                QJsonObject image;
                image.insert(QStringLiteral("type"), QStringLiteral("image_url"));
                QJsonObject imageUrl;
                imageUrl.insert(QStringLiteral("url"), QStringLiteral("data:image/jpeg;base64,%1").arg(QString::fromLatin1(entry.image.toBase64())));
                image.insert(QStringLiteral("image_url"), imageUrl);
                parts.append(image);

                message.insert(QStringLiteral("content"), parts);
            }
            messages.append(message);
            break;
        }
        case Role::Note:
            // Display-only, never sent to the model.
            break;
        }
    }

    return messages;
}

void AiConversation::onReply(const QString &content, const QList<AiToolCall> &toolCalls)
{
    Entry entry;
    entry.role = Role::Assistant;
    entry.text = stripStrayTokens(content);
    entry.toolCalls = toolCalls;
    for (AiToolCall &call : entry.toolCalls) {
        call.name = stripStrayTokens(call.name);
    }
    m_entries.append(entry);
    Q_EMIT entriesChanged();

    if (toolCalls.isEmpty()) {
        setBusy(false);
        save();
        return;
    }

    if (++m_rounds > MaxToolRounds) {
        Entry note;
        note.role = Role::Note;
        note.text = i18n("Too many tool calls in a row; stopping to avoid a loop. Ask me again if you want me to continue.");
        m_entries.append(note);
        setBusy(false);
        Q_EMIT entriesChanged();
        save();
        return;
    }

    m_queue.clear();
    m_queueIndex = 0;
    for (const AiToolCall &call : toolCalls) {
        PendingTool pending;
        pending.call = call;
        pending.risk = AiTools::risk(call.name, call.arguments);
        m_queue.append(pending);
    }

    processQueue();
}

void AiConversation::onFailed(const QString &message)
{
    Entry note;
    note.role = Role::Note;
    note.text = i18n("The assistant could not answer.");
    note.detail = message;
    m_entries.append(note);

    setBusy(false);
    Q_EMIT entriesChanged();
    save();
}

void AiConversation::processQueue()
{
    while (m_queueIndex < m_queue.size()) {
        const PendingTool pending = m_queue.at(m_queueIndex);

        if (!pending.call.error.isEmpty()) {
            failTool(pending.call, pending.call.error);
            m_queueIndex++;
            continue;
        }

        if (m_denied) {
            m_denied = false;
            failTool(pending.call, QStringLiteral("The user rejected this action. Do not retry it without being asked to."));
            m_queueIndex++;
            continue;
        }

        const bool autoRun = pending.risk == AiTools::Risk::Safe || (m_config.autoExecute && pending.risk == AiTools::Risk::Action);
        if (!autoRun) {
            m_awaiting = true;
            Q_EMIT entriesChanged();
            Q_EMIT approvalRequested();
            return;
        }

        runTool(pending);

        // runTool is synchronous unless it handed work to the model, started
        // paced typing or a wait timer. In those cases the continuation
        // happens in a slot; stop here so we do not run later calls out of
        // order.
        if (m_client.busy() || !m_typeRemainder.isEmpty() || m_waitActive) {
            return;
        }
        m_queueIndex++;
    }

    if (!m_queue.isEmpty() && !m_typeRemainder.isEmpty()) {
        return; // paced typing still running, its slot continues from here
    }

    if (m_client.busy() || m_waitActive) {
        return;
    }

    m_queue.clear();
    m_queueIndex = 0;

    // If the round we just finished was a tool round, the model needs the
    // results before it can continue the turn.
    const bool wasToolRound = !m_entries.isEmpty() && m_entries.last().role == Role::Tool;
    if (wasToolRound && m_rounds < MaxToolRounds) {
        Q_EMIT entriesChanged();
        sendToModel();
        return;
    }

    setBusy(false);
    Q_EMIT entriesChanged();
    save();
}

void AiConversation::finishTool(const AiToolCall &call, const QString &result, const QByteArray &image)
{
    Entry entry;
    entry.role = Role::Tool;
    entry.text = result;
    entry.toolCallId = call.id;
    entry.toolName = call.name;
    entry.image = image;
    m_entries.append(entry);
    Q_EMIT entriesChanged();
}

void AiConversation::failTool(const AiToolCall &call, const QString &message)
{
    finishTool(call, QStringLiteral("error: %1").arg(message));
}

void AiConversation::setBusy(bool busy)
{
    if (m_busy == busy) {
        return;
    }
    m_busy = busy;
    Q_EMIT busyChanged();
}

void AiConversation::runTool(const PendingTool &pending)
{
    const AiToolCall &call = pending.call;
    const QString name = call.name;
    const QJsonObject &args = call.arguments;

    RemoteView *view = m_view;

    if (name == QLatin1String("take_screenshot")) {
        const QByteArray image = grabScreenshot();
        if (image.isEmpty()) {
            failTool(call, QStringLiteral("No screenshot available. Is the connection established?"));
        } else {
            finishTool(call,
                       QStringLiteral("Screenshot attached; this image is %1x%2 pixels, the screen is %3x%4 pixels. "
                                      "Point tools take coordinates in this image's pixels.")
                           .arg(m_sentImageSize.width())
                           .arg(m_sentImageSize.height())
                           .arg(screenSize().width())
                           .arg(screenSize().height()),
                       image);
        }
        return;
    }

    if (name == QLatin1String("save_screenshot")) {
        if (!m_view || m_view->status() != RemoteView::Connected) {
            failTool(call, QStringLiteral("No screenshot available. Is the connection established?"));
            return;
        }

        // The saved file is the full-resolution PNG, unlike the downscaled
        // JPEG that goes to the model: the point of this tool is a readable
        // image for a guide.
        QPixmap pixmap = m_view->takeScreenshot();
        if (pixmap.isNull()) {
            failTool(call, QStringLiteral("No screenshot available. Is the connection established?"));
            return;
        }

        // The model supplies only a base name; anything path-shaped is
        // stripped so the file always lands in the configured folder.
        QString base = args.value(QStringLiteral("filename")).toString().trimmed();
        base.remove(QRegularExpression(QStringLiteral("[/\\\\]")));
        base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]")), QStringLiteral("_"));
        base = base.trimmed();
        while (base.startsWith(QLatin1Char('.'))) {
            base.remove(0, 1);
        }
        if (base.length() > 64) {
            base.truncate(64);
        }
        if (base.isEmpty()) {
            base = QDateTime::currentDateTime().toString(QStringLiteral("screenshot-yyyy-MM-dd-HHmmss"));
        }

        QDir directory(m_config.screenshotPath);
        if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
            failTool(call, QStringLiteral("Could not create the screenshot folder %1.").arg(directory.absolutePath()));
            return;
        }

        QString fileName = base + QStringLiteral(".png");
        int suffix = 2;
        while (directory.exists(fileName)) {
            fileName = base + QLatin1Char('-') + QString::number(suffix++) + QStringLiteral(".png");
        }

        QFile file{directory.filePath(fileName)};
        if (!file.open(QIODevice::WriteOnly) || !pixmap.save(&file, "PNG")) {
            failTool(call, QStringLiteral("Could not write %1.").arg(file.fileName()));
            return;
        }

        finishTool(call, QStringLiteral("Saved %1 (%2x%3).").arg(file.fileName()).arg(pixmap.width()).arg(pixmap.height()));
        return;
    }

    if (name == QLatin1String("screen_info")) {
        const QSize size = screenSize();
        QJsonObject result;
        result.insert(QStringLiteral("host"), view ? view->host() : QString());
        result.insert(QStringLiteral("url"), view ? view->url().toString() : QString());
        result.insert(QStringLiteral("screen_width"), size.width());
        result.insert(QStringLiteral("screen_height"), size.height());
        result.insert(QStringLiteral("screenshot_width"), m_sentImageSize.width());
        result.insert(QStringLiteral("screenshot_height"), m_sentImageSize.height());
        result.insert(QStringLiteral("connected"), view && view->status() == RemoteView::Connected);
        result.insert(QStringLiteral("input_ready"), view && view->supportsAiControl());
        finishTool(call, QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
        return;
    }

    if (!view || view->status() != RemoteView::Connected) {
        failTool(call, QStringLiteral("Not connected to the remote machine right now."));
        return;
    }

    if (name == QLatin1String("mouse_move")) {
        QPoint raw;
        QString error;
        if (!pointArg(args, QStringLiteral("x"), QStringLiteral("y"), &raw, &error, aimSize())) {
            failTool(call, error);
            return;
        }
        const QPoint pos = toScreen(raw);
        view->aiMouseMove(pos);
        finishTool(call, QStringLiteral("Pointer moved to (%1, %2).").arg(pos.x()).arg(pos.y()));
        return;
    }

    if (name == QLatin1String("mouse_click")) {
        bool buttonOk = true;
        const Qt::MouseButton button = buttonFromName(args.value(QStringLiteral("button")).toString(), &buttonOk);
        if (!buttonOk) {
            failTool(call, QStringLiteral("Unknown mouse button “%1”.").arg(args.value(QStringLiteral("button")).toString()));
            return;
        }

        QPoint raw;
        QString error;
        if (!pointArg(args, QStringLiteral("x"), QStringLiteral("y"), &raw, &error, aimSize())) {
            failTool(call, error);
            return;
        }
        const QPoint pos = toScreen(raw);
        const int clicks = clampInt(args.value(QStringLiteral("clicks")).toDouble(1), 1, 3, 1);

        view->aiMouseMove(pos);
        for (int i = 0; i < clicks; ++i) {
            view->aiMouseButton(pos, button, true);
            view->aiMouseButton(pos, button, false);
        }
        finishTool(call, QStringLiteral("Clicked %1× at (%2, %3).").arg(clicks).arg(pos.x()).arg(pos.y()));
        return;
    }

    if (name == QLatin1String("mouse_drag")) {
        bool buttonOk = true;
        const Qt::MouseButton button = buttonFromName(args.value(QStringLiteral("button")).toString(), &buttonOk);
        if (!buttonOk) {
            failTool(call, QStringLiteral("Unknown mouse button “%1”.").arg(args.value(QStringLiteral("button")).toString()));
            return;
        }

        QPoint rawFrom;
        QPoint rawTo;
        QString error;
        if (!pointArg(args, QStringLiteral("from_x"), QStringLiteral("from_y"), &rawFrom, &error, aimSize())
            || !pointArg(args, QStringLiteral("to_x"), QStringLiteral("to_y"), &rawTo, &error, aimSize())) {
            failTool(call, error);
            return;
        }
        const QPoint from = toScreen(rawFrom);
        const QPoint to = toScreen(rawTo);

        // Intermediate moves so the remote drags (text selection, window
        // moves) track the pointer instead of teleporting.
        view->aiMouseMove(from);
        view->aiMouseButton(from, button, true);
        constexpr int Steps = 8;
        for (int i = 1; i <= Steps; ++i) {
            const qreal t = qreal(i) / Steps;
            view->aiMouseMove(QPoint{from.x() + int((to.x() - from.x()) * t), from.y() + int((to.y() - from.y()) * t)});
        }
        view->aiMouseButton(to, button, false);
        finishTool(call, QStringLiteral("Dragged from (%1, %2) to (%3, %4).").arg(from.x()).arg(from.y()).arg(to.x()).arg(to.y()));
        return;
    }

    if (name == QLatin1String("mouse_scroll")) {
        QPoint raw;
        QString error;
        if (!pointArg(args, QStringLiteral("x"), QStringLiteral("y"), &raw, &error, aimSize())) {
            failTool(call, error);
            return;
        }
        const QPoint pos = toScreen(raw);
        const QString direction = args.value(QStringLiteral("direction")).toString().toLower();
        const int notches = clampInt(args.value(QStringLiteral("notches")).toDouble(1), 1, 20, 1);

        QPoint delta;
        if (direction == QLatin1String("up")) {
            delta = QPoint{0, 120 * notches};
        } else if (direction == QLatin1String("down")) {
            delta = QPoint{0, -120 * notches};
        } else if (direction == QLatin1String("left")) {
            delta = QPoint{-120 * notches, 0};
        } else if (direction == QLatin1String("right")) {
            delta = QPoint{120 * notches, 0};
        } else {
            failTool(call, QStringLiteral("Unknown scroll direction “%1”. Use up, down, left or right.").arg(direction));
            return;
        }

        view->aiMouseWheel(pos, delta);
        finishTool(call, QStringLiteral("Scrolled %1×%2 at (%3, %4).").arg(notches).arg(direction, pos.x()).arg(pos.y()));
        return;
    }

    if (name == QLatin1String("type_text")) {
        const QString text = args.value(QStringLiteral("text")).toString();
        if (text.isEmpty()) {
            failTool(call, QStringLiteral("No text was given."));
            return;
        }

        const int delay = clampInt(args.value(QStringLiteral("delay_ms")).toDouble(0), 0, MaxTypeDelayMs, 0);
        if (delay == 0) {
            if (view->aiTypeText(text)) {
                finishTool(call, QStringLiteral("Typed %1 characters.").arg(text.length()));
            } else {
                finishTool(call, QStringLiteral("Typed %1 characters, but some characters could not be injected.").arg(text.length()));
            }
            return;
        }

        // Paced typing: send in chunks on a timer so the remote field keeps up.
        m_typeCall = call;
        m_typeDelay = delay;
        m_typeRemainder = text;
        QMetaObject::invokeMethod(this, &AiConversation::typeNextChunk, Qt::QueuedConnection);
        return;
    }

    if (name == QLatin1String("press_key")) {
        QStringList names;
        for (const QJsonValue &value : args.value(QStringLiteral("modifiers")).toArray()) {
            names.append(value.toString().trimmed().toLower());
        }
        // Models that cannot emit the modifiers array often write the whole
        // combination into key instead: "ctrl+a". Accept both spellings.
        names.append(AiKeys::splitChord(args.value(QStringLiteral("key")).toString()));
        names.removeAll(QString());

        QString error;
        if (!sendChord(view, names, &error)) {
            failTool(call, error);
            return;
        }
        finishTool(call, QStringLiteral("Pressed %1.").arg(names.join(QLatin1Char('+'))));
        return;
    }

    if (name == QLatin1String("hotkey")) {
        // Normally one string like "ctrl+a"; the old array form is accepted
        // too so an in-flight conversation never breaks on the schema change.
        const QJsonValue keys = args.value(QStringLiteral("keys"));
        QStringList names;
        if (keys.isArray()) {
            for (const QJsonValue &entry : keys.toArray()) {
                names.append(AiKeys::splitChord(entry.toString()));
            }
        } else {
            names = AiKeys::splitChord(keys.toString());
        }
        names.removeAll(QString());

        QString error;
        if (!sendChord(view, names, &error)) {
            failTool(call, error);
            return;
        }
        finishTool(call, QStringLiteral("Pressed %1.").arg(names.join(QLatin1Char('+'))));
        return;
    }

    if (name == QLatin1String("wait")) {
        const double seconds = std::clamp(args.value(QStringLiteral("seconds")).toDouble(1.0), 0.1, MaxWaitSeconds);
        m_waitCall = call;
        m_waitActive = true;
        QTimer::singleShot(int(seconds * 1000.0), this, [this] {
            m_waitActive = false;
            if (!m_busy) {
                return; // the turn was cancelled while we waited
            }
            finishTool(m_waitCall, QStringLiteral("Waited, you can look at the screen again."));
            m_queueIndex++;
            processQueue();
        });
        return;
    }

    failTool(call, QStringLiteral("Unknown tool “%1”.").arg(name));
}

void AiConversation::typeNextChunk()
{
    RemoteView *view = m_view;
    if (!view || !m_busy) {
        m_typeRemainder.clear();
        return;
    }

    const QString chunk = m_typeRemainder.left(TypeChunkSize);
    m_typeRemainder.remove(0, chunk.length());

    const bool ok = view->aiTypeText(chunk);
    if (!ok) {
        m_typeRemainder.clear();
        failTool(m_typeCall, QStringLiteral("Typing failed partway through; the connection may not accept text input."));
        m_queueIndex++;
        processQueue();
        return;
    }

    if (m_typeRemainder.isEmpty()) {
        finishTool(m_typeCall, QStringLiteral("Typed text."));
        m_queueIndex++;
        processQueue();
    } else {
        QTimer::singleShot(m_typeDelay, this, &AiConversation::typeNextChunk);
    }
}

bool AiConversation::sendChord(RemoteView *view, const QStringList &names, QString *error)
{
    if (names.isEmpty() || names.size() > 8) {
        *error = QStringLiteral("The key combination must hold between 1 and 8 keys.");
        return false;
    }

    QVector<int> keys;
    keys.reserve(names.size());
    for (const QString &name : names) {
        if (name == QLatin1String("backtab")) {
            // Reverse tab is Shift+Tab: Qt has no separate key for it and the
            // wire only knows Tab with Shift held.
            if (!keys.contains(AiKeys::qtKey(QStringLiteral("shift")))) {
                keys.append(AiKeys::qtKey(QStringLiteral("shift")));
            }
            keys.append(AiKeys::qtKey(QStringLiteral("tab")));
            continue;
        }
        const int key = AiKeys::qtKey(name);
        if (key == 0) {
            *error = QStringLiteral("Unknown key “%1”. Use names like ctrl, alt, shift, win, return, tab, f5, or a single "
                                    "character; join combination keys with “+”, modifiers first.")
                         .arg(name);
            return false;
        }
        keys.append(key);
    }

    // Everything but the last key is held down; the last key is tapped. Each
    // key is pressed by its own name (no Qt modifier flags) so the down and
    // up of each key pair up exactly, even when the tapped key is itself a
    // modifier, e.g. ctrl+shift+tab.
    qCDebug(KRDC) << "AI chord down:" << names.join(QLatin1Char('+'));
    for (int i = 0; i + 1 < keys.size(); ++i) {
        view->aiKeyPress(keys.at(i), Qt::NoModifier, true);
    }
    view->aiKeyPress(keys.last(), Qt::NoModifier, true);

    // The final key is released immediately; the held keys are released on a
    // short stagger. A down and its up in the same millisecond are dropped by
    // some remote applications.
    constexpr int StepMs = 15;
    view->aiKeyPress(keys.last(), Qt::NoModifier, false);
    for (int i = keys.size() - 2; i >= 0; --i) {
        const int key = keys.at(i);
        // Release through m_view (a QPointer): if the tab closes before the
        // timer fires, the release is skipped instead of dangling.
        QTimer::singleShot((keys.size() - 1 - i) * StepMs, this, [this, key] {
            if (m_view) {
                m_view->aiKeyPress(key, Qt::NoModifier, false);
            }
        });
    }
    return true;
}

QString AiConversation::historyFilePath() const
{
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/ai");
    const QByteArray digest = QCryptographicHash::hash(m_key.toUtf8(), QCryptographicHash::Sha1).toHex();
    return directory + QStringLiteral("/%1.json").arg(QString::fromLatin1(digest));
}

void AiConversation::load()
{
    QFile file{historyFilePath()};
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        qCWarning(KRDC) << "Could not read AI history" << file.fileName() << parseError.errorString();
        return;
    }

    const QJsonObject root = document.object();
    m_title = root.value(QStringLiteral("title")).toString();

    const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        Entry entry;
        const QString role = object.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("user")) {
            entry.role = Role::User;
        } else if (role == QLatin1String("assistant")) {
            entry.role = Role::Assistant;
        } else if (role == QLatin1String("tool")) {
            entry.role = Role::Tool;
        } else {
            continue;
        }
        entry.text = object.value(QStringLiteral("text")).toString();
        const QString savedTime = object.value(QStringLiteral("time")).toString();
        if (!savedTime.isEmpty()) {
            entry.timestamp = QDateTime::fromString(savedTime, Qt::ISODateWithMs);
        }
        entry.toolCallId = object.value(QStringLiteral("tool_call_id")).toString();
        entry.toolName = object.value(QStringLiteral("tool")).toString();
        const QString image = object.value(QStringLiteral("image")).toString();
        if (!image.isEmpty()) {
            entry.image = QByteArray::fromBase64(image.toLatin1());
        }
        m_entries.append(entry);
    }

    if (!m_entries.isEmpty()) {
        Q_EMIT entriesChanged();
    }
}

void AiConversation::save() const
{
    if (!m_config.rememberHistory) {
        return;
    }

    QJsonArray entries;
    const int first = qMax(0, int(m_entries.size()) - SavedHistoryEntries);
    for (int i = first; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries.at(i);
        if (entry.role == Role::Note) {
            continue;
        }

        QJsonObject object;
        if (entry.role == Role::User) {
            object.insert(QStringLiteral("role"), QStringLiteral("user"));
        } else if (entry.role == Role::Assistant) {
            object.insert(QStringLiteral("role"), QStringLiteral("assistant"));
        } else {
            object.insert(QStringLiteral("role"), QStringLiteral("tool"));
        }
        object.insert(QStringLiteral("text"), entry.text);
        if (entry.timestamp.isValid()) {
            object.insert(QStringLiteral("time"), entry.timestamp.toString(Qt::ISODateWithMs));
        }
        if (!entry.toolCallId.isEmpty()) {
            object.insert(QStringLiteral("tool_call_id"), entry.toolCallId);
        }
        if (!entry.toolName.isEmpty()) {
            object.insert(QStringLiteral("tool"), entry.toolName);
        }
        // Screenshots are not persisted: they are large, go stale instantly,
        // and the model can always take a fresh one.
        entries.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("key"), m_key);
    if (!m_title.isEmpty()) {
        root.insert(QStringLiteral("title"), m_title);
    }
    root.insert(QStringLiteral("entries"), entries);

    QFile file{historyFilePath()};
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(KRDC) << "Could not write AI history" << file.fileName();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
