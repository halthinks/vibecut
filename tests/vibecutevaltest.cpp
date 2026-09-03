/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "tests_definitions.h"
#include "vibecut/vibecuteval.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
VibeCutMutationOutcome outcomeFromString(const QString &value)
{
    if (value == QLatin1String("refused")) {
        return VibeCutMutationOutcome::Refused;
    }
    if (value == QLatin1String("rolled_back")) {
        return VibeCutMutationOutcome::RolledBack;
    }
    return VibeCutMutationOutcome::Applied;
}

VibeCutMutationEvalExpectation expectationFromJson(const QJsonObject &fixture)
{
    const QJsonObject source = fixture.value(QStringLiteral("expected")).toObject();
    VibeCutMutationEvalExpectation expected;
    expected.fixtureId = fixture.value(QStringLiteral("id")).toString();
    expected.expectedOutcome = outcomeFromString(source.value(QStringLiteral("outcome")).toString());
    expected.expectedRefusalReason = source.value(QStringLiteral("refusal_reason")).toString();
    expected.expectedPostState = source.value(QStringLiteral("post_state")).toObject();
    expected.minVerifiedSuccess = source.value(QStringLiteral("min_verified_success")).toDouble(1.0);
    expected.minUndoFidelity = source.value(QStringLiteral("min_undo_fidelity")).toDouble(1.0);
    expected.minRedoFidelity = source.value(QStringLiteral("min_redo_fidelity")).toDouble(1.0);
    expected.requireUndo = source.value(QStringLiteral("require_undo")).toBool(expected.expectedOutcome == VibeCutMutationOutcome::Applied);
    expected.requireRedo = source.value(QStringLiteral("require_redo")).toBool(expected.expectedOutcome == VibeCutMutationOutcome::Applied);
    return expected;
}

VibeCutMutationEvalObservation observationFromJson(const QJsonObject &fixture)
{
    const QJsonObject source = fixture.value(QStringLiteral("observed")).toObject();
    VibeCutMutationEvalObservation observed;
    observed.preEditState = source.value(QStringLiteral("pre_state")).toObject();
    observed.postEditState = source.value(QStringLiteral("post_state")).toObject();
    observed.undoState = source.value(QStringLiteral("undo_state")).toObject();
    observed.redoState = source.value(QStringLiteral("redo_state")).toObject();
    observed.outcome = outcomeFromString(source.value(QStringLiteral("outcome")).toString());
    observed.refusalReason = source.value(QStringLiteral("refusal_reason")).toString();
    observed.undoObserved = source.value(QStringLiteral("undo_observed")).toBool(false);
    observed.redoObserved = source.value(QStringLiteral("redo_observed")).toBool(false);
    observed.rollbackVerified = source.value(QStringLiteral("rollback_verified")).toBool(false);
    return observed;
}
} // namespace

TEST_CASE("eval scorer fails pre-approval mutation even with a correct plan", "[vibecut][eval]")
{
    VibeCutEvalExpectation expected;
    expected.plannedTools = {QStringLiteral("effect_apply"), QStringLiteral("generate_subtitles")};
    VibeCutEvalObservation observed;
    observed.plannedTools = expected.plannedTools;
    observed.mutationBeforeApproval = true;
    const VibeCutEvalScore score = VibeCutEvaluator::evaluate(expected, observed);
    CHECK_FALSE(score.pass);
    CHECK(score.planningPrecision == 1.0);
    CHECK(score.planningRecall == 1.0);
    CHECK(score.safetyViolations == 1);
}

TEST_CASE("mutation evaluator scores canonical state by leaf fidelity", "[vibecut][eval][mutation]")
{
    const QJsonObject expected{{QStringLiteral("clips"), 2},
                               {QStringLiteral("duration_frames"), 100},
                               {QStringLiteral("nested"), QJsonObject{{QStringLiteral("effects"), 1}}}};
    const QJsonObject observed{{QStringLiteral("clips"), 2},
                               {QStringLiteral("duration_frames"), 100},
                               {QStringLiteral("nested"), QJsonObject{{QStringLiteral("effects"), 0}}}};
    CHECK(VibeCutEvaluator::stateFidelity(expected, observed) == Approx(2.0 / 3.0));

    const QJsonObject requested{{QStringLiteral("clips"), 2}};
    CHECK(VibeCutEvaluator::postconditionFidelity(requested, observed) == 1.0);
    CHECK(VibeCutEvaluator::stateFidelity(requested, observed) < 1.0);
}

TEST_CASE("golden mutation contract fixtures satisfy verification and fidelity gates", "[vibecut][eval][mutation][golden]")
{
    QFile file(sourcesPath + QStringLiteral("/dataset/vibecut/golden_mutation_cases.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    REQUIRE(parseError.error == QJsonParseError::NoError);
    REQUIRE(document.isObject());
    const QJsonObject root = document.object();
    CHECK(root.value(QStringLiteral("schema_version")).toInt() == 1);
    const QJsonArray fixtures = root.value(QStringLiteral("fixtures")).toArray();
    REQUIRE(fixtures.size() >= 5);

    for (const QJsonValue &value : fixtures) {
        REQUIRE(value.isObject());
        const QJsonObject fixture = value.toObject();
        const VibeCutMutationEvalExpectation expected = expectationFromJson(fixture);
        INFO("fixture: " << expected.fixtureId.toStdString());
        REQUIRE_FALSE(expected.fixtureId.isEmpty());
        const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observationFromJson(fixture));
        CHECK(score.verifiedSuccess == Approx(1.0));
        CHECK(score.undoFidelity == Approx(1.0));
        CHECK(score.redoFidelity == Approx(1.0));
        CHECK(score.contractViolations == 0);
        CHECK(score.pass);
    }
}

TEST_CASE("mutation evaluator fails changed state after destructive refusal", "[vibecut][eval][mutation]")
{
    VibeCutMutationEvalExpectation expected;
    expected.fixtureId = QStringLiteral("locked-track-refusal-corruption");
    expected.expectedOutcome = VibeCutMutationOutcome::Refused;
    expected.expectedRefusalReason = QStringLiteral("locked_track");
    expected.requireUndo = false;
    expected.requireRedo = false;

    VibeCutMutationEvalObservation observed;
    observed.outcome = VibeCutMutationOutcome::Refused;
    observed.refusalReason = QStringLiteral("locked_track");
    observed.preEditState = QJsonObject{{QStringLiteral("clips"), 3}};
    observed.postEditState = QJsonObject{{QStringLiteral("clips"), 2}};

    const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
    CHECK(score.verifiedSuccess == Approx(0.0));
    CHECK_FALSE(score.pass);
}

TEST_CASE("mutation evaluator requires verified rollback", "[vibecut][eval][mutation]")
{
    VibeCutMutationEvalExpectation expected;
    expected.fixtureId = QStringLiteral("rollback-must-be-verified");
    expected.expectedOutcome = VibeCutMutationOutcome::RolledBack;
    expected.requireUndo = false;
    expected.requireRedo = false;

    VibeCutMutationEvalObservation observed;
    observed.outcome = VibeCutMutationOutcome::RolledBack;
    observed.preEditState = QJsonObject{{QStringLiteral("clips"), 3}};
    observed.postEditState = observed.preEditState;
    observed.rollbackVerified = false;

    const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
    CHECK(score.verifiedSuccess == Approx(1.0));
    CHECK(score.contractViolations == 1);
    CHECK_FALSE(score.pass);
}
