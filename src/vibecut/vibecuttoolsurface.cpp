/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecuttoolsurface.h"

#include "vibecuttools.h"

namespace {
QJsonObject errorResult(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
} // namespace

VibeCutToolSurface::VibeCutToolSurface(VibeCutTools *baseTools)
    : m_baseTools(baseTools)
{
}

bool VibeCutToolSurface::baseContains(const QString &name) const
{
    if (!m_baseTools) {
        return false;
    }
    const QJsonArray baseSchemas = m_baseTools->schemas();
    for (const QJsonValue &value : baseSchemas) {
        if (value.toObject().value(QStringLiteral("name")).toString() == name) {
            return true;
        }
    }
    return false;
}

bool VibeCutToolSurface::registerTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (error) {
        error->clear();
    }

    const QString name = schema.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) {
            *error = QStringLiteral("tool schema requires a non-empty name");
        }
        return false;
    }
    if (policy.name != name) {
        if (error) {
            *error = QStringLiteral("tool policy name '%1' does not match schema name '%2'").arg(policy.name, name);
        }
        return false;
    }
    if (!schema.value(QStringLiteral("input_schema")).isObject()) {
        if (error) {
            *error = QStringLiteral("tool '%1' requires an input_schema object").arg(name);
        }
        return false;
    }
    if (!handler) {
        if (error) {
            *error = QStringLiteral("tool '%1' requires a handler").arg(name);
        }
        return false;
    }
    if (m_extensions.contains(name) || baseContains(name)) {
        if (error) {
            *error = QStringLiteral("tool '%1' is already registered").arg(name);
        }
        return false;
    }

    Extension extension;
    extension.schema = schema;
    extension.policy = policy;
    extension.handler = handler;
    m_extensions.insert(name, extension);
    m_extensionOrder.append(name);
    return true;
}

QJsonArray VibeCutToolSurface::schemas() const
{
    QJsonArray result = m_baseTools ? m_baseTools->schemas() : QJsonArray();
    for (const QString &name : m_extensionOrder) {
        result.append(m_extensions.value(name).schema);
    }
    return result;
}

QHash<QString, VibeCutToolPolicy> VibeCutToolSurface::policies() const
{
    QHash<QString, VibeCutToolPolicy> result = m_baseTools ? m_baseTools->policies() : QHash<QString, VibeCutToolPolicy>();
    for (const QString &name : m_extensionOrder) {
        result.insert(name, m_extensions.value(name).policy);
    }
    return result;
}

QJsonObject VibeCutToolSurface::invoke(const QString &name, const QJsonObject &input) const
{
    const auto extension = m_extensions.constFind(name);
    if (extension != m_extensions.constEnd()) {
        return extension.value().handler(input);
    }
    if (m_baseTools) {
        return m_baseTools->invoke(name, input);
    }
    return errorResult(QStringLiteral("Unknown tool: %1").arg(name));
}

quint64 VibeCutToolSurface::projectRevision() const
{
    return m_baseTools ? m_baseTools->projectRevision() : 0;
}
