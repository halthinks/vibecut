/*
    SPDX-FileCopyrightText: 2026 vibecut contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "catch.hpp"

#include "vibecut/vibecutjobmanager.h"
#include "vibecut/vibecutplanruntime.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject noArgSchema(const QString &name)
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("description"), QStringLiteral("test tool")},
        {QStringLiteral("input_schema"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                     {QStringLiteral("properties"), QJsonObject{}},
                     {QStringLiteral("additionalProperties"), false}}},
    };
}

QJsonObject proposalFor(const QString &tool)
{
    return QJsonObject{
        {QStringLiteral("objective"), QStringLiteral("Test approved edit")},
        {QStringLiteral("operations"),
         QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("step")},
                                {QStringLiteral("tool"), tool},
                                {QStringLiteral("input"), QJsonObject{}},
                                {QStringLiteral("expected_postconditions"), QJsonArray{QStringLiteral("tool reports ok")}}}}},
    };
}
} // namespace

TEST_CASE("plan runtime never invokes an edit before approval", "[vibecut][plan-runtime]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    int calls = 0;

    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("test_edit");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    REQUIRE(surface.registerTool(noArgSchema(policy.name), policy,
                                 [&calls](const QJsonObject &) {
                                     ++calls;
                                     return QJsonObject{{QStringLiteral("ok"), true}};
                                 }));

    VibeCutPlanRuntime runtime(&surface);
    const QJsonObject proposed = runtime.propose(proposalFor(policy.name));
    REQUIRE(proposed.value(QStringLiteral("ok")).toBool());
    CHECK(proposed.value(QStringLiteral("awaiting_approval")).toBool());
    CHECK(runtime.hasPendingPlan());
    CHECK(calls == 0);

    const QJsonObject approved = runtime.approvePendingPlan();
    CHECK(approved.value(QStringLiteral("ok")).toBool());
    CHECK(calls == 1);
    CHECK_FALSE(runtime.executing());
    CHECK_FALSE(runtime.hasPendingPlan());
}

TEST_CASE("plan runtime pauses on a tracked async checkpoint and resumes on success", "[vibecut][plan-runtime]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    QString asyncJobId;

    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("test_async");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    REQUIRE(surface.registerTool(noArgSchema(policy.name), policy,
                                 [&base, &asyncJobId](const QJsonObject &) {
                                     asyncJobId = base.jobManager()->createJob(QStringLiteral("test"), QStringLiteral("Async test"), false);
                                     base.jobManager()->markRunning(asyncJobId);
                                     return QJsonObject{{QStringLiteral("ok"), true},
                                                        {QStringLiteral("started"), true},
                                                        {QStringLiteral("job_id"), asyncJobId}};
                                 }));

    VibeCutPlanRuntime runtime(&surface);
    REQUIRE(runtime.propose(proposalFor(policy.name)).value(QStringLiteral("ok")).toBool());
    REQUIRE(runtime.approvePendingPlan().value(QStringLiteral("ok")).toBool());
    CHECK(runtime.executing());
    CHECK(runtime.hasPendingPlan());
    REQUIRE_FALSE(asyncJobId.isEmpty());

    // jobChanged is a same-thread direct signal here, so success immediately
    // drives the runtime through its remaining checkpoints.
    REQUIRE(base.jobManager()->markSucceeded(asyncJobId, QStringLiteral("done")));
    CHECK_FALSE(runtime.executing());
    CHECK_FALSE(runtime.hasPendingPlan());
}

TEST_CASE("direct model mutation calls can be converted into a pending review plan", "[vibecut][plan-runtime]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    int calls = 0;

    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("direct_edit");
    policy.risk = VibeCutToolRisk::MajorEdit;
    policy.mutatesProject = true;
    REQUIRE(surface.registerTool(noArgSchema(policy.name), policy,
                                 [&calls](const QJsonObject &) {
                                     ++calls;
                                     return QJsonObject{{QStringLiteral("ok"), true}};
                                 }));

    VibeCutPlanRuntime runtime(&surface);
    const QJsonArray blocks{QJsonObject{{QStringLiteral("type"), QStringLiteral("tool_use")},
                                       {QStringLiteral("name"), policy.name},
                                       {QStringLiteral("input"), QJsonObject{}}}};
    const QJsonObject result = runtime.proposeDirectToolCalls(blocks);
    CHECK(result.value(QStringLiteral("ok")).toBool());
    CHECK(runtime.hasPendingPlan());
    CHECK(calls == 0);
}
