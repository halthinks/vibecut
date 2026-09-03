/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutjobmanager.h"

#include <QJsonDocument>

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

TEST_CASE("structured job results are bounded and immutable after terminal success", "[vibecut][jobs][result]")
{
    VibeCutJobManager jobs;
    const QString id = jobs.createJob(QStringLiteral("semantic_text_search"), QStringLiteral("Semantic search"), true);
    REQUIRE(jobs.markRunning(id));

    QString error;
    const QJsonObject payload{{QStringLiteral("kind"), QStringLiteral("semantic_text_search")},
                              {QStringLiteral("hits"), QJsonArray{QJsonObject{{QStringLiteral("anchor_id"), QStringLiteral("subtitle:1")},
                                                                            {QStringLiteral("similarity"), 0.83}}}}};
    REQUIRE(jobs.setResult(id, payload, &error));
    CHECK(error.isEmpty());
    REQUIRE(jobs.markSucceeded(id, QStringLiteral("done")));

    VibeCutJob job;
    REQUIRE(jobs.job(id, job));
    CHECK(job.result == payload);
    CHECK_FALSE(jobs.setResult(id, QJsonObject{{QStringLiteral("changed"), true}}, &error));
    CHECK(error.contains(QStringLiteral("terminal"), Qt::CaseInsensitive));
    REQUIRE(jobs.job(id, job));
    CHECK(job.result == payload);
}

TEST_CASE("failed and cancelled jobs never expose partial structured results", "[vibecut][jobs][result]")
{
    VibeCutJobManager jobs;
    QString error;

    const QString failed = jobs.createJob(QStringLiteral("semantic_text_search"), QStringLiteral("failed"), true);
    REQUIRE(jobs.markRunning(failed));
    REQUIRE(jobs.setResult(failed, QJsonObject{{QStringLiteral("partial"), true}}, &error));
    REQUIRE(jobs.markFailed(failed, QStringLiteral("boom")));
    VibeCutJob job;
    REQUIRE(jobs.job(failed, job));
    CHECK(job.result.isEmpty());

    const QString cancelled = jobs.createJob(QStringLiteral("semantic_text_search"), QStringLiteral("cancelled"), true);
    REQUIRE(jobs.markRunning(cancelled));
    REQUIRE(jobs.setResult(cancelled, QJsonObject{{QStringLiteral("partial"), true}}, &error));
    REQUIRE(jobs.requestCancel(cancelled));
    REQUIRE(jobs.markCancelled(cancelled));
    REQUIRE(jobs.job(cancelled, job));
    CHECK(job.result.isEmpty());
}

TEST_CASE("structured job result size limit fails closed", "[vibecut][jobs][result][bounds]")
{
    VibeCutJobManager jobs;
    const QString id = jobs.createJob(QStringLiteral("semantic_text_search"), QStringLiteral("large"), true);
    REQUIRE(jobs.markRunning(id));

    QString error;
    const QString huge(VibeCutJobManager::MaxResultBytes + 1024, QLatin1Char('x'));
    CHECK_FALSE(jobs.setResult(id, QJsonObject{{QStringLiteral("payload"), huge}}, &error));
    CHECK(error.contains(QStringLiteral("limit"), Qt::CaseInsensitive));

    VibeCutJob job;
    REQUIRE(jobs.job(id, job));
    CHECK(job.result.isEmpty());
}
