/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aihttpclient.h"

#include "krdc_debug.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <chrono>

AiHttpClient::AiHttpClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void AiHttpClient::setConfig(const AiConfig &config)
{
    m_config = config;
}

const AiConfig &AiHttpClient::config() const
{
    return m_config;
}

bool AiHttpClient::busy() const
{
    return m_reply && m_reply->isRunning();
}

void AiHttpClient::request(const QJsonArray &messages, const QJsonArray &tools)
{
    if (busy()) {
        Q_EMIT requestFailed(QStringLiteral("A request is already running."));
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), m_config.model);
    payload.insert(QStringLiteral("messages"), messages);
    payload.insert(QStringLiteral("temperature"), m_config.temperature);
    payload.insert(QStringLiteral("max_tokens"), m_config.maxTokens);
    payload.insert(QStringLiteral("stream"), false);
    if (!tools.isEmpty()) {
        payload.insert(QStringLiteral("tools"), tools);
        payload.insert(QStringLiteral("tool_choice"), QStringLiteral("auto"));
    }

    QNetworkRequest request{QUrl{m_config.chatUrl()}};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_config.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_config.apiKey).toUtf8());
    }
    request.setTransferTimeout(std::chrono::seconds(qMax(1, m_config.timeoutSeconds)));

    qCDebug(KRDC) << "AI request to" << m_config.chatUrl() << "model" << m_config.model << "messages" << messages.count();

    m_reply = m_network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &AiHttpClient::onFinished);
}

void AiHttpClient::cancel()
{
    if (m_reply) {
        m_reply->abort();
    }
}

void AiHttpClient::listModels()
{
    if (m_modelsReply && m_modelsReply->isRunning()) {
        return;
    }

    QNetworkRequest request{QUrl{m_config.modelsUrl()}};
    if (!m_config.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_config.apiKey).toUtf8());
    }
    // Discovery should feel instant; do not sit on a dead endpoint for long.
    request.setTransferTimeout(std::chrono::seconds(10));

    qCDebug(KRDC) << "model discovery request to" << m_config.modelsUrl();
    m_modelsReply = m_network->get(request);
    connect(m_modelsReply, &QNetworkReply::finished, this, &AiHttpClient::onModelsFinished);
}

void AiHttpClient::onModelsFinished()
{
    if (!m_modelsReply) {
        return;
    }

    QNetworkReply *reply = m_modelsReply.data();
    m_modelsReply.clear();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT modelsFailed(QStringLiteral("%1 could not be reached (%2).")
                                .arg(m_config.modelsUrl(), reply->errorString()));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
    if (!document.isObject()) {
        Q_EMIT modelsFailed(QStringLiteral("The server did not send a JSON object (%1).").arg(parseError.errorString()));
        return;
    }

    // OpenAI compatible: {"data":[{"id":"model-one",...},...]}
    const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
    QStringList ids;
    for (const QJsonValue &value : data) {
        const QString id = value.toObject().value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }

    if (ids.isEmpty()) {
        Q_EMIT modelsFailed(QStringLiteral("%1 answered, but listed no models.").arg(m_config.modelsUrl()));
        return;
    }

    ids.sort(Qt::CaseInsensitive);
    ids.removeDuplicates();

    qCDebug(KRDC) << "discovered" << ids.count() << "models";
    Q_EMIT modelsReceived(ids);
}

static QString contentToText(const QJsonValue &content)
{
    if (content.isString()) {
        return content.toString();
    }

    // Some servers answer with a list of content parts.
    QString text;
    const QJsonArray parts = content.toArray();
    for (const QJsonValue &part : parts) {
        const QJsonObject object = part.toObject();
        if (object.value(QStringLiteral("type")).toString() == QLatin1String("text")) {
            text += object.value(QStringLiteral("text")).toString();
        }
    }
    return text;
}

