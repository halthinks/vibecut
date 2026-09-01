/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

/** Stable extension hooks for VibeCut integrations.
 *
 * Modules can subscribe to lifecycle events without coupling to the dock,
 * model provider, or Kdenlive implementation details. Context providers let
 * optional subsystems contribute bounded structured context on demand.
 */
class VibeCutHooks : public QObject
{
    Q_OBJECT
public:
    typedef std::function<QJsonObject()> ContextProvider;

    explicit VibeCutHooks(QObject *parent = nullptr);

    void publish(const QString &eventName, const QJsonObject &payload = QJsonObject());
    bool registerContextProvider(const QString &name, const ContextProvider &provider, QString *error = nullptr);
    bool unregisterContextProvider(const QString &name);
    QStringList contextProviderNames() const;
    QJsonObject collectContext() const;

Q_SIGNALS:
    void eventEmitted(const QString &eventName, const QJsonObject &payload);

private:
    QHash<QString, ContextProvider> m_contextProviders;
};
