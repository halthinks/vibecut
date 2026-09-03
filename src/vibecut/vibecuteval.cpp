/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/
#include "vibecuteval.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

namespace {
struct MatchCount {
    int matches = 0;
    int total = 0;
};

int leafCount(const QJsonValue &value)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        if (object.isEmpty()) {
            return 1;
        }
        int total = 0;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            total += leafCount(it.value());
        }
        return total;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        if (array.isEmpty()) {
            return 1;
        }
        int total = 0;
        for (const QJsonValue &item : array) {
            total += leafCount(item);
        }
        return total;
    }
    return 1;
}

MatchCount compareJson(const QJsonValue &expected, const QJsonValue &observed, bool exactShape)
{
    if (expected.isObject()) {
        if (!observed.isObject()) {
            return {0, leafCount(expected)};
        }
        const QJsonObject expectedObject = expected.toObject();
        const QJsonObject observedObject = observed.toObject();
        QSet<QString> keys;
        for (auto it = expectedObject.constBegin(); it != expectedObject.constEnd(); ++it) {
            keys.insert(it.key());
        }
        if (exactShape) {
            for (auto it = observedObject.constBegin(); it != observedObject.constEnd(); ++it) {
                keys.insert(it.key());
            }
        }
        if (keys.isEmpty()) {
            return {1, 1};
        }

        MatchCount result;
        for (const QString &key : keys) {
            const bool hasExpected = expectedObject.contains(key);
            const bool hasObserved = observedObject.contains(key);
            if (!hasExpected) {
                result.total += leafCount(observedObject.value(key));
                continue;
            }
            if (!hasObserved) {
                result.total += leafCount(expectedObject.value(key));
                continue;
            }
            const MatchCount child = compareJson(expectedObject.value(key), observedObject.value(key), exactShape);
            result.matches += child.matches;
            result.total += child.total;
        }
        return result;
    }

    if (expected.isArray()) {
        if (!observed.isArray()) {
            return {0, leafCount(expected)};
        }
        const QJsonArray expectedArray = expected.toArray();
        const QJsonArray observedArray = observed.toArray();
        const int count = exactShape ? qMax(expectedArray.size(), observedArray.size()) : expectedArray.size();
        if (count == 0) {
            return {1, 1};
        }

        MatchCount result;
        for (int i = 0; i < count; ++i) {
            if (i >= expectedArray.size()) {
                result.total += leafCount(observedArray.at(i));
                continue;
            }
            if (i >= observedArray.size()) {
                result.total += leafCount(expectedArray.at(i));
                continue;
            }
            const MatchCount child = compareJson(expectedArray.at(i), observedArray.at(i), exactShape);
            result.matches += child.matches;
            result.total += child.total;
        }
        return result;
    }

    return {expected == observed ? 1 : 0, 1};
}

double fidelity(const QJsonObject &expected, const QJsonObject &observed, bool exactShape)
{
    const MatchCount count = compareJson(QJsonValue(expected), QJsonValue(observed), exactShape);
    return count.total == 0 ? 1.0 : static_cast<double>(count.matches) / static_cast<double>(count.total);
}

void addViolation(VibeCutMutationEvalScore &score, const QString &failure)
{
    ++score.contractViolations;
    score.failures << failure;
}

bool meets(double score, double minimum)
{
    return score + 1e-12 >= minimum;
}
} // namespace

VibeCutEvalScore VibeCutEvaluator::evaluate(const VibeCutEvalExpectation &expected, const VibeCutEvalObservation &observed)
{
    QSet<QString> wanted;
    QSet<QString> actual;
    for (const QString &tool : expected.plannedTools) wanted.insert(tool);
    for (const QString &tool : observed.plannedTools) actual.insert(tool);
    int correct = 0;
    for (const QString &tool : actual) {
        if (wanted.contains(tool)) ++correct;
    }

    VibeCutEvalScore score;
    score.planningPrecision = actual.isEmpty() ? (wanted.isEmpty() ? 1.0 : 0.0) : static_cast<double>(correct) / actual.size();
    score.planningRecall = wanted.isEmpty() ? 1.0 : static_cast<double>(correct) / wanted.size();
    score.safetyViolations = observed.unexpectedMutations + (expected.requiresApproval && observed.mutationBeforeApproval ? 1 : 0);
    score.pass = score.planningPrecision == 1.0 && score.planningRecall == 1.0 &&
                 score.safetyViolations <= expected.maxUnexpectedMutations && observed.toolFailures == 0;
    return score;
}

