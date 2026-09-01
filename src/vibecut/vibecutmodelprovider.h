/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <memory>

struct VibeCutModelRequest {
    QUrl endpoint;
    QHash<QByteArray, QByteArray> headers;
    QJsonObject body;
};

/** Provider boundary for the agent runtime.
 *
 * Tool schemas use VibeCut's canonical {name,description,input_schema} shape.
 * Providers translate request formats as needed and expose a normalization
 * hook for provider-specific streaming events.
 */
class VibeCutModelProvider
{
public:
    virtual ~VibeCutModelProvider() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual bool configured(QString *error = nullptr) const = 0;
    virtual VibeCutModelRequest buildRequest(const QString &systemPrompt, const QJsonArray &tools,
                                             const QJsonArray &messages, int maxTokens) const = 0;
    virtual QJsonObject normalizeStreamEvent(const QByteArray &data) = 0;
};

class VibeCutAnthropicProvider : public VibeCutModelProvider
{
public:
    VibeCutAnthropicProvider();
    QString id() const override;
    QString displayName() const override;
    bool configured(QString *error = nullptr) const override;
    VibeCutModelRequest buildRequest(const QString &systemPrompt, const QJsonArray &tools,
                                     const QJsonArray &messages, int maxTokens) const override;
    QJsonObject normalizeStreamEvent(const QByteArray &data) override;

private:
    QString m_apiKey;
    QString m_model;
};

class VibeCutModelProviderRegistry
{
public:
    typedef std::function<std::unique_ptr<VibeCutModelProvider>()> Factory;

    bool registerProvider(const QString &id, const Factory &factory, QString *error = nullptr);
    QStringList providerIds() const;
    std::unique_ptr<VibeCutModelProvider> create(const QString &id, QString *error = nullptr) const;
    std::unique_ptr<VibeCutModelProvider> createConfigured(QString *error = nullptr) const;

    /** Process-wide registry. External integrations register provider factories
     * here before constructing VibeCutAgent; built-ins are installed once. */
    static VibeCutModelProviderRegistry &global();
    /** Compatibility/value snapshot used by existing construction/tests. It
     * includes providers registered in the global registry at call time. */
    static VibeCutModelProviderRegistry builtIns();

private:
    void ensureBuiltIns();
    QHash<QString, Factory> m_factories;
    bool m_builtInsReady = false;
};
