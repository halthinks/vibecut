/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "test_utils.hpp"

#include "doc/kdenlivedoc.h"
#include "vibecut/vibecuteval.h"
#include "vibecut/vibecutplanruntime.h"
#include "vibecut/vibecutprojectsnapshot.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject rollbackToolSchema()
{
    return QJsonObject{{QStringLiteral("name"), QStringLiteral("test_partial_failure_edit")},
                       {QStringLiteral("description"), QStringLiteral("Test-only edit that mutates then reports failure")},
                       {QStringLiteral("input_schema"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{}},
                                    {QStringLiteral("additionalProperties"), false}}}};
}

QJsonObject rollbackProposal()
{
    return QJsonObject{{QStringLiteral("objective"), QStringLiteral("Verify synchronous mutation rollback")},
                       {QStringLiteral("operations"),
                        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("mutate-then-fail")},
                                               {QStringLiteral("tool"), QStringLiteral("test_partial_failure_edit")},
                                               {QStringLiteral("input"), QJsonObject{}},
                                               {QStringLiteral("expected_postconditions"),
                                                QJsonArray{QStringLiteral("failure restores the checkpoint")}}}}}};
}

QJsonObject failBeforeMutationToolSchema()
{
    return QJsonObject{{QStringLiteral("name"), QStringLiteral("test_fail_before_mutation")},
                       {QStringLiteral("description"), QStringLiteral("Test-only mutating-policy tool that refuses before pushing any undo command")},
                       {QStringLiteral("input_schema"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{}},
                                    {QStringLiteral("additionalProperties"), false}}}};
}
}

TEST_CASE("plan runtime rollback restores exact live mutation state", "[vibecut][plan-runtime][eval][mutation][live]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack, {1, 3});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), false);
    const std::shared_ptr<TimelineItemModel> timeline = document.getTimeline(document.uuid());
    REQUIRE(timeline);
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 20);
    const int trackId = timeline->getTrackIndexFromPosition(3);
    REQUIRE(timeline->isTrack(trackId));
    const int clipId = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    KdenliveTests::makeFiniteClipEnd(timeline, clipId);
    const int clipLength = timeline->getClipPlaytime(clipId);
    REQUIRE(clipLength > 0);
    REQUIRE(timeline->requestClipMove(clipId, trackId, 0));
    REQUIRE(timeline->checkConsistency());

    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("test_partial_failure_edit");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    REQUIRE(surface.registerTool(rollbackToolSchema(), policy,
                                 [timeline, clipId, trackId, clipLength](const QJsonObject &) {
                                     const bool moved = timeline->requestClipMove(clipId, trackId, clipLength + 5);
                                     if (!moved) {
                                         return QJsonObject{{QStringLiteral("ok"), false},
                                                            {QStringLiteral("error"), QStringLiteral("test mutation could not be applied")}};
                                     }
                                     return QJsonObject{{QStringLiteral("ok"), false},
                                                        {QStringLiteral("error"), QStringLiteral("intentional failure after mutation")}};
                                 }));

    const QJsonObject preState = VibeCutProjectSnapshot::mutationStateV1(timeline);
    REQUIRE(preState.value(QStringLiteral("available")).toBool());

    VibeCutPlanRuntime runtime(&surface);
    runtime.setTrustMode(VibeCutTrustMode::Turbo);
    bool finished = false;
    bool finishedSuccess = true;
    QString finishedSummary;
    QObject::connect(&runtime, &VibeCutPlanRuntime::planFinished,
                     [&finished, &finishedSuccess, &finishedSummary](const QString &, bool success, const QString &summary, const QJsonArray &) {
                         finished = true;
                         finishedSuccess = success;
                         finishedSummary = summary;
                     });

    const QJsonObject proposed = runtime.propose(rollbackProposal());
    REQUIRE(proposed.value(QStringLiteral("ok")).toBool());
    REQUIRE(runtime.hasPendingPlan());
    const QJsonObject approval = runtime.approvePendingPlan();
    CHECK(approval.value(QStringLiteral("ok")).toBool());
    CHECK(finished);
    CHECK_FALSE(finishedSuccess);
    CHECK(finishedSummary.contains(QStringLiteral("rolled back"), Qt::CaseInsensitive));
    CHECK_FALSE(runtime.hasPendingPlan());
    CHECK_FALSE(runtime.executing());
    REQUIRE(timeline->checkConsistency());

    const QJsonObject postState = VibeCutProjectSnapshot::mutationStateV1(timeline);
    VibeCutMutationEvalExpectation expected;
    expected.fixtureId = QStringLiteral("transaction_rollback_after_partial_failure_live");
    expected.expectedOutcome = VibeCutMutationOutcome::RolledBack;
    expected.requireUndo = false;
    expected.requireRedo = false;

    VibeCutMutationEvalObservation observed;
    observed.outcome = VibeCutMutationOutcome::RolledBack;
    observed.preEditState = preState;
    observed.postEditState = postState;
    observed.rollbackVerified = finished && !finishedSuccess && finishedSummary.contains(QStringLiteral("rolled back"), Qt::CaseInsensitive);

    const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
    INFO(score.failures.join(QStringLiteral("; ")).toStdString());
    CHECK(score.verifiedSuccess == Approx(1.0));
    CHECK(score.contractViolations == 0);
    CHECK(score.pass);
}

