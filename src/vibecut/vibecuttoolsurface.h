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
 * The surface merges provider schemas, governance policies and dispatch while
 * failing registration on duplicates or missing policy metadata.
 */
class VibeCutToolSurface
{
public:
    typedef std::function<QJsonObject(const QJsonObject &)> Handler;

    explicit VibeCutToolSurface(VibeCutTools *baseTools);

    bool registerTool(const QJsonObject &schema, const VibeCutToolPolicy &policy, const Handler &handler, QString *error = nullptr);

    QJsonArray schemas() const;
    QHash<QString, VibeCutToolPolicy> policies() const;
    QJsonObject invoke(const QString &name, const QJsonObject &input) const;
    quint64 projectRevision() const;

private:
    struct Extension {
        QJsonObject schema;
        VibeCutToolPolicy policy;
        Handler handler;
    };

    bool baseContains(const QString &name) const;

    VibeCutTools *m_baseTools = nullptr;
    QHash<QString, Extension> m_extensions;
    QStringList m_extensionOrder;
};
