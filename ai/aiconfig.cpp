/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aiconfig.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QChar>
#include <QDir>
#include <QStandardPaths>

QString AiConfig::defaultScreenshotPath()
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) {
        pictures = QDir::homePath();
    }
    return pictures + QStringLiteral("/krdc-ai");
}

AiConfig AiConfig::load()
{
    AiConfig config;
    const KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("AiAssistant"));

    config.endpoint = group.readEntry(QStringLiteral("Endpoint"), config.endpoint);
    config.model = group.readEntry(QStringLiteral("Model"), config.model);
    config.apiKey = group.readEntry(QStringLiteral("ApiKey"), config.apiKey);
    config.systemPrompt = group.readEntry(QStringLiteral("SystemPrompt"), config.systemPrompt);
    config.autoExecute = group.readEntry(QStringLiteral("AutoExecute"), config.autoExecute);
    config.attachScreenshot = group.readEntry(QStringLiteral("AttachScreenshot"), config.attachScreenshot);
    config.rememberHistory = group.readEntry(QStringLiteral("RememberHistory"), config.rememberHistory);
    config.maxTokens = group.readEntry(QStringLiteral("MaxTokens"), config.maxTokens);
    config.temperature = group.readEntry(QStringLiteral("Temperature"), config.temperature);
    config.imageMaxSize = group.readEntry(QStringLiteral("ImageMaxSize"), config.imageMaxSize);
    config.imageQuality = group.readEntry(QStringLiteral("ImageQuality"), config.imageQuality);
    config.timeoutSeconds = group.readEntry(QStringLiteral("TimeoutSeconds"), config.timeoutSeconds);
    config.screenshotPath = group.readEntry(QStringLiteral("ScreenshotPath"), AiConfig{}.screenshotPath);

    return config;
}

void AiConfig::save() const
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("AiAssistant"));

    group.writeEntry(QStringLiteral("Endpoint"), endpoint);
    group.writeEntry(QStringLiteral("Model"), model);
    group.writeEntry(QStringLiteral("ApiKey"), apiKey);
    group.writeEntry(QStringLiteral("SystemPrompt"), systemPrompt);
    group.writeEntry(QStringLiteral("AutoExecute"), autoExecute);
    group.writeEntry(QStringLiteral("AttachScreenshot"), attachScreenshot);
    group.writeEntry(QStringLiteral("RememberHistory"), rememberHistory);
    group.writeEntry(QStringLiteral("MaxTokens"), maxTokens);
    group.writeEntry(QStringLiteral("Temperature"), temperature);
    group.writeEntry(QStringLiteral("ImageMaxSize"), imageMaxSize);
    group.writeEntry(QStringLiteral("ImageQuality"), imageQuality);
    group.writeEntry(QStringLiteral("TimeoutSeconds"), timeoutSeconds);
    group.writeEntry(QStringLiteral("ScreenshotPath"), screenshotPath);

    group.config()->sync();
}

QString AiConfig::chatUrl() const
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    if (base.endsWith(QStringLiteral("/chat/completions"))) {
        return base;
    }
    return base + QStringLiteral("/chat/completions");
}

QString AiConfig::modelsUrl() const
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    if (base.endsWith(QStringLiteral("/chat/completions"))) {
        base.chop(QStringLiteral("/chat/completions").size());
    } else if (base.endsWith(QStringLiteral("/models"))) {
        base.chop(QStringLiteral("/models").size());
    }
    return base + QStringLiteral("/models");
}

QString AiConfig::defaultSystemPrompt()
{
    QString prompt = QStringLiteral(
        "You are an assistant embedded in KRDC, a remote desktop client. You are looking at the "
        "screen of a remote machine that the user is connected to, usually over RDP.\n\n"
        "Rules:\n"
        "- You see the remote screen only through the take_screenshot tool. Screenshots are not "
        "kept up to date on their own, so call take_screenshot again after anything changes.\n"
        "- All coordinates you use are pixels of the screenshot you last received: (0,0) is the "
        "top left of that image. If the reported image size differs from the screen size, scale "
        "the coordinates you read off the image accordingly.\n"
        "- Prefer small, verifiable steps. Act, then take a screenshot to see what happened "
        "before acting again.\n"
        "- For keyboard shortcuts always use the hotkey tool with the combination as one string, modifiers first: \"ctrl+a\", "
        "\"win+e\", \"alt+tab\", \"ctrl+shift+esc\". Never press the modifier and the key separately, never type a shortcut "
        "with type_text. Use press_key only for single keys like return, escape or tab.\n"
        "- When writing a guide or documenting a procedure, capture each key step with save_screenshot using short "
        "descriptive file names, then quote the paths it returns in the write-up.\n"
        "- Never guess a coordinate you cannot see; take a screenshot first.\n"
        "- On every click or drag, set the target field to a few words naming what you are clicking, e.g. \"the OK button\"; "
        "the user approves the action from that and a zoomed crop of the spot.\n"
        "- If an action needs a password or would delete or overwrite something, stop and let "
        "the user do it or confirm it.\n"
        "- Keep answers short. Report what you did and what the screen now shows.");
    if (prompt.contains(QLatin1String("If the reported image size differs"))) {
        // The old wording made the model rescale coordinates itself, and
        // models are bad at that arithmetic: clicks landed off target by the
        // downscale factor. The point tools map screenshot pixels to screen
        // pixels now, so the model must not.
        return prompt.replace(
            QLatin1String("If the reported image size differs from the screen size, scale the coordinates you read off the "
                          "image accordingly."),
            QStringLiteral("Point tools take coordinates in this image's pixels and are mapped to the real screen "
                           "for you; do not scale coordinates yourself."));
    }
    return prompt;
}
