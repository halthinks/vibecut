/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecuthooks.h"

VibeCutHooks::VibeCutHooks(QObject *parent)
    : QObject(parent)
{
}

void VibeCutHooks::publish(const QString &eventName, const QJsonObject &payload)
{
    if (!eventName.trimmed().isEmpty()) {
        Q_EMIT eventEmitted(eventName, payload);
    }
}

bool VibeCutHooks::registerContextProvider(const QString &name, const ContextProvider &provider, QString *error)
{
    if (error) {
        error->clear();
    }
    const QString key = name.trimmed();
    if (key.isEmpty() || !provider) {
        if (error) {
            *error = QStringLiteral("Context hook requires a non-empty name and provider.");
        }
        return false;
    }
    if (m_contextProviders.contains(key)) {
        if (error) {
            *error = QStringLiteral("Context hook '%1' is already registered.").arg(key);
        }
        return false;
    }
    m_contextProviders.insert(key, provider);
    return true;
}

bool VibeCutHooks::unregisterContextProvider(const QString &name)
{
    return m_contextProviders.remove(name.trimmed()) > 0;
}

QStringList VibeCutHooks::contextProviderNames() const
{
    QStringList names = m_contextProviders.keys();
    names.sort();
    return names;
}

QJsonObject VibeCutHooks::collectContext() const
{
    QJsonObject context;
    for (const QString &name : contextProviderNames()) {
        const auto it = m_contextProviders.constFind(name);
        if (it != m_contextProviders.constEnd() && it.value()) {
            context.insert(name, it.value()());
        }
    }
    return context;
}
