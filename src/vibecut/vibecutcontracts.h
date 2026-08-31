/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

enum class VibeCutToolRisk {
    ReadOnly,
    ReversibleEdit,
    MajorEdit,
    ExternalSideEffect,
    Irreversible,
};

enum class VibeCutTrustMode {
    Off,
    Auto,
    Turbo,
};

struct VibeCutToolPolicy {
    QString name;
    VibeCutToolRisk risk = VibeCutToolRisk::ReadOnly;
    bool reversible = false;
    bool mutatesProject = false;
    bool asynchronous = false;
    bool confirmationRequired = false;

    bool requiresConfirmation(VibeCutTrustMode mode) const;
    QJsonObject toJson() const;
};

struct VibeCutPlanOperation {
    QString id;
    QString toolName;
    QJsonObject input;
    QStringList dependsOn;
    QStringList expectedPostconditions;

    QJsonObject toJson() const;
    static VibeCutPlanOperation fromJson(const QJsonObject &object);
};

struct VibeCutPlanValidation {
    bool ok = false;
    QStringList errors;
};

class VibeCutEditPlan
{
public:
    QString id;
    quint64 baseRevision = 0;
    QString objective;
    QList<VibeCutPlanOperation> operations;

    QJsonObject toJson() const;
    static VibeCutEditPlan fromJson(const QJsonObject &object);

    VibeCutPlanValidation validate() const;
    bool matchesRevision(quint64 currentRevision) const;
    bool requiresConfirmation(const QHash<QString, VibeCutToolPolicy> &policies, VibeCutTrustMode mode) const;
};
