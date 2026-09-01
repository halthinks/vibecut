/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"
#include "vibecut/vibecutprojectsnapshot.h"

TEST_CASE("project snapshots produce explicit edit diffs", "[vibecut][snapshot]")
{
    VibeCutProjectSnapshot before;
    before.revision = 10; before.durationFrames = 1000; before.clips = 3; before.subtitles = 0; before.effects = 1;
    VibeCutProjectSnapshot after = before;
    after.revision = 12; after.durationFrames = 900; after.subtitles = 42; after.effects = 2;
    const VibeCutProjectDiff diff = before.diffTo(after);
    CHECK(diff.revisionDelta == 2);
    CHECK(diff.durationFramesDelta == -100);
    CHECK(diff.subtitlesDelta == 42);
    CHECK(diff.effectsDelta == 1);
    CHECK(diff.summary().contains(QStringLiteral("subtitles +42")));
}
