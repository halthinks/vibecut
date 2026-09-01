/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecutagent.h"

#include "vibecutmodelprovider.h"

bool VibeCutAgent::reloadModelProvider(QString *error)
{
    if (error) error->clear();
    if (busy() || hasPendingPlan()) {
        const QString message = QStringLiteral("Model provider cannot be reloaded while VibeCut is busy or a plan is pending.");
        if (error) *error = message;
        return false;
    }

    QString providerError;
    std::unique_ptr<VibeCutModelProvider> provider = VibeCutModelProviderRegistry::global().createConfigured(&providerError);
    if (!provider) {
        m_provider.reset();
        m_providerError = providerError;
        if (error) *error = providerError;
        Q_EMIT statusChanged(QStringLiteral("No model provider"));
        return false;
    }

    m_provider = std::move(provider);
    m_providerError.clear();
    Q_EMIT statusChanged(QStringLiteral("Ready · %1").arg(m_provider->id()));
    return true;
}
