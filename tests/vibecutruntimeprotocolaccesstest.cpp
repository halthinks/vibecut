/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL */
#include "catch.hpp"

#include "vibecut/vibecutjobmanager.h"
#include "vibecut/vibecutruntimeprotocoladapter.h"
#include "vibecut/vibecuttools.h"
#include "vibecut/vibecuttoolsurface.h"

namespace {
QJsonObject request(const QString &type, const QJsonObject &payload, const QString &id)
{
    return QJsonObject{{QStringLiteral("v"), 1},
                       {QStringLiteral("id"), id},
                       {QStringLiteral("kind"), QStringLiteral("request")},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("payload"), payload}};
}

QJsonObject singleOperationPlan(quint64 revision, const QString &tool)
{
    return QJsonObject{{QStringLiteral("id"), QStringLiteral("access-plan")},
                       {QStringLiteral("base_revision"), static_cast<qint64>(revision)},
                       {QStringLiteral("objective"), QStringLiteral("test protocol-only access hooks")},
                       {QStringLiteral("operations"), QJsonArray{
                           QJsonObject{{QStringLiteral("id"), QStringLiteral("op-1")},
                                       {QStringLiteral("tool"), tool},
                                       {QStringLiteral("input"), QJsonObject()},
                                       {QStringLiteral("depends_on"), QJsonArray()},
                                       {QStringLiteral("expected_postconditions"), QJsonArray()}}}}};
}
} // namespace

TEST_CASE("protocol adapter exposes only active plan-owned async job identity", "[vibecut][runtime-protocol][job-containment]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    quint64 revision = 4;

    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("protocol_fake_async")},
                             {QStringLiteral("description"), QStringLiteral("protocol async fixture")},
                             {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("protocol_fake_async");
    policy.risk = VibeCutToolRisk::ExternalSideEffect;
    policy.asynchronous = true;
    policy.mutatesProject = false;
    QString error;
    REQUIRE(surface.registerTool(schema, policy, [&base](const QJsonObject &) {
        const QString jobId = base.jobManager()->createJob(QStringLiteral("protocol_fixture"), QStringLiteral("fixture"), true);
        base.jobManager()->markRunning(jobId);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("started"), true},
                           {QStringLiteral("job_id"), jobId}};
    }, &error));
    REQUIRE(error.isEmpty());

    VibeCutRuntimeProtocolAdapter adapter(&surface, [&revision]() { return revision; });
    REQUIRE(adapter.handleRequest(request(QStringLiteral("propose_plan"), singleOperationPlan(revision, policy.name), QStringLiteral("p")))
                .value(QStringLiteral("type")).toString() == QStringLiteral("propose_plan"));
    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    const QString auth = approved.value(QStringLiteral("payload")).toObject().value(QStringLiteral("authorization_id")).toString();
    const QJsonObject invoked = adapter.handleRequest(request(QStringLiteral("invoke"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("access-plan")},
                    {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-1")},
                    {QStringLiteral("expected_revision"), static_cast<qint64>(revision)}}, QStringLiteral("i")));
    const QString ownedJob = invoked.value(QStringLiteral("payload")).toObject().value(QStringLiteral("job_id")).toString();
    REQUIRE_FALSE(ownedJob.isEmpty());
    CHECK(adapter.ownsProtocolJob(ownedJob));
    CHECK_FALSE(adapter.ownsProtocolJob(QStringLiteral("unrelated-job")));

    base.jobManager()->markSucceeded(ownedJob, QStringLiteral("done"));
    CHECK_FALSE(adapter.ownsProtocolJob(ownedJob));
}

TEST_CASE("protocol checkpoint revision resync is editor authoritative", "[vibecut][runtime-protocol][revision-resync]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    quint64 revision = 12;

    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("protocol_fake_sync")},
                             {QStringLiteral("description"), QStringLiteral("protocol sync fixture")},
                             {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("protocol_fake_sync");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    QString error;
    REQUIRE(surface.registerTool(schema, policy, [](const QJsonObject &) {
        return QJsonObject{{QStringLiteral("ok"), true}};
    }, &error));
    REQUIRE(error.isEmpty());

    VibeCutRuntimeProtocolAdapter adapter(&surface, [&revision]() { return revision; });
    adapter.handleRequest(request(QStringLiteral("propose_plan"), singleOperationPlan(revision, policy.name), QStringLiteral("p")));
    adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    REQUIRE(adapter.expectedRevision() == 12);

    revision = 13;
    CHECK(adapter.synchronizeExpectedRevision() == 13);
    CHECK(adapter.expectedRevision() == 13);
    CHECK(adapter.protocolProjectRevision() == 13);
}

