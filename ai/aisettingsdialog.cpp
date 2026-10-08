/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aisettingsdialog.h"

#include "aihttpclient.h"
#include "aitext.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

AiSettingsDialog::AiSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18nc("@title:window", "AI Assistant Settings"));

    auto *layout = new QVBoxLayout(this);

    // No scroll area: a scrolling settings dialog was the complaint. Instead
    // the settings are grouped, the rarely changed ones live in a collapsed
    // Advanced section, and the result labels only take up space once they
    // have something to say. The dialog then fits its content at a height any
    // screen can show.

    // --- Connection ---
    auto *connectionGroup = new QGroupBox(i18n("Connection"), this);
    auto *connectionForm = new QFormLayout(connectionGroup);
    connectionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addWidget(connectionGroup);

    m_endpoint = new QLineEdit(connectionGroup);
    m_endpoint->setPlaceholderText(QStringLiteral("http://localhost:11434/v1"));
    connectionForm->addRow(i18n("Server &URL:"), m_endpoint);

    m_model = new QComboBox(connectionGroup);
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_model->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(m_model, 1);
    m_discoverButton = new QPushButton(i18n("&Discover…"), connectionGroup);
    m_discoverButton->setToolTip(i18n("Ask the server which models it is serving"));
    modelRow->addWidget(m_discoverButton);
    connectionForm->addRow(i18n("&Model:"), modelRow);

    m_apiKey = new QLineEdit(connectionGroup);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(i18n("(not needed for a local server)"));
    connectionForm->addRow(i18n("&API key:"), m_apiKey);

    m_discoverResult = new QLabel(connectionGroup);
    m_discoverResult->setWordWrap(true);
    m_discoverResult->setStyleSheet(QStringLiteral("color:palette(mid);"));
    // Hidden until discovery reports something, so it reserves no height and
    // the dialog does not grow or clip around an empty label.
    m_discoverResult->setVisible(false);
    connectionForm->addRow(m_discoverResult);

    // --- Behaviour ---
    auto *behaviourGroup = new QGroupBox(i18n("Behaviour"), this);
    auto *behaviourLayout = new QVBoxLayout(behaviourGroup);
    layout->addWidget(behaviourGroup);

    m_autoExecute = new QCheckBox(i18n("Run clicks and typing &without asking first"), behaviourGroup);
    // The caveat about destructive actions is a tooltip and What's-This rather
    // than an inline paragraph: a permanently visible wrapped label is what
    // pushed this dialog past the height of the screen.
    const QString riskNote = i18n("Destructive actions (deleting files, formatting, shutting down, "
                                  "and key combinations such as Ctrl+Alt+Del) always ask for approval, "
                                  "even when this is turned on.");
    m_autoExecute->setToolTip(riskNote);
    m_autoExecute->setWhatsThis(riskNote);
    behaviourLayout->addWidget(m_autoExecute);

    m_attachScreenshot = new QCheckBox(i18n("Atta&ch a screenshot of the remote screen to each question"), behaviourGroup);
    behaviourLayout->addWidget(m_attachScreenshot);

    m_rememberHistory = new QCheckBox(i18n("&Remember each connection's chat history between restarts"), behaviourGroup);
    behaviourLayout->addWidget(m_rememberHistory);

    // --- Advanced, collapsed unless the user opens it ---
    auto *advancedToggle = new QCheckBox(i18n("&Show advanced settings"), this);
    layout->addWidget(advancedToggle);

    // A layout cannot be hidden, so the advanced fields live in a container
    // widget whose visibility the toggle flips.
    auto *advancedWidget = new QWidget(this);
    auto *advancedForm = new QFormLayout(advancedWidget);
    advancedForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    advancedWidget->setVisible(false);
    layout->addWidget(advancedWidget);

    m_maxTokens = new QSpinBox(advancedWidget);
    m_maxTokens->setRange(128, 32768);
    m_maxTokens->setSingleStep(128);
    advancedForm->addRow(i18n("Answer &length limit:"), m_maxTokens);

    m_temperature = new QDoubleSpinBox(advancedWidget);
    m_temperature->setRange(0.0, 2.0);
    m_temperature->setSingleStep(0.1);
    m_temperature->setDecimals(1);
    advancedForm->addRow(i18n("&Temperature:"), m_temperature);

    m_timeout = new QSpinBox(advancedWidget);
    m_timeout->setRange(5, 600);
    m_timeout->setSuffix(i18n(" s"));
    advancedForm->addRow(i18n("Request &timeout:"), m_timeout);

    m_systemPrompt = new QPlainTextEdit(advancedWidget);
    m_systemPrompt->setPlaceholderText(i18n("(default instructions)"));
    m_systemPrompt->setFixedHeight(100);
    advancedForm->addRow(i18n("&Custom instructions:"), m_systemPrompt);

    auto *testRow = new QHBoxLayout;
    m_testButton = new QPushButton(i18n("&Test connection"), this);
    m_testResult = new QLabel(this);
    m_testResult->setWordWrap(true);
    m_testResult->setVisible(false);
    testRow->addWidget(m_testButton);
    testRow->addWidget(m_testResult, 1);
    layout->addLayout(testRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_testButton, &QPushButton::clicked, this, &AiSettingsDialog::testConnection);
    connect(m_discoverButton, &QPushButton::clicked, this, &AiSettingsDialog::discoverModels);
    connect(advancedToggle, &QCheckBox::toggled, this, [this, advancedWidget, advancedToggle](bool shown) {
        advancedWidget->setVisible(shown);
        advancedToggle->setText(shown ? i18n("&Hide advanced settings") : i18n("&Show advanced settings"));
        // Let the dialog grow or shrink to suit rather than leaving a gap.
        adjustSize();
    });

    setMinimumWidth(480);
    // No fixed height: the dialog takes the height its visible content needs,
    // so nothing is clipped and no scrollbar is involved.
}

