/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutjobmanager.h"

TEST_CASE("vibecut jobs have durable lifecycle state", "[vibecut][jobs]")
{
    VibeCutJobManager jobs;
    const QString id = jobs.createJob(QStringLiteral("transcription"), QStringLiteral("Generate subtitles"), true);

    VibeCutJob job;
    REQUIRE(jobs.job(id, job));
    CHECK(job.state == VibeCutJobState::Queued);
    CHECK(job.progress == -1);

    REQUIRE(jobs.markRunning(id));
    REQUIRE(jobs.setProgress(id, 37, QStringLiteral("Transcribing")));
    REQUIRE(jobs.job(id, job));
    CHECK(job.state == VibeCutJobState::Running);
    CHECK(job.progress == 37);

    REQUIRE(jobs.markSucceeded(id, QStringLiteral("591 subtitles added")));
    REQUIRE(jobs.job(id, job));
    CHECK(job.terminal());
    CHECK(job.progress == 100);
    CHECK_FALSE(jobs.setProgress(id, 50));
    CHECK_FALSE(jobs.markFailed(id, QStringLiteral("too late")));
}

TEST_CASE("job cancellation is explicit and cancelability-aware", "[vibecut][jobs]")
{
    VibeCutJobManager jobs;
    const QString cancellable = jobs.createJob(QStringLiteral("render"), QStringLiteral("Export audio"), true);
    const QString fixed = jobs.createJob(QStringLiteral("setup"), QStringLiteral("Finalize install"), false);

    VibeCutJob job;
    CHECK(jobs.requestCancel(cancellable));
    REQUIRE(jobs.job(cancellable, job));
    CHECK(job.state == VibeCutJobState::CancelRequested);
    CHECK(jobs.markCancelled(cancellable));
    REQUIRE(jobs.job(cancellable, job));
    CHECK(job.state == VibeCutJobState::Cancelled);

    CHECK_FALSE(jobs.requestCancel(fixed));
    REQUIRE(jobs.job(fixed, job));
    CHECK(job.state == VibeCutJobState::Queued);
}
