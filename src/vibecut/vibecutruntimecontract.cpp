/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "vibecuttoolsurface.h"

#include <QJsonArray>
#include <QJsonObject>

QJsonObject VibeCutToolSurface::runtimeContractSnapshot() const
{
    const QJsonArray advertisedSchemas = schemas();
    const QHash<QString, VibeCutToolPolicy> effectivePolicies = policies();

    QJsonArray tools;
    for (const QJsonValue &value : advertisedSchemas) {
        if (!value.isObject()) continue;
        const QJsonObject schema = value.toObject();
        const QString name = schema.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        const auto policy = effectivePolicies.constFind(name);
        if (policy == effectivePolicies.constEnd() || !policy.value().enabled) continue;
        tools.append(QJsonObject{{QStringLiteral("schema"), schema},
                                 {QStringLiteral("policy"), policy.value().toJson()}});
    }

    return QJsonObject{{QStringLiteral("protocol_version"), 1},
                       {QStringLiteral("editor_id"), QStringLiteral("kdenlive")},
                       {QStringLiteral("adapter_id"), QStringLiteral("halthinks-vibecut-adapter")},
                       {QStringLiteral("project_revision"), static_cast<qint64>(projectRevision())},
                       {QStringLiteral("tool_count"), tools.size()},
                       {QStringLiteral("tools"), tools}};
}
