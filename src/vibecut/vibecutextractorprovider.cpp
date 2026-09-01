/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutextractorprovider.h"

#include <algorithm>

bool VibeCutExtractorProviderRegistry::registerProvider(const QString &id, const Factory &factory, QString *error)
{
    if (error) error->clear();
    const QString key = id.trimmed();
    if (key.isEmpty()) {
        if (error) *error = QStringLiteral("Extractor provider id must not be empty.");
        return false;
    }
    if (!factory) {
        if (error) *error = QStringLiteral("Extractor provider '%1' requires a factory.").arg(key);
        return false;
    }
    if (m_factories.contains(key)) {
        if (error) *error = QStringLiteral("Extractor provider '%1' is already registered.").arg(key);
        return false;
    }
    m_factories.insert(key, factory);
    return true;
}

QStringList VibeCutExtractorProviderRegistry::providerIds() const
{
    QStringList ids = m_factories.keys();
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return a.toLower() < b.toLower(); });
    return ids;
}

std::unique_ptr<VibeCutExtractorProvider> VibeCutExtractorProviderRegistry::create(const QString &id, QString *error) const
{
    if (error) error->clear();
    const auto it = m_factories.constFind(id.trimmed());
    if (it == m_factories.constEnd()) {
        if (error) *error = QStringLiteral("Unknown extractor provider '%1'.").arg(id);
        return nullptr;
    }
    std::unique_ptr<VibeCutExtractorProvider> provider = it.value()();
    if (!provider) {
        if (error) *error = QStringLiteral("Extractor provider factory '%1' returned null.").arg(id);
        return nullptr;
    }
    return provider;
}

QStringList VibeCutExtractorProviderRegistry::providerIdsForCapability(const QString &capability) const
{
    const QString wanted = capability.trimmed().toLower();
    QStringList ids;
    if (wanted.isEmpty()) return ids;
    for (const QString &id : providerIds()) {
        QString error;
        std::unique_ptr<VibeCutExtractorProvider> provider = create(id, &error);
        if (!provider) continue;
        QStringList caps = provider->capabilities();
        for (QString &cap : caps) cap = cap.trimmed().toLower();
        if (caps.contains(wanted)) ids.append(id);
    }
    return ids;
}

VibeCutExtractorProviderRegistry &VibeCutExtractorProviderRegistry::global()
{
    static VibeCutExtractorProviderRegistry registry;
    return registry;
}