TEST_CASE("plan failure before native mutation never undoes the previous unrelated command", "[vibecut][plan-runtime][rollback][empty-checkpoint][live]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);

    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack, {1, 3});
    pCore->projectManager()->testSetDocument(&document);
    KdenliveTests::updateTimeline(false, QString(), QString(), QDateTime::currentDateTime(), false);
    const std::shared_ptr<TimelineItemModel> timeline = document.getTimeline(document.uuid());
    REQUIRE(timeline);
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 20);
    const int trackId = timeline->getTrackIndexFromPosition(3);
    REQUIRE(timeline->isTrack(trackId));
    const int clipId = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    KdenliveTests::makeFiniteClipEnd(timeline, clipId);
    REQUIRE(timeline->requestClipMove(clipId, trackId, 0));
    REQUIRE(timeline->checkConsistency());

    // Establish a real pre-existing undoable command that is unrelated to the
    // plan. The old blind endMacro()+undo() rollback could undo this command if
    // the next mutating-policy tool failed before pushing anything.
    const int priorIndex = undoStack->index();
    REQUIRE(priorIndex > 0);
    const QJsonObject preState = VibeCutProjectSnapshot::mutationStateV1(timeline);

    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("test_fail_before_mutation");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    REQUIRE(surface.registerTool(failBeforeMutationToolSchema(), policy,
                                 [](const QJsonObject &) {
                                     return QJsonObject{{QStringLiteral("ok"), false},
                                                        {QStringLiteral("error"), QStringLiteral("intentional refusal before mutation")}};
                                 }));

    VibeCutPlanRuntime runtime(&surface);
    runtime.setTrustMode(VibeCutTrustMode::Turbo);
    const QJsonObject proposed = runtime.propose(
        QJsonObject{{QStringLiteral("objective"), QStringLiteral("Refuse without touching prior undo history")},
                    {QStringLiteral("operations"), QJsonArray{
                        QJsonObject{{QStringLiteral("id"), QStringLiteral("fail" )},
                                    {QStringLiteral("tool"), QStringLiteral("test_fail_before_mutation")},
                                    {QStringLiteral("input"), QJsonObject{}},
                                    {QStringLiteral("expected_postconditions"), QJsonArray()}}}}});
    REQUIRE(proposed.value(QStringLiteral("ok")).toBool());
    runtime.approvePendingPlan();

    CHECK(undoStack->index() == priorIndex);
    CHECK(VibeCutProjectSnapshot::mutationStateV1(timeline) == preState);
    REQUIRE(timeline->checkConsistency());
}
