/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "vibecuttoolsurface.h"

#include "vibecutedittools.h"
#include "vibecuttools.h"

#include <QDebug>

namespace {
QJsonObject errorResult(const QString &message)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
}
} // namespace

VibeCutToolSurface::VibeCutToolSurface(VibeCutTools *baseTools)
    : m_baseTools(baseTools)
{
    // Core timeline mutations are part of VibeCut's canonical surface rather
    // than optional agent wiring: every planner/runtime sees the same edit
    // vocabulary, while the implementations remain isolated from the legacy
    // vibecuttools.cpp monolith.
    QString error;
    if (!registerVibeCutEditTools(*this, &error)) {
        qWarning().noquote() << QStringLiteral("[VibeCut] core edit tools unavailable: %1").arg(error);
    }
}

bool VibeCutToolSurface::baseContains(const QString &name) const
{
    if (!m_baseTools) return false;
    const QJsonArray baseSchemas = m_baseTools->schemas();
    for (const QJsonValue &value : baseSchemas) {
        if (value.toObject().value(QStringLiteral("name")).toString() == name) return true;
    }
    return false;
}

bool VibeCutToolSurface::validateRegistration(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (error) error->clear();
    const QString name = schema.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (error) *error = QStringLiteral("tool schema requires a non-empty name");
        return false;
    }
    if (policy.name != name) {
        if (error) *error = QStringLiteral("tool policy name '%1' does not match schema name '%2'").arg(policy.name, name);
        return false;
    }
    if (!schema.value(QStringLiteral("input_schema")).isObject()) {
        if (error) *error = QStringLiteral("tool '%1' requires an input_schema object").arg(name);
        return false;
    }
    if (!handler) {
        if (error) *error = QStringLiteral("tool '%1' requires a handler").arg(name);
        return false;
    }
    return true;
}

bool VibeCutToolSurface::registerTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (!validateRegistration(schema, policy, handler, error)) return false;
    const QString name = policy.name;
    if (m_extensions.contains(name) || m_overrides.contains(name) || baseContains(name)) {
        if (error) *error = QStringLiteral("tool '%1' is already registered").arg(name);
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

bool VibeCutToolSurface::overrideBaseTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error)
{
    if (!validateRegistration(schema, policy, handler, error)) return false;
    const QString name = policy.name;
    if (!baseContains(name)) {
        if (error) *error = QStringLiteral("cannot override unknown native tool '%1'").arg(name);
        return false;
    }
    if (m_extensions.contains(name) || m_overrides.contains(name)) {
        if (error) *error = QStringLiteral("tool '%1' already has a surface registration").arg(name);
        return false;
    }
    Extension override;
    override.schema = schema;
    override.policy = policy;
    override.handler = handler;
    m_overrides.insert(name, override);
    return true;
}

QJsonArray VibeCutToolSurface::schemas() const
{
    QJsonArray result;
    if (m_baseTools) {
        for (const QJsonValue &value : m_baseTools->schemas()) {
            const QJsonObject baseSchema = value.toObject();
            const QString name = baseSchema.value(QStringLiteral("name")).toString();
            const auto override = m_overrides.constFind(name);
            result.append(override != m_overrides.constEnd() ? override.value().schema : baseSchema);
        }
    }
    for (const QString &name : m_extensionOrder) result.append(m_extensions.value(name).schema);
    return result;
}

QHash<QString, VibeCutToolPolicy> VibeCutToolSurface::policies() const
{
    QHash<QString, VibeCutToolPolicy> result = m_baseTools ? m_baseTools->policies() : QHash<QString, VibeCutToolPolicy>();
    for (auto it = m_overrides.constBegin(); it != m_overrides.constEnd(); ++it) result.insert(it.key(), it.value().policy);
    for (const QString &name : m_extensionOrder) result.insert(name, m_extensions.value(name).policy);
    return result;
}

QJsonObject VibeCutToolSurface::invoke(const QString &name, const QJsonObject &input) const
{
    const auto override = m_overrides.constFind(name);
    if (override != m_overrides.constEnd()) return override.value().handler(input);
    const auto extension = m_extensions.constFind(name);
    if (extension != m_extensions.constEnd()) return extension.value().handler(input);
    return invokeBase(name, input);
}

QJsonObject VibeCutToolSurface::invokeBase(const QString &name, const QJsonObject &input) const
{
    if (m_baseTools) return m_baseTools->invoke(name, input);
    return errorResult(QStringLiteral("Unknown tool: %1").arg(name));
}

quint64 VibeCutToolSurface::projectRevision() const
{
    return m_baseTools ? m_baseTools->projectRevision() : 0;
}
