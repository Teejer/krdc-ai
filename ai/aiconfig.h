/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>

/**
 * Settings for the AI assistant chat panel.
 *
 * Stored in the "AiAssistant" group of krdcrc. The API key is kept in the
 * plain config file, which is fine for the default local inference server that
 * needs no key at all; the settings dialog says so when a key is entered.
 */
struct AiConfig {
    /** Base URL of an OpenAI-compatible server, without the /chat/completions suffix. */
    QString endpoint = QStringLiteral("http://localhost:8080/v1");
    QString model = QStringLiteral("default");
    QString apiKey;
    QString systemPrompt;

    /** Run non-destructive actions without asking. Destructive ones always ask. */
    bool autoExecute = false;
    /** Include a screenshot of the remote machine with every question. */
    bool attachScreenshot = true;
    /** Keep the conversation of a connection across restarts. */
    bool rememberHistory = true;

    int maxTokens = 1024;
    double temperature = 0.3;

    /** Screenshots are scaled down so their longest side is at most this many pixels. */
    int imageMaxSize = 1280;
    int imageQuality = 80;

    /** Seconds to wait for a response before giving up. */
    int timeoutSeconds = 120;

    static AiConfig load();
    void save() const;

    /** @return the full URL of the chat completions endpoint. */
    QString chatUrl() const;

    /**
     * @return the URL of the model list endpoint derived from the server URL,
     * e.g. http://localhost:11434/v1/models. Used for model discovery.
     */
    QString modelsUrl() const;

    /** @return the prompt used when the user has not written one. */
    static QString defaultSystemPrompt();
};
