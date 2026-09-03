/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#pragma once

#include "vibecutcontracts.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

class VibeCutPolicyOverrides
{
public:
    static QString fileName();

    /** Apply a parsed project-policy object to code-defined base policies.
     * Code-defined confirmationRequired=true is a hard lower bound: project
     * configuration may make policy stricter but may not waive it. */
    static QHash<QString, VibeCutToolPolicy> applyObject(const QHash<QString, VibeCutToolPolicy> &basePolicies,
                                                         const QJsonObject &root);

    static QHash<QString, VibeCutToolPolicy> applyCurrent(const QHash<QString, VibeCutToolPolicy> &basePolicies, QString *error = nullptr);

    // Compatibility facade for call sites that consume policy overrides as a
    // value object. The authoritative implementation remains applyCurrent(),
    // so parsing and policy semantics stay centralized in one path.
    static VibeCutPolicyOverrides loadCurrent() { return VibeCutPolicyOverrides(); }

    QHash<QString, VibeCutToolPolicy> apply(const QHash<QString, VibeCutToolPolicy> &basePolicies, QString *error = nullptr) const
    {
        return applyCurrent(basePolicies, error);
    }

    bool isDenied(const QString &name) const
    {
        VibeCutToolPolicy policy;
        policy.name = name;
        policy.enabled = true;
        QHash<QString, VibeCutToolPolicy> policies;
        policies.insert(name, policy);
        const QHash<QString, VibeCutToolPolicy> effective = applyCurrent(policies);
        return !effective.value(name).enabled;
    }
};
