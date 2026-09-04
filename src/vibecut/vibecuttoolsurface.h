/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "vibecutcontracts.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>

class VibeCutTools;

/** Composable provider-independent capability surface.
 *
 * Existing native handlers remain in VibeCutTools. New capabilities can be
 * registered as small isolated modules instead of growing vibecuttools.cpp.
 * Existing native tools can also be decorated/overridden at this boundary so
 * fixes can be introduced without rewriting their legacy implementation.
 */
class VibeCutToolSurface
{
public:
    typedef std::function<QJsonObject(const QJsonObject &)> Handler;

    explicit VibeCutToolSurface(VibeCutTools *baseTools);

    bool registerTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error = nullptr);
    bool overrideBaseTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error = nullptr);

    QJsonArray schemas() const;
    QHash<QString, VibeCutToolPolicy> policies() const;
    /** Deterministic protocol-facing snapshot of the currently advertised
     * schemas paired with their effective policies and current revision.
     * Denied tools are omitted because schemas() already applies overrides. */
    QJsonObject runtimeContractSnapshot() const;
    QJsonObject invoke(const QString &name, const QJsonObject &input) const;
    /** Bypass any surface override and invoke the original native handler. */
    QJsonObject invokeBase(const QString &name, const QJsonObject &input) const;
    VibeCutTools *baseTools() const { return m_baseTools; }
    quint64 projectRevision() const;

private:
    struct Extension {
        QJsonObject schema;
        VibeCutToolPolicy policy;
        Handler handler;
    };

    bool baseContains(const QString &name) const;
    static bool validateRegistration(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error);

    VibeCutTools *m_baseTools = nullptr;
    QHash<QString, Extension> m_extensions;
    QStringList m_extensionOrder;
    QHash<QString, Extension> m_overrides;
};
