/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#pragma once

#include <QJsonObject>
#include <QString>
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

enum class VibeCutMutationOutcome {
    Applied,
    Refused,
    RolledBack,
};

struct VibeCutMutationEvalExpectation {
    QString fixtureId;
    QJsonObject expectedPostState;
    VibeCutMutationOutcome expectedOutcome = VibeCutMutationOutcome::Applied;
    QString expectedRefusalReason;
    double minVerifiedSuccess = 1.0;
    double minUndoFidelity = 1.0;
    double minRedoFidelity = 1.0;
    bool requireUndo = true;
    bool requireRedo = true;
};

struct VibeCutMutationEvalObservation {
    QJsonObject preEditState;
    QJsonObject postEditState;
    QJsonObject undoState;
    QJsonObject redoState;
    VibeCutMutationOutcome outcome = VibeCutMutationOutcome::Applied;
    QString refusalReason;
    bool undoObserved = false;
    bool redoObserved = false;
    bool rollbackVerified = false;
};

struct VibeCutMutationEvalScore {
    bool pass = false;
    double verifiedSuccess = 0.0;
    double undoFidelity = 0.0;
    double redoFidelity = 0.0;
    int contractViolations = 0;
    QStringList failures;
};

class VibeCutEvaluator
{
public:
    static VibeCutEvalScore evaluate(const VibeCutEvalExpectation &expected, const VibeCutEvalObservation &observed);

    // Compare complete canonical states. Unexpected observed structure counts as a mismatch.
    static double stateFidelity(const QJsonObject &expected, const QJsonObject &observed);

    // Compare only the requested postcondition shape. Unspecified observed state is ignored.
    static double postconditionFidelity(const QJsonObject &expected, const QJsonObject &observed);

    static VibeCutMutationEvalScore evaluateMutation(const VibeCutMutationEvalExpectation &expected,
                                                     const VibeCutMutationEvalObservation &observed);
};
