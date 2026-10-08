/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "aiconfig.h"
#include "aitools.h"

#include <QList>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Talks to one OpenAI compatible /chat/completions endpoint.
 *
 * A request is either in flight or not; the assistant only ever has one
 * request per conversation outstanding.
 */
class AiHttpClient : public QObject
{
    Q_OBJECT

public:
    explicit AiHttpClient(QObject *parent = nullptr);

    void setConfig(const AiConfig &config);
    const AiConfig &config() const;

    bool busy() const;

    /**
     * Sends @p messages to the model, offering @p tools.
     * Emits either replyReceived() or requestFailed().
     */
    void request(const QJsonArray &messages, const QJsonArray &tools);
    void cancel();

    /**
     * Asks the server's /models endpoint which models it is serving.
     * Independent of the chat request state, so it can run while a
     * conversation is idle or busy. Emits modelsReceived() or modelsFailed().
     */
    void listModels();

Q_SIGNALS:
    void replyReceived(const QString &content, const QList<AiToolCall> &toolCalls);
    void requestFailed(const QString &message);
    void modelsReceived(const QStringList &modelIds);
    void modelsFailed(const QString &message);

private:
    void onFinished();
    void onModelsFinished();

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_modelsReply;
    AiConfig m_config;
};
