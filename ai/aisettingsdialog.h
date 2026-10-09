/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "aiconfig.h"

#include <QDialog>

class QCheckBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QComboBox;

/**
 * Configuration for the AI assistant: endpoint, model, key and behaviour.
 */
class AiSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AiSettingsDialog(QWidget *parent = nullptr);

    AiConfig config() const;
    void setConfig(const AiConfig &config);

private Q_SLOTS:
    void testConnection();
    void discoverModels();

private:
    AiConfig m_config;

    QLineEdit *m_endpoint = nullptr;
    QComboBox *m_model = nullptr;
    QPushButton *m_discoverButton = nullptr;
    QLabel *m_discoverResult = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QPlainTextEdit *m_systemPrompt = nullptr;
    QCheckBox *m_autoExecute = nullptr;
    QCheckBox *m_attachScreenshot = nullptr;
    QCheckBox *m_rememberHistory = nullptr;
    QLineEdit *m_screenshotPath = nullptr;
    QSpinBox *m_maxTokens = nullptr;
    QDoubleSpinBox *m_temperature = nullptr;
    QSpinBox *m_timeout = nullptr;
    QPushButton *m_testButton = nullptr;
    QLabel *m_testResult = nullptr;
};
