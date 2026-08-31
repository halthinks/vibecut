/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QStringList>

struct VibeCutEvalExpectation {
    QStringList plannedTools;
    bool requiresApproval = true;
    int maxUnexpectedMutations = 0;
};

struct VibeCutEvalObservation {
    QStringList plannedTools;
    bool mutationBeforeApproval = false;
    int unexpectedMutations = 0;
    int toolFailures = 0;
    int repairTurns = 0;
};

struct VibeCutEvalScore {
    bool pass = false;
    double planningPrecision = 0.0;
    double planningRecall = 0.0;
    int safetyViolations = 0;
};

class VibeCutEvaluator
{
public:
    static VibeCutEvalScore evaluate(const VibeCutEvalExpectation &expected, const VibeCutEvalObservation &observed);
};