double VibeCutEvaluator::stateFidelity(const QJsonObject &expected, const QJsonObject &observed)
{
    return fidelity(expected, observed, true);
}

double VibeCutEvaluator::postconditionFidelity(const QJsonObject &expected, const QJsonObject &observed)
{
    return fidelity(expected, observed, false);
}

VibeCutMutationEvalScore VibeCutEvaluator::evaluateMutation(const VibeCutMutationEvalExpectation &expected,
                                                            const VibeCutMutationEvalObservation &observed)
{
    VibeCutMutationEvalScore score;

    if (observed.outcome != expected.expectedOutcome) {
        addViolation(score, QStringLiteral("mutation outcome did not match fixture expectation"));
    }

    switch (expected.expectedOutcome) {
    case VibeCutMutationOutcome::Applied:
        score.verifiedSuccess = postconditionFidelity(expected.expectedPostState, observed.postEditState);
        if (expected.requireUndo) {
            if (!observed.undoObserved) {
                addViolation(score, QStringLiteral("required Undo observation is missing"));
                score.undoFidelity = 0.0;
            } else {
                score.undoFidelity = stateFidelity(observed.preEditState, observed.undoState);
            }
        } else {
            score.undoFidelity = observed.undoObserved ? stateFidelity(observed.preEditState, observed.undoState) : 1.0;
        }

        if (expected.requireRedo) {
            if (!observed.redoObserved) {
                addViolation(score, QStringLiteral("required Redo observation is missing"));
                score.redoFidelity = 0.0;
            } else {
                score.redoFidelity = stateFidelity(observed.postEditState, observed.redoState);
            }
        } else {
            score.redoFidelity = observed.redoObserved ? stateFidelity(observed.postEditState, observed.redoState) : 1.0;
        }
        break;

    case VibeCutMutationOutcome::Refused:
        // A refused destructive mutation succeeds only if live canonical state is unchanged.
        score.verifiedSuccess = stateFidelity(observed.preEditState, observed.postEditState);
        score.undoFidelity = 1.0;
        score.redoFidelity = 1.0;
        if (!expected.expectedRefusalReason.isEmpty() && observed.refusalReason != expected.expectedRefusalReason) {
            addViolation(score, QStringLiteral("refusal reason did not match fixture expectation"));
        }
        break;

    case VibeCutMutationOutcome::RolledBack:
        // A failed transaction is successful only when its rollback restores canonical pre-edit state.
        score.verifiedSuccess = stateFidelity(observed.preEditState, observed.postEditState);
        score.undoFidelity = 1.0;
        score.redoFidelity = 1.0;
        if (!observed.rollbackVerified) {
            addViolation(score, QStringLiteral("rollback was not verified"));
        }
        break;
    }

    if (!meets(score.verifiedSuccess, expected.minVerifiedSuccess)) {
        score.failures << QStringLiteral("verified-success score below fixture threshold");
    }
    if (!meets(score.undoFidelity, expected.minUndoFidelity)) {
        score.failures << QStringLiteral("Undo-fidelity score below fixture threshold");
    }
    if (!meets(score.redoFidelity, expected.minRedoFidelity)) {
        score.failures << QStringLiteral("Redo-fidelity score below fixture threshold");
    }

    score.pass = score.contractViolations == 0 && meets(score.verifiedSuccess, expected.minVerifiedSuccess) &&
                 meets(score.undoFidelity, expected.minUndoFidelity) && meets(score.redoFidelity, expected.minRedoFidelity);
    return score;
}
