/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "vibecutcontracts.h"

#include <QHash>
#include <QStringList>

enum class VibeCutPlanGateStatus {
    Ready,
    InvalidPlan,
    StalePlan,
    UnknownTool,
    ConfirmationRequired,
};

struct VibeCutPlanGateResult {
    VibeCutPlanGateStatus status = VibeCutPlanGateStatus::InvalidPlan;
    QStringList errors;
    QStringList executionOrder;

    bool ready() const { return status == VibeCutPlanGateStatus::Ready; }
};

class VibeCutPlanGate
{
public:
    static VibeCutPlanGateResult assess(const VibeCutEditPlan &plan, quint64 currentRevision,
                                        const QHash<QString, VibeCutToolPolicy> &policies, VibeCutTrustMode mode,
                                        bool planApproved);
};
