/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecutmodelprovider.h"

#include <QJsonDocument>

VibeCutAnthropicProvider::VibeCutAnthropicProvider()
    : m_apiKey(qEnvironmentVariable("ANTHROPIC_API_KEY").trimmed())
    , m_model(qEnvironmentVariable("VIBECUT_MODEL", QStringLiteral("claude-sonnet-5")).trimmed())
{
}

QString VibeCutAnthropicProvider::id() const { return QStringLiteral("anthropic"); }
QString VibeCutAnthropicProvider::displayName() const { return QStringLiteral("Anthropic"); }

bool VibeCutAnthropicProvider::configured(QString *error) const
{
    if (error) error->clear();
    if (m_apiKey.isEmpty()) {
        if (error) *error = QStringLiteral("ANTHROPIC_API_KEY is not set in the environment.");
        return false;
    }
    if (m_model.isEmpty()) {
        if (error) *error = QStringLiteral("VibeCut model name is empty.");
        return false;
    }
    return true;
}

VibeCutModelRequest VibeCutAnthropicProvider::buildRequest(const QString &systemPrompt, const QJsonArray &tools,
                                                            const QJsonArray &messages, int maxTokens) const
{
    VibeCutModelRequest request;
    request.endpoint = QUrl(QStringLiteral("https://api.anthropic.com/v1/messages"));
    request.headers.insert(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json"));
    request.headers.insert(QByteArrayLiteral("x-api-key"), m_apiKey.toUtf8());
    request.headers.insert(QByteArrayLiteral("anthropic-version"), QByteArrayLiteral("2023-06-01"));
    const QJsonObject systemBlock{{QStringLiteral("type"), QStringLiteral("text")},
                                  {QStringLiteral("text"), systemPrompt},
                                  {QStringLiteral("cache_control"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}}}};
    request.body = QJsonObject{{QStringLiteral("model"), m_model},
                               {QStringLiteral("max_tokens"), maxTokens},
                               {QStringLiteral("stream"), true},
                               {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("adaptive")}}},
                               {QStringLiteral("system"), QJsonArray{systemBlock}},
                               {QStringLiteral("tools"), tools},
                               {QStringLiteral("messages"), messages}};
    return request;
}

QJsonObject VibeCutAnthropicProvider::normalizeStreamEvent(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).object();
}

bool VibeCutModelProviderRegistry::registerProvider(const QString &id, const Factory &factory, QString *error)
{
    if (error) error->clear();
    const QString key = id.trimmed().toLower();
    if (key.isEmpty() || !factory) {
        if (error) *error = QStringLiteral("Provider registration requires an id and factory.");
        return false;
    }
    if (m_factories.contains(key)) {
        if (error) *error = QStringLiteral("Model provider '%1' is already registered.").arg(key);
        return false;
    }
    m_factories.insert(key, factory);
    return true;
}

QStringList VibeCutModelProviderRegistry::providerIds() const
{
    QStringList ids = m_factories.keys();
    ids.sort();
    return ids;
}

std::unique_ptr<VibeCutModelProvider> VibeCutModelProviderRegistry::create(const QString &id, QString *error) const
{
    if (error) error->clear();
    const QString key = id.trimmed().toLower();
    const auto factory = m_factories.constFind(key);
    if (factory == m_factories.constEnd()) {
        if (error) *error = QStringLiteral("Unknown VibeCut model provider '%1'. Registered providers: %2")
                                .arg(key, providerIds().join(QStringLiteral(", ")));
        return std::unique_ptr<VibeCutModelProvider>();
    }
    return factory.value()();
}

std::unique_ptr<VibeCutModelProvider> VibeCutModelProviderRegistry::createConfigured(QString *error) const
{
    const QString requested = qEnvironmentVariable("VIBECUT_PROVIDER", QStringLiteral("anthropic"));
    std::unique_ptr<VibeCutModelProvider> provider = create(requested, error);
    if (!provider) return provider;
    QString configurationError;
    if (!provider->configured(&configurationError)) {
        if (error) *error = configurationError;
        return std::unique_ptr<VibeCutModelProvider>();
    }
    return provider;
}

void VibeCutModelProviderRegistry::ensureBuiltIns()
{
    if (m_builtInsReady) return;
    m_builtInsReady = true;
    QString ignored;
    registerProvider(QStringLiteral("anthropic"), []() {
        return std::unique_ptr<VibeCutModelProvider>(new VibeCutAnthropicProvider());
    }, &ignored);
}

VibeCutModelProviderRegistry &VibeCutModelProviderRegistry::global()
{
    static VibeCutModelProviderRegistry registry;
    registry.ensureBuiltIns();
    return registry;
}