AiConfig AiSettingsDialog::config() const
{
    AiConfig config = m_config;
    config.endpoint = m_endpoint->text().trimmed();
    if (config.endpoint.isEmpty()) {
        config.endpoint = AiConfig{}.endpoint;
    }
    config.model = m_model->currentText().trimmed();
    if (config.model.isEmpty()) {
        config.model = QStringLiteral("default");
    }
    config.apiKey = m_apiKey->text();
    config.systemPrompt = m_systemPrompt->toPlainText().trimmed();
    config.autoExecute = m_autoExecute->isChecked();
    config.attachScreenshot = m_attachScreenshot->isChecked();
    config.rememberHistory = m_rememberHistory->isChecked();
    config.maxTokens = m_maxTokens->value();
    config.temperature = m_temperature->value();
    config.timeoutSeconds = m_timeout->value();
    return config;
}

void AiSettingsDialog::setConfig(const AiConfig &config)
{
    m_config = config;
    m_endpoint->setText(config.endpoint);
    m_model->setCurrentText(config.model);
    m_apiKey->setText(config.apiKey);
    m_systemPrompt->setPlainText(config.systemPrompt);
    m_autoExecute->setChecked(config.autoExecute);
    m_attachScreenshot->setChecked(config.attachScreenshot);
    m_rememberHistory->setChecked(config.rememberHistory);
    m_maxTokens->setValue(config.maxTokens);
    m_temperature->setValue(config.temperature);
    m_timeout->setValue(config.timeoutSeconds);
}

void AiSettingsDialog::discoverModels()
{
    // Use the endpoint currently in the dialog, so the user can point at a
    // different server and discover its models without saving first.
    AiConfig candidate = config();

    auto *client = new AiHttpClient(this);
    client->setConfig(candidate);

    m_discoverButton->setEnabled(false);
    // The url is user-entered text: keep it out of the translated template
    // (see aitext.h) so percent-escapes in it are not read as placeholders.
    m_discoverResult->setText(AiText::after(i18n("Asking for models from"), candidate.modelsUrl() + QStringLiteral("…")));
    m_discoverResult->setVisible(true);

    connect(client, &AiHttpClient::modelsReceived, this, [this, client](const QStringList &models) {
        m_discoverButton->setEnabled(true);

        const QString current = m_model->currentText();
        m_model->blockSignals(true);
        m_model->clear();
        m_model->addItems(models);
        m_model->setCurrentText(current);
        m_model->blockSignals(false);

        if (!models.contains(current) && !current.isEmpty()) {
            // Note the two-argument i18n form: i18n("...%1...") alone is
            // finalised immediately and a later QString::arg() on it leaves a
            // "(I18N_ARGUMENT_MISSING)" marker behind (see aitext.h). The
            // model id is untrusted text, so it is appended, not inserted.
            m_discoverResult->setText(AiText::quotedAfter(i18n("%1 model(s) found; the model currently set is not among them:",
                                                               models.count()),
                                                          current));
        } else {
            m_discoverResult->setText(i18n("%1 model(s) found.", models.count()));
        }
        adjustSize();
        client->deleteLater();
    });
    connect(client, &AiHttpClient::modelsFailed, this, [this, client](const QString &message) {
        m_discoverButton->setEnabled(true);
        m_discoverResult->setText(message);
        adjustSize();
        client->deleteLater();
    });

    client->listModels();
}

void AiSettingsDialog::testConnection()
{
    // Send one trivial message with the values currently in the dialog, not
    // the stored ones, so the test reflects what the user just typed.
    AiConfig candidate = config();
    candidate.timeoutSeconds = qMin(candidate.timeoutSeconds, 20);

    auto *client = new AiHttpClient(this);
    client->setConfig(candidate);

    m_testButton->setEnabled(false);
    m_testResult->setText(AiText::after(i18n("Contacting"), candidate.chatUrl() + QStringLiteral("…")));
    m_testResult->setVisible(true);

    QJsonArray messages;
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), QStringLiteral("Reply with exactly: ok"));
    messages.append(message);

    connect(client, &AiHttpClient::replyReceived, this, [this, client](const QString &content, const QList<AiToolCall> &) {
        m_testButton->setEnabled(true);
        // The answer is arbitrary model output; if it contains something like
        // "%1" it must not end up inside the translated template (aitext.h).
        m_testResult->setText(AiText::quotedAfter(i18n("Working. The model answered:"), content.trimmed().left(60)));
        adjustSize();
        client->deleteLater();
    });
    connect(client, &AiHttpClient::requestFailed, this, [this, client](const QString &message) {
        m_testButton->setEnabled(true);
        m_testResult->setText(message);
        adjustSize();
        client->deleteLater();
    });

    client->request(messages, QJsonArray{});
}
