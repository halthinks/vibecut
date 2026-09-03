/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "test_utils.hpp"

#include "doc/kdenlivedoc.h"
#include "vibecut/vibecutedittools.h"
#include "vibecut/vibecuteval.h"
#include "vibecut/vibecutprojectsnapshot.h"

TEST_CASE("live timeline range removal satisfies mutation fidelity contract", "[vibecut][eval][mutation][live]")
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

    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 20);
    const int trackId = timeline->getTrackIndexFromPosition(3);
    REQUIRE(timeline->isTrack(trackId));

    const int clip1 = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    const int clip2 = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    const int clip3 = ClipModel::construct(timeline, binId, -1, PlaylistState::VideoOnly);
    KdenliveTests::makeFiniteClipEnd(timeline, clip1);
    KdenliveTests::makeFiniteClipEnd(timeline, clip2);
    KdenliveTests::makeFiniteClipEnd(timeline, clip3);
    const int clipLength = timeline->getClipPlaytime(clip1);
    REQUIRE(clipLength > 0);
    REQUIRE(timeline->requestClipMove(clip1, trackId, 0));
    REQUIRE(timeline->requestClipMove(clip2, trackId, clipLength));
    REQUIRE(timeline->requestClipMove(clip3, trackId, 2 * clipLength));
    REQUIRE(timeline->checkConsistency());

    SECTION("ripple removal verifies postcondition and exact undo redo state")
    {
        const QJsonObject preState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        REQUIRE(preState.value(QStringLiteral("available")).toBool());
        REQUIRE(preState.value(QStringLiteral("clip_count")).toInt() == 3);
        const int beforeDuration = preState.value(QStringLiteral("duration_frames")).toInt();

        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        QJsonObject verification;
        QString failure;
        REQUIRE(appendVibeCutTimelineRangeRemove(timeline, clipLength, 2 * clipLength, false, QVector<int>{trackId}, undo, redo, &verification, &failure));
        INFO(failure.toStdString());
        REQUIRE(verification.value(QStringLiteral("verified")).toBool());
        REQUIRE(timeline->checkConsistency());

        const QJsonObject postState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        VibeCutMutationEvalExpectation expected;
        expected.fixtureId = QStringLiteral("timeline_range_remove_ripple_success_live");
        expected.expectedOutcome = VibeCutMutationOutcome::Applied;
        expected.expectedPostState = QJsonObject{{QStringLiteral("duration_frames"), beforeDuration - clipLength},
                                                 {QStringLiteral("clip_count"), 2}};

        VibeCutMutationEvalObservation observed;
        observed.outcome = VibeCutMutationOutcome::Applied;
        observed.preEditState = preState;
        observed.postEditState = postState;

        REQUIRE(undo());
        REQUIRE(timeline->checkConsistency());
        observed.undoObserved = true;
        observed.undoState = VibeCutProjectSnapshot::mutationStateV1(timeline);

        REQUIRE(redo());
        REQUIRE(timeline->checkConsistency());
        observed.redoObserved = true;
        observed.redoState = VibeCutProjectSnapshot::mutationStateV1(timeline);

        const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
        INFO(score.failures.join(QStringLiteral("; ")).toStdString());
        CHECK(score.verifiedSuccess == Approx(1.0));
        CHECK(score.undoFidelity == Approx(1.0));
        CHECK(score.redoFidelity == Approx(1.0));
        CHECK(score.pass);
    }

    SECTION("locked track refusal preserves exact canonical state")
    {
        timeline->setTrackLockedState(trackId, true);
        const QJsonObject preState = VibeCutProjectSnapshot::mutationStateV1(timeline);
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        QString failure;
        CHECK_FALSE(appendVibeCutTimelineRangeRemove(timeline, clipLength, 2 * clipLength, false, QVector<int>{trackId}, undo, redo, nullptr, &failure));
        CHECK(failure.contains(QStringLiteral("locked"), Qt::CaseInsensitive));
        REQUIRE(timeline->checkConsistency());

        VibeCutMutationEvalExpectation expected;
        expected.fixtureId = QStringLiteral("locked_track_refusal_live");
        expected.expectedOutcome = VibeCutMutationOutcome::Refused;
        expected.expectedRefusalReason = QStringLiteral("locked_track");
        expected.requireUndo = false;
        expected.requireRedo = false;

        VibeCutMutationEvalObservation observed;
        observed.outcome = VibeCutMutationOutcome::Refused;
        observed.refusalReason = QStringLiteral("locked_track");
        observed.preEditState = preState;
        observed.postEditState = VibeCutProjectSnapshot::mutationStateV1(timeline);

        const VibeCutMutationEvalScore score = VibeCutEvaluator::evaluateMutation(expected, observed);
        INFO(score.failures.join(QStringLiteral("; ")).toStdString());
        CHECK(score.verifiedSuccess == Approx(1.0));
        CHECK(score.pass);
    }
}