TEST_CASE("invoke preflight refuses stale or substituted requests before native execution", "[vibecut][runtime-protocol][preflight]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    quint64 revision = 21;
    int mutationCalls = 0;

    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("protocol_preflight_mutate")},
                             {QStringLiteral("description"), QStringLiteral("preflight fixture")},
                             {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("protocol_preflight_mutate");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    QString error;
    REQUIRE(surface.registerTool(schema, policy, [&mutationCalls](const QJsonObject &) {
        ++mutationCalls;
        return QJsonObject{{QStringLiteral("ok"), true}};
    }, &error));
    REQUIRE(error.isEmpty());

    VibeCutRuntimeProtocolAdapter adapter(&surface, [&revision]() { return revision; });
    adapter.handleRequest(request(QStringLiteral("propose_plan"), singleOperationPlan(revision, policy.name), QStringLiteral("p")));
    const QJsonObject approved = adapter.authorizePending(VibeCutTrustMode::Off, true, true);
    const QString auth = approved.value(QStringLiteral("payload")).toObject().value(QStringLiteral("authorization_id")).toString();

    VibeCutToolPolicy preflightPolicy;
    QString code;
    error.clear();
    const QJsonObject validInvoke = request(QStringLiteral("invoke"),
        QJsonObject{{QStringLiteral("plan_id"), QStringLiteral("access-plan")},
                    {QStringLiteral("authorization_id"), auth},
                    {QStringLiteral("operation_id"), QStringLiteral("op-1")},
                    {QStringLiteral("expected_revision"), static_cast<qint64>(revision)}}, QStringLiteral("i-valid"));
    CHECK(adapter.preflightInvoke(validInvoke, preflightPolicy, &code, &error));
    CHECK(code.isEmpty());
    CHECK(error.isEmpty());
    CHECK(preflightPolicy.name == policy.name);
    CHECK(preflightPolicy.mutatesProject);
    CHECK(mutationCalls == 0);

    QJsonObject substituted = validInvoke;
    QJsonObject substitutedPayload = substituted.value(QStringLiteral("payload")).toObject();
    substitutedPayload.insert(QStringLiteral("input"), QJsonObject{{QStringLiteral("invented"), true}});
    substituted.insert(QStringLiteral("payload"), substitutedPayload);
    error.clear();
    code.clear();
    CHECK_FALSE(adapter.preflightInvoke(substituted, preflightPolicy, &code, &error));
    CHECK(code == QStringLiteral("plan_substitution_attempt"));
    CHECK(mutationCalls == 0);

    ++revision;
    error.clear();
    code.clear();
    CHECK_FALSE(adapter.preflightInvoke(validInvoke, preflightPolicy, &code, &error));
    CHECK(code == QStringLiteral("stale_revision"));
    CHECK(mutationCalls == 0);
}

TEST_CASE("host may discard only an unapproved staged protocol plan", "[vibecut][runtime-protocol][host-discard]")
{
    VibeCutTools base;
    VibeCutToolSurface surface(&base);
    quint64 revision = 31;

    const QJsonObject schema{{QStringLiteral("name"), QStringLiteral("protocol_host_discard")},
                             {QStringLiteral("description"), QStringLiteral("host discard fixture")},
                             {QStringLiteral("input_schema"), QJsonObject{{QStringLiteral("type"), QStringLiteral("object")}}}};
    VibeCutToolPolicy policy;
    policy.name = QStringLiteral("protocol_host_discard");
    policy.risk = VibeCutToolRisk::ReversibleEdit;
    policy.reversible = true;
    policy.mutatesProject = true;
    QString error;
    REQUIRE(surface.registerTool(schema, policy, [](const QJsonObject &) {
        return QJsonObject{{QStringLiteral("ok"), true}};
    }, &error));
    REQUIRE(error.isEmpty());

    VibeCutRuntimeProtocolAdapter adapter(&surface, [&revision]() { return revision; });
    const QJsonObject staged = adapter.stageHostPlan(singleOperationPlan(revision, policy.name));
    REQUIRE(staged.value(QStringLiteral("type")).toString() == QStringLiteral("propose_plan"));
    REQUIRE(adapter.hasPendingPlan());
    CHECK_FALSE(adapter.hasAuthorization());

    error.clear();
    CHECK(adapter.discardUnapprovedHostPlan(&error));
    CHECK(error.isEmpty());
    CHECK_FALSE(adapter.hasPendingPlan());

    REQUIRE(adapter.stageHostPlan(singleOperationPlan(revision, policy.name)).value(QStringLiteral("type")).toString() == QStringLiteral("propose_plan"));
    REQUIRE(adapter.authorizePending(VibeCutTrustMode::Off, true, true).value(QStringLiteral("type")).toString() == QStringLiteral("authorize"));
    REQUIRE(adapter.hasAuthorization());
    error.clear();
    CHECK_FALSE(adapter.discardUnapprovedHostPlan(&error));
    CHECK(error.contains(QStringLiteral("after authorization")));
    CHECK(adapter.hasPendingPlan());
    CHECK(adapter.hasAuthorization());
}
