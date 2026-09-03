/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "test_utils.hpp"

#include "doc/kdenlivedoc.h"
#include "vibecut/vibecuteval.h"
#include "vibecut/vibecutprojectsnapshot.h"
#include "vibecut/vibecuttakeselectiontools.h"

namespace {
QJsonObject resolvedPlan(const QJsonArray &rejects)
{
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("selection_count"), 1},
                       {QStringLiteral("resolved_group_count"), 1},
                       {QStringLiteral("rejected_take_count"), rejects.size()},
                       {QStringLiteral("execution_ready"), !rejects.isEmpty()},
                       {QStringLiteral("groups"),
                        QJsonArray{QJsonObject{{QStringLiteral("group_index"), 0},
                                               {QStringLiteral("keep"), QJsonObject{{QStringLiteral("subtitle_id"), 100}}},
                                               {QStringLiteral("reject"), rejects},
                                               {QStringLiteral("reject_count"), rejects.size()}}}}};
}

QJsonObject rejectRange(int subtitleId, int start, int end)
{
    return QJsonObject{{QStringLiteral("subtitle_id"), subtitleId},
                       {QStringLiteral("timeline_start_frame"), start},
                       {QStringLiteral("timeline_end_frame"), end},
                       {QStringLiteral("remove_frames"), end - start},
                       {QStringLiteral("execution_ready"), true}};
}
} // namespace

TEST_CASE("resolved repeated-take mutation core is overlap-safe and one-step reversible", "[vibecut][repeated-takes][eval][mutation][live]")
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

    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "yellow", binModel, 20);
    const int trackId = timeline->getTrackIndexFromPosition(3);
    REQUIRE(timeline->isTrack(trackId));

    const int clip1 = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    const int clip2 = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    const int clip3 = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    KdenliveTests::makeFiniteClipEnd(timeline, clip1);
    KdenliveTests::makeFiniteClipEnd(timeline, clip2);
    KdenliveTests::makeFiniteClipEnd(timeline, clip3);
    const int clipLength = timeline->getClipPlaytime(clip1);
    REQUIRE(clipLength > 2);
    REQUIRE(timeline->requestClipMove(clip1, trackId, 0));
    REQUIRE(timeline->requestClipMove(clip2, trackId, clipLength));
    REQUIRE(timeline->requestClipMove(clip3, trackId, 2 * clipLength));
    REQUIRE(timeline->checkConsistency());

    SECTION("overlap refusal makes no project or Undo-stack mutation")
    {
        const QJsonObject preState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        const int undoIndex = undoStack->index();
        const QJsonObject plan = resolvedPlan(QJsonArray{
            rejectRange(101, clipLength, 2 * clipLength),
            rejectRange(102, clipLength + clipLength / 2, 2 * clipLength + clipLength / 2),
        });

        const QJsonObject result = executeVibeCutResolvedTakeSelection(timeline, plan, QStringLiteral("ripple"));
        CHECK_FALSE(result.value(QStringLiteral("ok")).toBool());
        CHECK(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("overlap"), Qt::CaseInsensitive));
        CHECK(undoStack->index() == undoIndex);
        REQUIRE(timeline->checkConsistency());

        VibeCutMutationEvalExpectation expected;
        expected.fixtureId = QStringLiteral("repeated_take_overlap_refusal_live");
        expected.expectedOutcome = VibeCutMutationOutcome::Refused;
        expected.expectedRefusalReason = QStringLiteral("overlap");
        expected.requireUndo = false;
        expected.requireRedo = false;

        VibeCutMutationEvalObservation observed;
        observed.outcome = VibeCutMutationOutcome::Refused;
        observed.refusalReason = QStringLiteral("overlap");
        observed.preEditState = preState;
        observed.postEditState = VibeCutProjectSnapshot::mutationStateV1(timeline);

        const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
        INFO(score.failures.join(QStringLiteral("; ")).toStdString());
        CHECK(score.verifiedSuccess == Approx(1.0));
        CHECK(score.pass);
    }

    SECTION("successful repeated-take batch is exactly one Undo step with exact redo fidelity")
    {
        const QJsonObject preState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        REQUIRE(preState.value(QStringLiteral("clip_count")).toInt() == 3);
        const int undoIndexBefore = undoStack->index();
        const QJsonObject plan = resolvedPlan(QJsonArray{
            rejectRange(101, clipLength, 2 * clipLength),
            rejectRange(102, 2 * clipLength, 3 * clipLength),
        });

        const QJsonObject result = executeVibeCutResolvedTakeSelection(timeline, plan, QStringLiteral("ripple"));
        REQUIRE(result.value(QStringLiteral("ok")).toBool());
        CHECK(result.value(QStringLiteral("verified")).toBool());
        CHECK(result.value(QStringLiteral("undo_atomic")).toBool());
        CHECK(result.value(QStringLiteral("removed_take_count")).toInt() == 2);
        CHECK(undoStack->index() == undoIndexBefore + 1);
        REQUIRE(timeline->checkConsistency());

        const QJsonObject postState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        CHECK(postState.value(QStringLiteral("clip_count")).toInt() == 1);

        VibeCutMutationEvalExpectation expected;
        expected.fixtureId = QStringLiteral("repeated_take_selection_execute_success_live");
        expected.expectedOutcome = VibeCutMutationOutcome::Applied;
        expected.expectedPostState = QJsonObject{{QStringLiteral("clip_count"), 1}};

        VibeCutMutationEvalObservation observed;
        observed.outcome = VibeCutMutationOutcome::Applied;
        observed.preEditState = preState;
        observed.postEditState = postState;

        REQUIRE(undoStack->canUndo());
        undoStack->undo();
        REQUIRE(timeline->checkConsistency());
        observed.undoObserved = true;
        observed.undoState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        CHECK(undoStack->index() == undoIndexBefore);

        REQUIRE(undoStack->canRedo());
        undoStack->redo();
        REQUIRE(timeline->checkConsistency());
        observed.redoObserved = true;
        observed.redoState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        CHECK(undoStack->index() == undoIndexBefore + 1);

        const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
        INFO(score.failures.join(QStringLiteral("; ")).toStdString());
        CHECK(score.verifiedSuccess == Approx(1.0));
        CHECK(score.undoFidelity == Approx(1.0));
        CHECK(score.redoFidelity == Approx(1.0));
        CHECK(score.contractViolations == 0);
        CHECK(score.pass);
    }
}