static QString extractErrorBody(const QByteArray &body, const QString &fallback)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (document.isObject()) {
        const QJsonObject object = document.object();
        const QJsonObject error = object.value(QStringLiteral("error")).toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            return message;
        }
        const QString detail = object.value(QStringLiteral("detail")).toString();
        if (!detail.isEmpty()) {
            return detail;
        }
    }
    if (body.isEmpty()) {
        return fallback;
    }
    return fallback + QStringLiteral(": ") + QString::fromUtf8(body.left(300));
}

static QList<AiToolCall> parseToolCalls(const QJsonObject &message)
{
    QList<AiToolCall> calls;

    const QJsonArray toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    for (const QJsonValue &value : toolCalls) {
        const QJsonObject object = value.toObject();
        AiToolCall call;
        call.id = object.value(QStringLiteral("id")).toString();
        if (call.id.isEmpty()) {
            call.id = QStringLiteral("call-%1").arg(calls.count());
        }

        const QJsonObject function = object.value(QStringLiteral("function")).toObject();
        call.name = function.value(QStringLiteral("name")).toString();

        const QJsonValue arguments = function.value(QStringLiteral("arguments"));
        if (arguments.isObject()) {
            call.arguments = arguments.toObject();
        } else if (arguments.isString() && !arguments.toString().trimmed().isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(arguments.toString().toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                call.arguments = document.object();
            } else {
                call.error = QStringLiteral("The arguments for %1 were not valid JSON (%2).")
                                 .arg(call.name, parseError.errorString());
            }
        }

        if (call.error.isEmpty() && !AiTools::isKnownTool(call.name)) {
            call.error = QStringLiteral("Unknown tool “%1”.").arg(call.name);
        }

        calls.append(call);
    }

    return calls;
}

void AiHttpClient::onFinished()
{
    if (!m_reply) {
        return;
    }

    QNetworkReply *reply = m_reply.data();
    m_reply.clear();
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        qCDebug(KRDC) << "AI request cancelled";
        return;
    }

    if (reply->error() != QNetworkReply::NoError && status == 0) {
        Q_EMIT requestFailed(QStringLiteral("Could not reach %1 (%2).").arg(m_config.chatUrl(), reply->errorString()));
        return;
    }

    if (reply->error() == QNetworkReply::TimeoutError) {
        Q_EMIT requestFailed(QStringLiteral("%1 did not answer within %2 s.")
                                 .arg(m_config.endpoint)
                                 .arg(m_config.timeoutSeconds));
        return;
    }

    if (status != 0 && (status < 200 || status >= 300)) {
        Q_EMIT requestFailed(QStringLiteral("HTTP %1 from %2").arg(status).arg(extractErrorBody(body, reply->errorString())));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (!document.isObject()) {
        Q_EMIT requestFailed(QStringLiteral("The server did not send a JSON object (%1).").arg(parseError.errorString()));
        return;
    }

    const QJsonArray choices = document.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        Q_EMIT requestFailed(extractErrorBody(body, QStringLiteral("The answer contained no choices.")));
        return;
    }

    const QJsonObject choice = choices.first().toObject();
    const QJsonObject message = choice.value(QStringLiteral("message")).toObject();
    const QString content = contentToText(message.value(QStringLiteral("content")));
    const QList<AiToolCall> toolCalls = parseToolCalls(message);

    if (content.isEmpty() && toolCalls.isEmpty()) {
        const QString finish = choice.value(QStringLiteral("finish_reason")).toString();
        if (finish == QLatin1String("length")) {
            Q_EMIT requestFailed(QStringLiteral("The answer was cut off at the token limit. Increase the answer length in the "
                                                "assistant settings."));
        } else {
            Q_EMIT requestFailed(QStringLiteral("The model answered with an empty message."));
        }
        return;
    }

    qCDebug(KRDC) << "AI reply:" << content.left(80) << "tool calls:" << toolCalls.count();
    Q_EMIT replyReceived(content, toolCalls);
}
