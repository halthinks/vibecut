/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "test_utils.hpp"

#include "doc/kdenlivedoc.h"
#include "vibecut/vibecuteval.h"
#include "vibecut/vibecutplanruntime.h"
#include "vibecut/vibecutprojectsnapshot.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject staleToolSchema()
{
    return QJsonObject{{QStringLiteral("name"), QStringLiteral("test_stale_guarded_edit")},
                       {QStringLiteral("description"), QStringLiteral("Test-only mutation that must never run after the plan becomes stale")},
                       {QStringLiteral("input_schema"),
                        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                                    {QStringLiteral("properties"), QJsonObject{}},
                                    {QStringLiteral("additionalProperties"), false}}}};
}

QJsonObject staleProposal()
{
    return QJsonObject{{QStringLiteral("objective"), QStringLiteral("Verify stale plan refusal preserves live state")},
                       {QStringLiteral("operations"),
                        QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("guarded-edit")},
                                               {QStringLiteral("tool"), QStringLiteral("test_stale_guarded_edit")},
                                               {QStringLiteral("input"), QJsonObject{}},
                                               {QStringLiteral("expected_postconditions"),
                                                QJsonArray{QStringLiteral("planned mutation executes only against its bound revision")}}}}}};
}
} // namespace

TEST_CASE("stale approved plan is refused before mutation and preserves exact live state", "[vibecut][plan-runtime][eval][mutation][live]")
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

    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "green", binModel, 20);
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
    int plannedCalls = 0;
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("test_stale_guarded_edit");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    REQUIRE(surface.registerTool(staleToolSchema(), policy,
                                 [&plannedCalls, timeline, clipId, trackId, clipLength](const QJsonObject &) {
                                     ++plannedCalls;
                                     const bool moved = timeline->requestClipMove(clipId, trackId, 2 * clipLength + 10);
                                     return QJsonObject{{QStringLiteral("ok"), moved},
                                                        {QStringLiteral("error"), moved ? QString() : QStringLiteral("planned test move failed")}};
                                 }));

    VibeCutPlanRuntime runtime(&surface);
    runtime.setTrustMode(VibeCutTrustMode::Turbo);
    const QJsonObject proposed = runtime.propose(staleProposal());
    REQUIRE(proposed.value(QStringLiteral("ok")).toBool());
    REQUIRE(runtime.hasPendingPlan());
    const quint64 plannedRevision = proposed.value(QStringLiteral("plan")).toObject().value(QStringLiteral("base_revision")).toVariant().toULongLong();

    // Change the real Kdenlive project after proposal. VibeCut's revision tracker
    // is already observing this DocUndoStack because propose() captured the plan
    // revision through the ToolSurface.
    REQUIRE(timeline->requestClipMove(clipId, trackId, clipLength + 5));
    REQUIRE(timeline->checkConsistency());
    const quint64 currentRevision = surface.projectRevision();
    REQUIRE(currentRevision != plannedRevision);

    const QJsonObject beforeRefusal = VibeCutProjectSnapshot::mutationStateV1(timeline);
    REQUIRE(beforeRefusal.value(QStringLiteral("available")).toBool());
    const int undoIndexBeforeApproval = undoStack->index();

    const QJsonObject approval = runtime.approvePendingPlan();
    CHECK_FALSE(approval.value(QStringLiteral("ok")).toBool());
    const QString error = approval.value(QStringLiteral("error")).toString();
    CHECK(error.contains(QStringLiteral("revision"), Qt::CaseInsensitive));
    CHECK(plannedCalls == 0);
    CHECK_FALSE(runtime.hasPendingPlan());
    CHECK_FALSE(runtime.executing());
    CHECK(undoStack->index() == undoIndexBeforeApproval);
    REQUIRE(timeline->checkConsistency());

    VibeCutMutationEvalExpectation expected;
    expected.fixtureId = QStringLiteral("stale_plan_refusal_live");
    expected.expectedOutcome = VibeCutMutationOutcome::Refused;
    expected.expectedRefusalReason = QStringLiteral("stale_plan");
    expected.requireUndo = false;
    expected.requireRedo = false;

    VibeCutMutationEvalObservation observed;
    observed.outcome = VibeCutMutationOutcome::Refused;
    observed.refusalReason = QStringLiteral("stale_plan");
    observed.preEditState = beforeRefusal;
    observed.postEditState = VibeCutProjectSnapshot::mutationStateV1(timeline);

    const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
    INFO(score.failures.join(QStringLiteral("; ")).toStdString());
    CHECK(score.verifiedSuccess == Approx(1.0));
    CHECK(score.contractViolations == 0);
    CHECK(score.pass);
}
